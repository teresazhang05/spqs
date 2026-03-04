#pragma once

#include <cstdint>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/types.hpp"

namespace spqs {

struct LocalBlockMatrix {
  int rows = 0;
  int cols = 0;
  AlignedDoubles data;

  [[nodiscard]] const double* row_ptr(int row) const { return data.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(cols); }
};

struct ConstraintsLocal {
  BlockLayout layout;
  int m_local_per_block = 0;
  std::vector<LocalBlockMatrix> A_block;
  std::vector<int> row_id_base;

  [[nodiscard]] int total_rows() const { return layout.B * m_local_per_block; }
  [[nodiscard]] int constraint_id(int block_id, int row_in_block) const;
};

ConstraintsLocal generate_local_constraints(const BlockLayout& layout,
                                            int m_local_per_block,
                                            std::uint64_t seed);

}  // namespace spqs
