#pragma once

#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"

namespace spqs {

struct OsqpBaselineParams {
  double eps_abs = 1e-12;
  double eps_rel = 1e-12;
  int max_iter = 200000;
  bool polish = true;
};

struct BaselineSolveResult {
  bool available = false;
  bool success = false;
  int iterations = 0;
  double objective = 0.0;
  const char* backend = "NONE";
  const char* detail = "";
};

double objective_half_l2_sq(const double* q,
                            const double* q_prop,
                            int n);

BaselineSolveResult solve_osqp_projection_baseline(
    const ConstraintsLocal& local,
    const ConstraintsGlobal& global,
    const RHSAll& rhs,
    const double* q_prop,
    const OsqpBaselineParams& params,
    double* q_out);

BaselineSolveResult solve_rayscale_baseline(
    const ConstraintsLocal& local,
    const ConstraintsGlobal& global,
    const RHSAll& rhs,
    const double* q_prop,
    double tau_abs_scale,
    double* q_out);

}  // namespace spqs
