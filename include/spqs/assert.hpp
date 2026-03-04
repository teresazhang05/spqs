#pragma once

#include <cstdio>
#include <cstdlib>

namespace spqs {

[[noreturn]] inline void fail_fast(const char* expr,
                                   const char* msg,
                                   const char* file,
                                   int line) noexcept {
  std::fprintf(stderr, "SPQS_CHECK failed: (%s) %s at %s:%d\n", expr, msg, file, line);
  std::fflush(stderr);
  std::abort();
}

}  // namespace spqs

#define SPQS_CHECK(cond, msg)                                                   \
  do {                                                                          \
    if (!(cond)) {                                                              \
      ::spqs::fail_fast(#cond, (msg), __FILE__, __LINE__);                     \
    }                                                                           \
  } while (false)

