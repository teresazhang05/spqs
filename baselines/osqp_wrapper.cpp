#include "spqs/baselines.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "spqs/safefallback.hpp"

#if SPQS_ENABLE_OSQP
#include <osqp/osqp.h>
#endif

namespace spqs {

double objective_half_l2_sq(const double* q,
                            const double* q_prop,
                            int n) {
  double s = 0.0;
  for (int j = 0; j < n; ++j) {
    const double d = q[j] - q_prop[j];
    s += d * d;
  }
  return 0.5 * s;
}

BaselineSolveResult solve_osqp_projection_baseline(
    const ConstraintsLocal& local,
    const ConstraintsGlobal& global,
    const RHSAll& rhs,
    const double* q_prop,
    const OsqpBaselineParams& params,
    double* q_out) {
  BaselineSolveResult out;
  out.backend = "OSQP";
  out.available = true;
  out.success = false;
  out.detail = "";
  out.iterations = 0;

#if !SPQS_ENABLE_OSQP
  (void)global;
  (void)rhs;
  (void)params;
  out.available = false;
  out.detail = "OSQP backend unavailable in this build";
  if (q_out != nullptr && q_prop != nullptr) {
    std::copy(q_prop, q_prop + local.layout.n, q_out);
  }
  return out;
#else
  if (q_prop == nullptr || q_out == nullptr) {
    out.success = false;
    out.detail = "null pointer input";
    return out;
  }

  const int n_i = local.layout.n;
  const int m_local_i = local.total_rows();
  const int m_i = m_local_i + global.m_global;
  if (n_i <= 0 || m_i <= 0) {
    std::copy(q_prop, q_prop + n_i, q_out);
    out.success = false;
    out.detail = "invalid dimensions";
    return out;
  }

  const OSQPInt n = static_cast<OSQPInt>(n_i);
  const OSQPInt m = static_cast<OSQPInt>(m_i);

  std::vector<OSQPFloat> q(static_cast<std::size_t>(n));
  for (OSQPInt j = 0; j < n; ++j) {
    q[static_cast<std::size_t>(j)] = static_cast<OSQPFloat>(-q_prop[static_cast<std::size_t>(j)]);
  }

  std::vector<OSQPFloat> l(static_cast<std::size_t>(m), static_cast<OSQPFloat>(-OSQP_INFTY));
  std::vector<OSQPFloat> u(static_cast<std::size_t>(m));
  for (int block_id = 0; block_id < local.layout.B; ++block_id) {
    const double* b_block = rhs.local.block_ptr(block_id);
    for (int i = 0; i < local.m_local_per_block; ++i) {
      const int row_id = local.constraint_id(block_id, i);
      u[static_cast<std::size_t>(row_id)] = static_cast<OSQPFloat>(b_block[i]);
    }
  }
  for (int g = 0; g < global.m_global; ++g) {
    const int row_id = m_local_i + g;
    u[static_cast<std::size_t>(row_id)] = static_cast<OSQPFloat>(rhs.global.b[static_cast<std::size_t>(g)]);
  }

  std::vector<OSQPFloat> p_x(static_cast<std::size_t>(n), static_cast<OSQPFloat>(1.0));
  std::vector<OSQPInt> p_i(static_cast<std::size_t>(n));
  std::vector<OSQPInt> p_p(static_cast<std::size_t>(n) + 1U);
  p_p[0] = static_cast<OSQPInt>(0);
  for (OSQPInt j = 0; j < n; ++j) {
    p_i[static_cast<std::size_t>(j)] = j;
    p_p[static_cast<std::size_t>(j) + 1U] = j + 1;
  }

  std::vector<OSQPInt> a_col_counts(static_cast<std::size_t>(n), static_cast<OSQPInt>(global.m_global));
  for (int block_id = 0; block_id < local.layout.B; ++block_id) {
    const auto& block = local.A_block[static_cast<std::size_t>(block_id)];
    const int offset = local.layout.block_offsets[block_id];
    for (int j = 0; j < block.cols; ++j) {
      a_col_counts[static_cast<std::size_t>(offset + j)] += static_cast<OSQPInt>(block.rows);
    }
  }

  std::vector<OSQPInt> a_p(static_cast<std::size_t>(n) + 1U);
  a_p[0] = static_cast<OSQPInt>(0);
  for (OSQPInt j = 0; j < n; ++j) {
    a_p[static_cast<std::size_t>(j) + 1U] =
        a_p[static_cast<std::size_t>(j)] + a_col_counts[static_cast<std::size_t>(j)];
  }
  const OSQPInt a_nnz = a_p[static_cast<std::size_t>(n)];
  std::vector<OSQPInt> a_i(static_cast<std::size_t>(a_nnz));
  std::vector<OSQPFloat> a_x(static_cast<std::size_t>(a_nnz));
  std::vector<OSQPInt> a_next = a_p;

  for (int block_id = 0; block_id < local.layout.B; ++block_id) {
    const auto& block = local.A_block[static_cast<std::size_t>(block_id)];
    const int offset = local.layout.block_offsets[block_id];
    for (int i = 0; i < block.rows; ++i) {
      const OSQPInt row_id = static_cast<OSQPInt>(local.constraint_id(block_id, i));
      const double* row = block.row_ptr(i);
      for (int j = 0; j < block.cols; ++j) {
        const int col = offset + j;
        const std::size_t write_pos = static_cast<std::size_t>(a_next[static_cast<std::size_t>(col)]++);
        a_i[write_pos] = row_id;
        a_x[write_pos] = static_cast<OSQPFloat>(row[j]);
      }
    }
  }

  for (int g = 0; g < global.m_global; ++g) {
    const OSQPInt row_id = static_cast<OSQPInt>(m_local_i + g);
    const double* row = global.row_ptr(g);
    for (int col = 0; col < n_i; ++col) {
      const std::size_t write_pos = static_cast<std::size_t>(a_next[static_cast<std::size_t>(col)]++);
      a_i[write_pos] = row_id;
      a_x[write_pos] = static_cast<OSQPFloat>(row[col]);
    }
  }

  OSQPCscMatrix p_mat {};
  OSQPCscMatrix_set_data(&p_mat, n, n, n, p_x.data(), p_i.data(), p_p.data());
  OSQPCscMatrix a_mat {};
  OSQPCscMatrix_set_data(&a_mat, m, n, a_nnz, a_x.data(), a_i.data(), a_p.data());

  OSQPSettings settings {};
  osqp_set_default_settings(&settings);
  settings.verbose = static_cast<OSQPInt>(0);
  settings.eps_abs = static_cast<OSQPFloat>(params.eps_abs);
  settings.eps_rel = static_cast<OSQPFloat>(params.eps_rel);
  settings.max_iter = static_cast<OSQPInt>(params.max_iter);
  settings.polishing = params.polish ? static_cast<OSQPInt>(1) : static_cast<OSQPInt>(0);
  settings.warm_starting = static_cast<OSQPInt>(0);

  OSQPSolver* solver = nullptr;
  const OSQPInt setup_status =
      osqp_setup(&solver, &p_mat, q.data(), &a_mat, l.data(), u.data(), m, n, &settings);
  if (setup_status != 0 || solver == nullptr) {
    std::copy(q_prop, q_prop + n_i, q_out);
    out.success = false;
    out.detail = "osqp_setup_failed";
    return out;
  }

  const OSQPInt solve_status = osqp_solve(solver);
  if (solve_status != 0) {
    std::copy(q_prop, q_prop + n_i, q_out);
    out.success = false;
    out.detail = osqp_error_message(solve_status);
    (void)osqp_cleanup(solver);
    return out;
  }

  const OSQPInt status = solver->info ? solver->info->status_val : static_cast<OSQPInt>(OSQP_UNSOLVED);
  out.iterations = solver->info ? static_cast<int>(solver->info->iter) : 0;

  if ((status == static_cast<OSQPInt>(OSQP_SOLVED) ||
       status == static_cast<OSQPInt>(OSQP_SOLVED_INACCURATE)) &&
      solver->solution != nullptr &&
      solver->solution->x != nullptr) {
    for (int j = 0; j < n_i; ++j) {
      q_out[j] = static_cast<double>(solver->solution->x[j]);
    }
    out.success = true;
    out.objective = objective_half_l2_sq(q_out, q_prop, n_i);
  } else {
    std::copy(q_prop, q_prop + n_i, q_out);
    out.success = false;
    out.detail = "osqp_not_solved";
  }

  (void)osqp_cleanup(solver);
  return out;
#endif
}

BaselineSolveResult solve_rayscale_baseline(
    const ConstraintsLocal& local,
    const ConstraintsGlobal& global,
    const RHSAll& rhs,
    const double* q_prop,
    double tau_abs_scale,
    double* q_out) {
  BaselineSolveResult out;
  out.backend = "RAYSCALE";
  out.available = true;
  out.success = false;
  out.detail = "";

  if (q_prop == nullptr || q_out == nullptr) {
    out.detail = "null pointer input";
    return out;
  }

  std::vector<double> anchor(static_cast<std::size_t>(local.layout.n), 0.0);
  safe_fallback_ray_scale_to_anchor(
      local, global, rhs, q_prop, anchor.data(), tau_abs_scale, q_out, nullptr);

  out.success = true;
  out.iterations = 0;
  out.objective = objective_half_l2_sq(q_out, q_prop, local.layout.n);
  return out;
}

}  // namespace spqs
