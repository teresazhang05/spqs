#pragma once

#include <cstdint>

namespace spqs {

struct LoopCounters {
  std::uint64_t iterations = 0;
  std::uint64_t fallback_count = 0;
};

}  // namespace spqs
