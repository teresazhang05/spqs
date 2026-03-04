#include "spqs/brute_force_scan.hpp"

#include <cmath>
#include <limits>

#include "spqs/assert.hpp"
#include "spqs/fp_cert.hpp"

namespace spqs {

bool prefer_lhs(const Violator& lhs, const Violator& rhs) {
  if (std::isnan(lhs.violation) && !std::isnan(rhs.violation)) {
    return false;
  }
  if (!std::isnan(lhs.violation) && std::isnan(rhs.violation)) {
    return true;
  }
  if (lhs.violation > rhs.violation) {
    return true;
  }
  if (lhs.violation < rhs.violation) {
    return false;
  }

  if (lhs.scope != rhs.scope) {
    return lhs.scope == ConstraintScope::LOCAL;
  }

  return lhs.constraint_id < rhs.constraint_id;
}

BruteForceScan::BruteForceScan(const ConstraintsLocal* local,
                               const ConstraintsGlobal* global,
                               double tau_abs_scale)
    : local_(local), global_(global), tau_abs_scale_(tau_abs_scale) {
  SPQS_CHECK(local_ != nullptr, "BruteForceScan local pointer is null");
  SPQS_CHECK(global_ != nullptr, "BruteForceScan global pointer is null");
}

Violator BruteForceScan::max_violation(const double* q_n,
                                       const RHSAll& rhs) const {
  Violator best;
  best.violation = -std::numeric_limits<double>::infinity();
  best.constraint_id = std::numeric_limits<int>::max();

  for (int r = 0; r < local_->layout.B; ++r) {
    const auto& block = local_->A_block[static_cast<std::size_t>(r)];
    const int offset = local_->layout.block_offsets[r];
    const double* q_block = q_n + offset;
    const double* b_block = rhs.local.block_ptr(r);

    for (int i = 0; i < block.rows; ++i) {
      const double* a_row = block.row_ptr(i);
      const CertifiedDot cd = dot_certified_fma(a_row, q_block, block.cols);
      const double v = certified_violation_from_dot(
          cd.s_hat, cd.t_abs, block.cols, b_block[i], tau_abs_scale_);

      Violator cand;
      cand.scope = ConstraintScope::LOCAL;
      cand.block_id = r;
      cand.row_in_block = i;
      cand.global_row = -1;
      cand.constraint_id = local_->constraint_id(r, i);
      cand.violation = v;

      if (prefer_lhs(cand, best)) {
        best = cand;
      }
    }
  }

  const int global_base = local_->total_rows();
  const double* b_global = rhs.global.data();
  for (int g = 0; g < global_->m_global; ++g) {
    const double* a_row = global_->row_ptr(g);
    const CertifiedDot cd = dot_certified_fma(a_row, q_n, global_->n);
    const double v = certified_violation_from_dot(
        cd.s_hat, cd.t_abs, global_->n, b_global[g], tau_abs_scale_);

    Violator cand;
    cand.scope = ConstraintScope::GLOBAL;
    cand.block_id = -1;
    cand.row_in_block = -1;
    cand.global_row = g;
    cand.constraint_id = global_base + g;
    cand.violation = v;

    if (prefer_lhs(cand, best)) {
      best = cand;
    }
  }

  return best;
}

bool BruteForceScan::certified_feasible(const double* q_n,
                                        const RHSAll& rhs) const {
  return max_violation(q_n, rhs).violation <= 0.0;
}

double BruteForceScan::min_certified_slack(const double* q_n,
                                           const RHSAll& rhs) const {
  double min_slack = std::numeric_limits<double>::infinity();

  for (int r = 0; r < local_->layout.B; ++r) {
    const auto& block = local_->A_block[static_cast<std::size_t>(r)];
    const int offset = local_->layout.block_offsets[r];
    const double* q_block = q_n + offset;
    const double* b_block = rhs.local.block_ptr(r);

    for (int i = 0; i < block.rows; ++i) {
      const CertifiedDot cd = dot_certified_fma(block.row_ptr(i), q_block, block.cols);
      const double slack = certified_slack_from_dot(
          cd.s_hat, cd.t_abs, block.cols, b_block[i], tau_abs_scale_);
      if (slack < min_slack) {
        min_slack = slack;
      }
    }
  }

  const double* b_global = rhs.global.data();
  for (int g = 0; g < global_->m_global; ++g) {
    const CertifiedDot cd = dot_certified_fma(global_->row_ptr(g), q_n, global_->n);
    const double slack = certified_slack_from_dot(
        cd.s_hat, cd.t_abs, global_->n, b_global[g], tau_abs_scale_);
    if (slack < min_slack) {
      min_slack = slack;
    }
  }

  return min_slack;
}

bool BruteForceScan::audit_feasible_long_double(const double* q_n,
                                                const RHSAll& rhs,
                                                long double tol) const {
  for (int r = 0; r < local_->layout.B; ++r) {
    const auto& block = local_->A_block[static_cast<std::size_t>(r)];
    const int offset = local_->layout.block_offsets[r];
    const double* b_block = rhs.local.block_ptr(r);

    for (int i = 0; i < block.rows; ++i) {
      const double* a_row = block.row_ptr(i);
      long double s = 0.0L;
      for (int j = 0; j < block.cols; ++j) {
        s += static_cast<long double>(a_row[j]) *
             static_cast<long double>(q_n[offset + j]);
      }
      if (s > static_cast<long double>(b_block[i]) + tol) {
        return false;
      }
    }
  }

  for (int g = 0; g < global_->m_global; ++g) {
    const double* a_row = global_->row_ptr(g);
    long double s = 0.0L;
    for (int j = 0; j < global_->n; ++j) {
      s += static_cast<long double>(a_row[j]) * static_cast<long double>(q_n[j]);
    }
    if (s > static_cast<long double>(rhs.global.b[static_cast<std::size_t>(g)]) + tol) {
      return false;
    }
  }

  return true;
}

}  // namespace spqs
