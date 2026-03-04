#pragma once

#include <vector>

#include "spqs/active_set.hpp"
#include "spqs/arena.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_oracle.hpp"

namespace spqs {

struct SolverStats {
  int iters = 0;
  int adds = 0;
  int removes = 0;
  int active_size_final = 0;
  int touched_blocks_per_iter_max = 0;
  int tick_changed_blocks_count = 0;
  int tick_local_blocks_recomputed = 0;
  bool fallback_used = false;
  bool kappa_valid = true;
  double min_kappa = 0.0;
  double tau_required = 0.0;
  double tau_shrink_used = 0.0;
  double fallback_alpha = 1.0;
  double max_violation_certified = 0.0;
  double min_slack_certified = 0.0;
};

struct ProjectorOptions {
  int a_max = 96;
  int I_max = 200;
  bool warm_start = true;
  bool bland_rule = true;
  bool force_full_rescan = false;
  double tau_abs_scale = 8.0;
  bool strict_interior = true;
  double kappa_min = 1e-6;
  double tau_shrink_min = 0.0;
  double tau_shrink_max = 1e-3;
  bool fallback_enabled = true;
  int fallback_bisect_iters = 64;
  double strict_tau_floor = 1e-12;
  double lambda_negative_tol = 1e-12;
  double q_change_tol = 1e-15;
};

class StreamingProjector {
 public:
  StreamingProjector(const ConstraintsLocal* local,
                     const ConstraintsGlobal* global,
                     ViolatorOracle* oracle,
                     Arena* arena);

  void set_options(const ProjectorOptions& options);
  void set_rhs(const RHSAll& rhs);
  SolverStats project(const double* q_prop_n, double* q_out_n);

 private:
  void precompute_strict_interior_bounds();
  bool solve_active_system(const std::vector<int>& active_ids,
                           const double* q_prop_n,
                           std::vector<double>* lambda_out,
                           std::vector<ConstraintRef>* refs_out);
  void apply_active_solution(const std::vector<ConstraintRef>& refs,
                             const std::vector<double>& lambda,
                             const double* q_prop_n,
                             std::vector<double>* q_out);
  void sync_oracle_active_set(const std::vector<int>& desired_active_ids);
  void changed_blocks(const std::vector<double>& q_prev,
                      const std::vector<double>& q_next,
                      std::vector<int>* out_blocks) const;
  bool finalize_with_safety(const std::vector<double>& q_candidate,
                            const double* q_prop_n,
                            SolverStats* stats,
                            double* q_out_n);
  void fallback_to_anchor(const double* q_prop_n,
                          SolverStats* stats,
                          double* q_out_n);

  const ConstraintsLocal* local_;
  const ConstraintsGlobal* global_;
  ViolatorOracle* oracle_;
  Arena* arena_;
  ProjectorOptions options_;
  bool rhs_set_ = false;
  RHSAll rhs_;
  std::vector<int> warm_active_ids_;

  // Persistent active-set factor state reused across ticks.
  std::vector<int> factor_active_ids_;
  std::vector<ConstraintRef> factor_refs_;
  std::vector<double> factor_l_;
  bool factor_valid_ = false;

  // Scratch buffers reserved up to configured caps to keep project() allocation-free.
  std::vector<double> factor_gram_scratch_;
  std::vector<double> rhs_scratch_;
  std::vector<double> g_scratch_;
  std::vector<double> y_scratch_;
  std::vector<double> q_curr_scratch_;
  std::vector<double> q_next_scratch_;
  std::vector<double> q_anchor_scratch_;
  std::vector<int> active_ids_scratch_;
  std::vector<int> touched_blocks_scratch_;
  std::vector<int> active_mask_changed_blocks_scratch_;
  std::vector<double> lambda_scratch_;
  std::vector<ConstraintRef> refs_scratch_;

  // Strict-interior bound caches used to avoid per-tick full-row scans.
  std::vector<double> local_row_l2_max_per_block_;
  double global_row_l2_max_ = 0.0;
  double rhs_min_kappa_cached_ = 0.0;
  double rhs_abs_b_max_cached_ = 0.0;
  bool rhs_kappa_valid_cached_ = true;

  // Cross-tick streaming state.
  std::vector<double> q_prev_tick_;
  bool have_prev_tick_ = false;

  // Oracle-active ids are tracked incrementally to avoid O(m) mask rebuilds.
  std::vector<int> oracle_active_ids_;
};

}  // namespace spqs
