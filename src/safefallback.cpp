#include "spqs/safefallback.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "spqs/assert.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/fp_cert.hpp"

namespace spqs {

bool compute_tau_required_to_anchor_certified(const ConstraintsLocal& local,
                                              const ConstraintsGlobal& global,
                                              const RHSAll& rhs,
                                              const double* q_n,
                                              const double* q_anchor_n,
                                              double tau_abs_scale,
                                              double kappa_min,
                                              InteriorizationReport* report) {
  SPQS_CHECK(q_n != nullptr, "compute_tau_required_to_anchor_certified q_n is null");
  SPQS_CHECK(q_anchor_n != nullptr,
             "compute_tau_required_to_anchor_certified q_anchor_n is null");
  SPQS_CHECK(report != nullptr,
             "compute_tau_required_to_anchor_certified report is null");

  InteriorizationReport r;
  r.kappa_valid = true;
  r.min_kappa = std::numeric_limits<double>::infinity();
  r.tau_required = 0.0;

  for (int block_id = 0; block_id < local.layout.B; ++block_id) {
    const auto& block = local.A_block[static_cast<std::size_t>(block_id)];
    const int offset = local.layout.block_offsets[block_id];
    const double* q_block = q_n + offset;
    const double* anchor_block = q_anchor_n + offset;
    const double* b_block = rhs.local.block_ptr(block_id);

    for (int i = 0; i < block.rows; ++i) {
      const double* row = block.row_ptr(i);
      const CertifiedDot cd = dot_certified_fma(row, q_block, block.cols);

      double anchor_dot = 0.0;
      for (int j = 0; j < block.cols; ++j) {
        anchor_dot += row[j] * anchor_block[j];
      }

      const double b_i = b_block[i];
      const double e_i = gamma_s(block.cols) * cd.t_abs + tau_abs(b_i, tau_abs_scale);
      const double kappa_i = b_i - anchor_dot;

      r.min_kappa = std::min(r.min_kappa, kappa_i);
      if (!(kappa_i >= kappa_min)) {
        r.kappa_valid = false;
        continue;
      }

      const double ratio = e_i / kappa_i;
      if (ratio > r.tau_required) {
        r.tau_required = ratio;
      }
    }
  }

  for (int g = 0; g < global.m_global; ++g) {
    const double* row = global.row_ptr(g);
    const CertifiedDot cd = dot_certified_fma(row, q_n, global.n);

    double anchor_dot = 0.0;
    for (int j = 0; j < global.n; ++j) {
      anchor_dot += row[j] * q_anchor_n[j];
    }

    const double b_i = rhs.global.b[static_cast<std::size_t>(g)];
    const double e_i = gamma_s(global.n) * cd.t_abs + tau_abs(b_i, tau_abs_scale);
    const double kappa_i = b_i - anchor_dot;

    r.min_kappa = std::min(r.min_kappa, kappa_i);
    if (!(kappa_i >= kappa_min)) {
      r.kappa_valid = false;
      continue;
    }

    const double ratio = e_i / kappa_i;
    if (ratio > r.tau_required) {
      r.tau_required = ratio;
    }
  }

  if (!std::isfinite(r.min_kappa)) {
    r.min_kappa = 0.0;
  }
  if (!std::isfinite(r.tau_required)) {
    r.tau_required = std::numeric_limits<double>::infinity();
  }

  *report = r;
  return true;
}

double ray_scale_alpha_to_anchor_certified(const ConstraintsLocal& local,
                                           const ConstraintsGlobal& global,
                                           const RHSAll& rhs,
                                           const double* q_prop_n,
                                           const double* q_anchor_n,
                                           double tau_abs_scale,
                                           int /*max_bisect_iters*/) {
  SPQS_CHECK(q_prop_n != nullptr, "ray_scale_alpha_to_anchor_certified q_prop_n is null");
  SPQS_CHECK(q_anchor_n != nullptr,
             "ray_scale_alpha_to_anchor_certified q_anchor_n is null");

  double alpha = 1.0;

  for (int block_id = 0; block_id < local.layout.B; ++block_id) {
    const auto& block = local.A_block[static_cast<std::size_t>(block_id)];
    const int offset = local.layout.block_offsets[block_id];
    const double* b_block = rhs.local.block_ptr(block_id);

    for (int i = 0; i < block.rows; ++i) {
      const double* row = block.row_ptr(i);
      double anchor_dot = 0.0;
      double d_dot = 0.0;
      for (int j = 0; j < block.cols; ++j) {
        const double a = row[j];
        const double q_anchor = q_anchor_n[offset + j];
        anchor_dot += a * q_anchor;
        d_dot += a * (q_prop_n[offset + j] - q_anchor);
      }

      const double b_i = b_block[i];
      const double margin = tau_abs(b_i, tau_abs_scale);
      const double numer = b_i - anchor_dot - margin;
      if (numer <= 0.0) {
        return 0.0;
      }
      if (d_dot > 0.0) {
        alpha = std::min(alpha, numer / d_dot);
      }
    }
  }

  const double* b_global = rhs.global.data();
  for (int g = 0; g < global.m_global; ++g) {
    const double* row = global.row_ptr(g);
    double anchor_dot = 0.0;
    double d_dot = 0.0;
    for (int j = 0; j < global.n; ++j) {
      const double a = row[j];
      const double q_anchor = q_anchor_n[j];
      anchor_dot += a * q_anchor;
      d_dot += a * (q_prop_n[j] - q_anchor);
    }

    const double b_i = b_global[g];
    const double margin = tau_abs(b_i, tau_abs_scale);
    const double numer = b_i - anchor_dot - margin;
    if (numer <= 0.0) {
      return 0.0;
    }
    if (d_dot > 0.0) {
      alpha = std::min(alpha, numer / d_dot);
    }
  }

  alpha = std::min(1.0, std::max(0.0, alpha));
  if (alpha > 0.0) {
    alpha = std::nextafter(alpha, 0.0);
  }
  return alpha;
}

void safe_fallback_ray_scale_to_anchor(const ConstraintsLocal& local,
                                       const ConstraintsGlobal& global,
                                       const RHSAll& rhs,
                                       const double* q_prop_n,
                                       const double* q_anchor_n,
                                       double tau_abs_scale,
                                       double* q_out_n,
                                       double* alpha_out,
                                       int max_bisect_iters) {
  SPQS_CHECK(q_out_n != nullptr, "safe_fallback_ray_scale_to_anchor q_out_n is null");

  const int n = local.layout.n;
  const double alpha = ray_scale_alpha_to_anchor_certified(
      local, global, rhs, q_prop_n, q_anchor_n, tau_abs_scale, max_bisect_iters);

  for (int j = 0; j < n; ++j) {
    const double delta = q_prop_n[j] - q_anchor_n[j];
    q_out_n[j] = q_anchor_n[j] + alpha * delta;
  }

  BruteForceScan scan(&local, &global, tau_abs_scale);
  if (!scan.certified_feasible(q_out_n, rhs)) {
    std::copy(q_anchor_n, q_anchor_n + n, q_out_n);
    if (alpha_out != nullptr) {
      *alpha_out = 0.0;
    }
    return;
  }

  if (alpha_out != nullptr) {
    *alpha_out = alpha;
  }
}

}  // namespace spqs
