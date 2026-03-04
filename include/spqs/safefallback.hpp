#pragma once

#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"

namespace spqs {

struct InteriorizationReport {
  bool kappa_valid = true;
  double min_kappa = 0.0;
  double tau_required = 0.0;
};

bool compute_tau_required_to_anchor_certified(const ConstraintsLocal& local,
                                              const ConstraintsGlobal& global,
                                              const RHSAll& rhs,
                                              const double* q_n,
                                              const double* q_anchor_n,
                                              double tau_abs_scale,
                                              double kappa_min,
                                              InteriorizationReport* report);

double ray_scale_alpha_to_anchor_certified(const ConstraintsLocal& local,
                                           const ConstraintsGlobal& global,
                                           const RHSAll& rhs,
                                           const double* q_prop_n,
                                           const double* q_anchor_n,
                                           double tau_abs_scale,
                                           int max_bisect_iters = 64);

void safe_fallback_ray_scale_to_anchor(const ConstraintsLocal& local,
                                       const ConstraintsGlobal& global,
                                       const RHSAll& rhs,
                                       const double* q_prop_n,
                                       const double* q_anchor_n,
                                       double tau_abs_scale,
                                       double* q_out_n,
                                       double* alpha_out = nullptr,
                                       int max_bisect_iters = 64);

}  // namespace spqs
