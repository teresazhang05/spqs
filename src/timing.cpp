#include "spqs/timing.hpp"

#include <chrono>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace spqs {

std::uint64_t read_tsc() {
#if defined(__x86_64__) || defined(_M_X64)
  return __rdtsc();
#else
  return static_cast<std::uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}

}  // namespace spqs
