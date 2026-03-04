#include "spqs/alloc_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace spqs {
namespace alloc_detail {

std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_malloc_calls{0};
std::atomic<std::uint64_t> g_calloc_calls{0};
std::atomic<std::uint64_t> g_realloc_calls{0};
std::atomic<std::uint64_t> g_free_calls{0};
std::atomic<std::uint64_t> g_new_calls{0};
std::atomic<std::uint64_t> g_delete_calls{0};
std::atomic<std::uint64_t> g_bytes_allocated{0};

inline bool enabled() noexcept {
  return g_enabled.load(std::memory_order_relaxed);
}

#if defined(__linux__)
extern "C" void* __real_malloc(std::size_t nbytes);
extern "C" void* __real_calloc(std::size_t nmemb, std::size_t nbytes);
extern "C" void* __real_realloc(void* ptr, std::size_t nbytes);
extern "C" void __real_free(void* ptr);
#endif

inline void* raw_malloc(std::size_t nbytes) noexcept {
#if defined(__linux__)
  return __real_malloc(nbytes);
#else
  return std::malloc(nbytes);
#endif
}

inline void raw_free(void* ptr) noexcept {
#if defined(__linux__)
  __real_free(ptr);
#else
  std::free(ptr);
#endif
}

}  // namespace alloc_detail

void alloc_counter_reset() {
  alloc_detail::g_malloc_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_calloc_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_realloc_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_free_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_new_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_delete_calls.store(0, std::memory_order_relaxed);
  alloc_detail::g_bytes_allocated.store(0, std::memory_order_relaxed);
}

void alloc_counter_set_enabled(bool enabled_flag) {
  alloc_detail::g_enabled.store(enabled_flag, std::memory_order_relaxed);
}

bool alloc_counter_enabled() {
  return alloc_detail::enabled();
}

AllocCounters alloc_counter_snapshot() {
  AllocCounters out;
  out.malloc_calls = alloc_detail::g_malloc_calls.load(std::memory_order_relaxed);
  out.calloc_calls = alloc_detail::g_calloc_calls.load(std::memory_order_relaxed);
  out.realloc_calls = alloc_detail::g_realloc_calls.load(std::memory_order_relaxed);
  out.free_calls = alloc_detail::g_free_calls.load(std::memory_order_relaxed);
  out.new_calls = alloc_detail::g_new_calls.load(std::memory_order_relaxed);
  out.delete_calls = alloc_detail::g_delete_calls.load(std::memory_order_relaxed);
  out.bytes_allocated = alloc_detail::g_bytes_allocated.load(std::memory_order_relaxed);
  return out;
}

void alloc_counter_note_malloc(std::size_t nbytes) {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_malloc_calls.fetch_add(1, std::memory_order_relaxed);
  alloc_detail::g_bytes_allocated.fetch_add(static_cast<std::uint64_t>(nbytes), std::memory_order_relaxed);
}

void alloc_counter_note_calloc(std::size_t nbytes) {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_calloc_calls.fetch_add(1, std::memory_order_relaxed);
  alloc_detail::g_bytes_allocated.fetch_add(static_cast<std::uint64_t>(nbytes), std::memory_order_relaxed);
}

void alloc_counter_note_realloc(std::size_t nbytes) {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_realloc_calls.fetch_add(1, std::memory_order_relaxed);
  alloc_detail::g_bytes_allocated.fetch_add(static_cast<std::uint64_t>(nbytes), std::memory_order_relaxed);
}

void alloc_counter_note_free() {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_free_calls.fetch_add(1, std::memory_order_relaxed);
}

void alloc_counter_note_new(std::size_t nbytes) {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_new_calls.fetch_add(1, std::memory_order_relaxed);
  alloc_detail::g_bytes_allocated.fetch_add(static_cast<std::uint64_t>(nbytes), std::memory_order_relaxed);
}

void alloc_counter_note_delete() {
  if (!alloc_detail::enabled()) {
    return;
  }
  alloc_detail::g_delete_calls.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace spqs

extern "C" {

#if defined(__linux__)
void* __wrap_malloc(std::size_t nbytes) {
  spqs::alloc_counter_note_malloc(nbytes);
  return spqs::alloc_detail::__real_malloc(nbytes);
}

void* __wrap_calloc(std::size_t nmemb, std::size_t nbytes) {
  const std::size_t total = nmemb * nbytes;
  spqs::alloc_counter_note_calloc(total);
  return spqs::alloc_detail::__real_calloc(nmemb, nbytes);
}

void* __wrap_realloc(void* ptr, std::size_t nbytes) {
  spqs::alloc_counter_note_realloc(nbytes);
  return spqs::alloc_detail::__real_realloc(ptr, nbytes);
}

void __wrap_free(void* ptr) {
  spqs::alloc_counter_note_free();
  spqs::alloc_detail::__real_free(ptr);
}
#endif

}  // extern "C"

void* operator new(std::size_t nbytes) {
  spqs::alloc_counter_note_new(nbytes);
  if (void* p = spqs::alloc_detail::raw_malloc(nbytes)) {
    return p;
  }
  std::abort();
}

void* operator new[](std::size_t nbytes) {
  spqs::alloc_counter_note_new(nbytes);
  if (void* p = spqs::alloc_detail::raw_malloc(nbytes)) {
    return p;
  }
  std::abort();
}

void* operator new(std::size_t nbytes, const std::nothrow_t&) noexcept {
  spqs::alloc_counter_note_new(nbytes);
  return spqs::alloc_detail::raw_malloc(nbytes);
}

void* operator new[](std::size_t nbytes, const std::nothrow_t&) noexcept {
  spqs::alloc_counter_note_new(nbytes);
  return spqs::alloc_detail::raw_malloc(nbytes);
}

void operator delete(void* ptr) noexcept {
  spqs::alloc_counter_note_delete();
  spqs::alloc_detail::raw_free(ptr);
}

void operator delete[](void* ptr) noexcept {
  spqs::alloc_counter_note_delete();
  spqs::alloc_detail::raw_free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
  spqs::alloc_counter_note_delete();
  spqs::alloc_detail::raw_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
  spqs::alloc_counter_note_delete();
  spqs::alloc_detail::raw_free(ptr);
}
