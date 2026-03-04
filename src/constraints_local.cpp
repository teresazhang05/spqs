#include "spqs/constraints_local.hpp"

#include <cmath>
#include <random>

#include "spqs/assert.hpp"

namespace spqs {

int ConstraintsLocal::constraint_id(int block_id, int row_in_block) const {
  SPQS_CHECK(block_id >= 0 && block_id < layout.B, "block_id out of range");
  SPQS_CHECK(row_in_block >= 0 && row_in_block < m_local_per_block, "row_in_block out of range");
  return row_id_base.at(static_cast<std::size_t>(block_id)) + row_in_block;
}

ConstraintsLocal generate_local_constraints(const BlockLayout& layout,
                                            int m_local_per_block,
                                            std::uint64_t seed) {
  SPQS_CHECK(m_local_per_block > 0, "m_local_per_block must be positive");

  ConstraintsLocal out;
  out.layout = layout;
  out.m_local_per_block = m_local_per_block;
  out.A_block.resize(static_cast<std::size_t>(layout.B));
  out.row_id_base.resize(static_cast<std::size_t>(layout.B));

  std::mt19937_64 rng(seed);
  std::normal_distribution<double> nd(0.0, 1.0);

  int running_row_id = 0;
  for (int r = 0; r < layout.B; ++r) {
    const int cols = layout.block_sizes[r];
    const int rows = m_local_per_block;

    out.row_id_base[static_cast<std::size_t>(r)] = running_row_id;
    running_row_id += rows;

    LocalBlockMatrix block;
    block.rows = rows;
    block.cols = cols;
    block.data.resize(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));

    for (int i = 0; i < rows; ++i) {
      double norm2 = 0.0;
      double* row = block.data.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(cols);
      for (int j = 0; j < cols; ++j) {
        const double v = nd(rng);
        row[j] = v;
        norm2 += v * v;
      }
      double norm = std::sqrt(norm2);
      if (norm == 0.0) {
        norm = 1.0;
      }
      for (int j = 0; j < cols; ++j) {
        row[j] /= norm;
      }
    }

    out.A_block[static_cast<std::size_t>(r)] = std::move(block);
  }

  return out;
}

}  // namespace spqs
