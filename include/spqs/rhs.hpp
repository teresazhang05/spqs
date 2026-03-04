#pragma once

#include <cstdint>
#include <vector>

#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/types.hpp"

namespace spqs {

struct RHSLocal {
  int B = 0;
  int m_local_per_block = 0;
  std::vector<AlignedDoubles> b_block;

  [[nodiscard]] const double* block_ptr(int block_id) const { return b_block.at(static_cast<std::size_t>(block_id)).data(); }
};

struct RHSGlobal {
  int m_global = 0;
  AlignedDoubles b;

  [[nodiscard]] const double* data() const { return b.data(); }
};

struct RHSAll {
  RHSLocal local;
  RHSGlobal global;
};

RHSAll generate_rhs(const ConstraintsLocal& local,
                    const ConstraintsGlobal& global,
                    double b_margin,
                    double b_noise_std,
                    std::uint64_t seed);

}  // namespace spqs
