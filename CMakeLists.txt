cmake_minimum_required(VERSION 3.20)
project(streaming_proj_qpsolver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SPQS_ENABLE_QPOASES "Enable qpOASES baseline wrapper" OFF)
option(SPQS_ENABLE_OSQP "Enable OSQP baseline wrapper" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "DebugSan")
  add_compile_options(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  set(SPQS_MODE_CFLAGS "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3 -march=native -fno-exceptions -fno-rtti)
  set(SPQS_MODE_CFLAGS "-O3 -march=native -fno-exceptions -fno-rtti")
else()
  set(SPQS_MODE_CFLAGS "")
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
set(SPQS_COMMON_CFLAGS "-Wall -Wextra -Wpedantic -Werror")

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE SPQS_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT SPQS_GIT_COMMIT)
  set(SPQS_GIT_COMMIT "unknown")
endif()

set(SPQS_CFLAGS_STR "${SPQS_MODE_CFLAGS} ${SPQS_COMMON_CFLAGS}")
set(SPQS_COMPILER_STR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

set(SPQS_SOURCES
  src/arena.cpp
  src/alloc_counter.cpp
  src/timing.cpp
  src/config_loader.cpp
  src/block_layout.cpp
  src/constraints_local.cpp
  src/constraints_global.cpp
  src/structure_validator.cpp
  src/violator_oracle.cpp
  src/brute_force_scan.cpp
  src/linalg_small.cpp
  src/chol_rank1.cpp
  src/cholesky_update.cpp
  src/projector.cpp
  src/fp_cert.cpp
  src/safefallback.cpp
  src/counters.cpp
  src/logschema.cpp
  src/stream_gen.cpp
  baselines/osqp_wrapper.cpp
)

add_library(spqs STATIC ${SPQS_SOURCES})
target_include_directories(spqs PUBLIC include)
target_compile_definitions(spqs PUBLIC
  SPQS_GIT_COMMIT="${SPQS_GIT_COMMIT}"
  SPQS_COMPILER_STR="${SPQS_COMPILER_STR}"
  SPQS_CFLAGS_STR="${SPQS_CFLAGS_STR}"
)

add_executable(bench_projector benches/bench_projector.cpp)
target_link_libraries(bench_projector PRIVATE spqs)

add_executable(bench_feasibility_only benches/bench_feasibility_only.cpp)
target_link_libraries(bench_feasibility_only PRIVATE spqs)

add_executable(bench_oracle_correctness benches/bench_oracle_correctness.cpp)
target_link_libraries(bench_oracle_correctness PRIVATE spqs)

add_executable(bench_baselines benches/bench_baselines.cpp)
target_link_libraries(bench_baselines PRIVATE spqs)

set(SPQS_HAS_QPOASES 0)
if(SPQS_ENABLE_QPOASES)
  target_sources(spqs PRIVATE baselines/qpoases_wrapper.cpp)
  set(SPQS_HAS_QPOASES 1)
endif()
target_compile_definitions(spqs PUBLIC SPQS_ENABLE_QPOASES=${SPQS_HAS_QPOASES})

set(SPQS_HAS_OSQP 0)
if(SPQS_ENABLE_OSQP)
  find_path(OSQP_INCLUDE_DIR NAMES osqp/osqp.h)
  find_library(OSQP_LIBRARY NAMES osqp osqpstatic)
  if(OSQP_INCLUDE_DIR AND OSQP_LIBRARY)
    target_include_directories(spqs PRIVATE ${OSQP_INCLUDE_DIR})
    target_link_libraries(spqs PUBLIC ${OSQP_LIBRARY})
    set(SPQS_HAS_OSQP 1)
  else()
    message(WARNING "SPQS_ENABLE_OSQP=ON but OSQP library/headers not found; building without OSQP baseline")
  endif()
endif()
target_compile_definitions(spqs PUBLIC SPQS_ENABLE_OSQP=${SPQS_HAS_OSQP})

set(SPQS_ALLOC_WRAP_LINKOPTS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(SPQS_ALLOC_WRAP_LINKOPTS
    -Wl,--wrap=malloc
    -Wl,--wrap=calloc
    -Wl,--wrap=realloc
    -Wl,--wrap=free
  )
endif()

target_link_options(bench_projector PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
target_link_options(bench_feasibility_only PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
target_link_options(bench_oracle_correctness PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
target_link_options(bench_baselines PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})

enable_testing()

add_executable(test_oracle_vs_bruteforce tests/test_oracle_vs_bruteforce.cpp)
target_link_libraries(test_oracle_vs_bruteforce PRIVATE spqs)
target_link_options(test_oracle_vs_bruteforce PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_oracle_vs_bruteforce COMMAND test_oracle_vs_bruteforce)

add_executable(test_oracle_inactive_vs_bruteforce tests/test_oracle_inactive_vs_bruteforce.cpp)
target_link_libraries(test_oracle_inactive_vs_bruteforce PRIVATE spqs)
target_link_options(test_oracle_inactive_vs_bruteforce PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_oracle_inactive_vs_bruteforce COMMAND test_oracle_inactive_vs_bruteforce)

add_executable(test_fp_cert_soundness tests/test_fp_cert_soundness.cpp)
target_link_libraries(test_fp_cert_soundness PRIVATE spqs)
target_link_options(test_fp_cert_soundness PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_fp_cert_soundness COMMAND test_fp_cert_soundness)

add_executable(test_projector_vs_reference tests/test_projector_vs_reference.cpp)
target_link_libraries(test_projector_vs_reference PRIVATE spqs)
target_link_options(test_projector_vs_reference PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_projector_vs_reference COMMAND test_projector_vs_reference)

add_executable(test_cholesky_update tests/test_cholesky_update.cpp)
target_link_libraries(test_cholesky_update PRIVATE spqs)
target_link_options(test_cholesky_update PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_cholesky_update COMMAND test_cholesky_update)

add_executable(test_chol_rank1_insert_remove_matches_refactor
  tests/test_chol_rank1_insert_remove_matches_refactor.cpp)
target_link_libraries(test_chol_rank1_insert_remove_matches_refactor PRIVATE spqs)
target_link_options(test_chol_rank1_insert_remove_matches_refactor PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_chol_rank1_insert_remove_matches_refactor
  COMMAND test_chol_rank1_insert_remove_matches_refactor)

add_executable(test_stage6_baselines_and_logs tests/test_stage6_baselines_and_logs.cpp)
target_link_libraries(test_stage6_baselines_and_logs PRIVATE spqs)
target_link_options(test_stage6_baselines_and_logs PRIVATE ${SPQS_ALLOC_WRAP_LINKOPTS})
add_test(NAME test_stage6_baselines_and_logs COMMAND test_stage6_baselines_and_logs)
