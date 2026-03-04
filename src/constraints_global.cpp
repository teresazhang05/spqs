#include "spqs/constraints_global.hpp"

#include <cmath>
#include <random>

#include "spqs/assert.hpp"
#include "spqs/rhs.hpp"

namespace spqs {
namespace {

void normalize_row(double* row, int n) {
  double norm2 = 0.0;
  for (int j = 0; j < n; ++j) {
    norm2 += row[j] * row[j];
  }
  double norm = std::sqrt(norm2);
  if (norm == 0.0) {
    norm = 1.0;
  }
  for (int j = 0; j < n; ++j) {
    row[j] /= norm;
  }
}

}  // namespace

ConstraintsGlobal generate_global_constraints(int n,
                                              int m_global,
                                              int factors,
                                              std::uint64_t seed) {
  SPQS_CHECK(n > 0, "n must be positive");
  SPQS_CHECK(m_global >= 0, "m_global must be >= 0");

  ConstraintsGlobal out;
  out.n = n;
  out.m_global = m_global;
  out.A.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(m_global));

  std::mt19937_64 rng(seed + 0x9e3779b97f4a7c15ULL);
  std::normal_distribution<double> nd(0.0, 1.0);

  int row_idx = 0;

  const int factor_rows = std::min(factors, m_global);
  for (; row_idx < factor_rows; ++row_idx) {
    double* row = out.A.data() + static_cast<std::size_t>(row_idx) * static_cast<std::size_t>(n);
    for (int j = 0; j < n; ++j) {
      row[j] = nd(rng);
    }
    normalize_row(row, n);
  }

  const int gross_rows = std::min(8, m_global - row_idx);
  for (int k = 0; k < gross_rows; ++k, ++row_idx) {
    double* row = out.A.data() + static_cast<std::size_t>(row_idx) * static_cast<std::size_t>(n);
    const double sign = ((k % 2) == 0) ? 1.0 : -1.0;
    const double variant = 1.0 + 0.05 * static_cast<double>(k / 2);
    for (int j = 0; j < n; ++j) {
      row[j] = sign * variant;
    }
    normalize_row(row, n);
  }

  for (; row_idx < m_global; ++row_idx) {
    double* row = out.A.data() + static_cast<std::size_t>(row_idx) * static_cast<std::size_t>(n);
    for (int j = 0; j < n; ++j) {
      row[j] = nd(rng);
    }
    normalize_row(row, n);
  }

  return out;
}

RHSAll generate_rhs(const ConstraintsLocal& local,
                    const ConstraintsGlobal& global,
                    double b_margin,
                    double b_noise_std,
                    std::uint64_t seed) {
  RHSAll rhs;
  rhs.local.B = local.layout.B;
  rhs.local.m_local_per_block = local.m_local_per_block;
  rhs.local.b_block.resize(static_cast<std::size_t>(local.layout.B));

  std::mt19937_64 rng(seed + 0x243f6a8885a308d3ULL);
  std::normal_distribution<double> nd(0.0, b_noise_std);

  for (int r = 0; r < local.layout.B; ++r) {
    AlignedDoubles block_b(static_cast<std::size_t>(local.m_local_per_block));
    for (int i = 0; i < local.m_local_per_block; ++i) {
      const double v = b_margin + nd(rng);
      block_b[static_cast<std::size_t>(i)] = std::max(v, 1.0);
    }
    rhs.local.b_block[static_cast<std::size_t>(r)] = std::move(block_b);
  }

  rhs.global.m_global = global.m_global;
  rhs.global.b.resize(static_cast<std::size_t>(global.m_global));
  for (int i = 0; i < global.m_global; ++i) {
    const double v = b_margin + nd(rng);
    rhs.global.b[static_cast<std::size_t>(i)] = std::max(v, 1.0);
  }

  return rhs;
}

}  // namespace spqs
