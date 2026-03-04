#pragma once

#include <random>

namespace spqs {

using Rng64 = std::mt19937_64;

inline Rng64 make_rng(unsigned long long seed) {
  return Rng64(seed);
}

}  // namespace spqs
