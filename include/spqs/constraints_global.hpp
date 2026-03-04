#pragma once

#include <cstdint>

#include "spqs/types.hpp"

namespace spqs {

struct ConstraintsGlobal {
  int n = 0;
  int m_global = 0;
  AlignedDoubles A;

  [[nodiscard]] const double* row_ptr(int row) const { return A.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(n); }
};

ConstraintsGlobal generate_global_constraints(int n,
                                              int m_global,
                                              int factors,
                                              std::uint64_t seed);

}  // namespace spqs
