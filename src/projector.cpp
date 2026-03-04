#include "spqs/projector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "spqs/assert.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/chol_rank1.hpp"
#include "spqs/fp_cert.hpp"
#include "spqs/linalg_small.hpp"
#include "spqs/safefallback.hpp"

namespace spqs {
namespace {

bool contains_id(const std::vector<int>& ids, int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void append_unique(std::vector<int>* ids, int id) {
  if (!contains_id(*ids, id)) {
    ids->push_back(id);
  }
}

void insert_sorted_unique(std::vector<int>* ids, int id) {
  auto it = std::lower_bound(ids->begin(), ids->end(), id);
  if (it == ids->end() || *it != id) {
    ids->insert(it, id);
  }
}

void erase_if_present(std::vector<int>* ids, int id) {
  auto it = std::find(ids->begin(), ids->end(), id);
  if (it != ids->end()) {
    ids->erase(it);
  }
}

int local_block_from_constraint_id(const ConstraintsLocal& local, int constraint_id) {
  if (constraint_id < 0 || constraint_id >= local.total_rows()) {
    return -1;
  }
  return constraint_id / local.m_local_per_block;
}

}  // namespace

StreamingProjector::StreamingProjector(const ConstraintsLocal* local,
                                       const ConstraintsGlobal* global,
                                       ViolatorOracle* oracle,
                                       Arena* arena)
    : local_(local), global_(global), oracle_(oracle), arena_(arena) {
  precompute_strict_interior_bounds();
}

void StreamingProjector::precompute_strict_interior_bounds() {
  local_row_l2_max_per_block_.assign(static_cast<std::size_t>(local_->layout.B), 0.0);
  global_row_l2_max_ = 0.0;

  for (int block_id = 0; block_id < local_->layout.B; ++block_id) {
    const auto& block = local_->A_block[static_cast<std::size_t>(block_id)];
    double max_row_l2 = 0.0;
    for (int i = 0; i < block.rows; ++i) {
      const double* row = block.row_ptr(i);
      double row_l2_sq = 0.0;
      for (int j = 0; j < block.cols; ++j) {
        row_l2_sq += row[j] * row[j];
      }
      max_row_l2 = std::max(max_row_l2, std::sqrt(row_l2_sq));
    }
    local_row_l2_max_per_block_[static_cast<std::size_t>(block_id)] = max_row_l2;
  }

  for (int g = 0; g < global_->m_global; ++g) {
    const double* row = global_->row_ptr(g);
    double row_l2_sq = 0.0;
    for (int j = 0; j < global_->n; ++j) {
      row_l2_sq += row[j] * row[j];
    }
    global_row_l2_max_ = std::max(global_row_l2_max_, std::sqrt(row_l2_sq));
  }
}

void StreamingProjector::set_options(const ProjectorOptions& options) {
  options_ = options;

  const int n = local_->layout.n;
  const int a_cap = std::max(1, options_.a_max);

  q_curr_scratch_.reserve(static_cast<std::size_t>(n));
  q_next_scratch_.reserve(static_cast<std::size_t>(n));
  q_anchor_scratch_.reserve(static_cast<std::size_t>(n));
  q_prev_tick_.reserve(static_cast<std::size_t>(n));

  active_ids_scratch_.reserve(static_cast<std::size_t>(a_cap));
  warm_active_ids_.reserve(static_cast<std::size_t>(a_cap));
  oracle_active_ids_.reserve(static_cast<std::size_t>(a_cap));
  touched_blocks_scratch_.reserve(static_cast<std::size_t>(local_->layout.B));
  active_mask_changed_blocks_scratch_.reserve(static_cast<std::size_t>(local_->layout.B));

  lambda_scratch_.reserve(static_cast<std::size_t>(a_cap));
  refs_scratch_.reserve(static_cast<std::size_t>(a_cap));

  factor_active_ids_.reserve(static_cast<std::size_t>(a_cap));
  factor_refs_.reserve(static_cast<std::size_t>(a_cap));
  factor_l_.reserve(static_cast<std::size_t>(a_cap) * static_cast<std::size_t>(a_cap));
  factor_gram_scratch_.reserve(static_cast<std::size_t>(a_cap) * static_cast<std::size_t>(a_cap));
  rhs_scratch_.reserve(static_cast<std::size_t>(a_cap));
  g_scratch_.reserve(static_cast<std::size_t>(a_cap));
  y_scratch_.reserve(static_cast<std::size_t>(a_cap));

  chol_rank1_prealloc(a_cap + 1);
  have_prev_tick_ = false;
  factor_valid_ = false;
}

void StreamingProjector::set_rhs(const RHSAll& rhs) {
  rhs_ = rhs;
  rhs_set_ = true;
  have_prev_tick_ = false;

  rhs_min_kappa_cached_ = std::numeric_limits<double>::infinity();
  rhs_abs_b_max_cached_ = 0.0;

  for (int block_id = 0; block_id < local_->layout.B; ++block_id) {
    const double* b_block = rhs.local.block_ptr(block_id);
    for (int i = 0; i < local_->m_local_per_block; ++i) {
      const double b_i = b_block[i];
      rhs_min_kappa_cached_ = std::min(rhs_min_kappa_cached_, b_i);
      rhs_abs_b_max_cached_ = std::max(rhs_abs_b_max_cached_, std::abs(b_i));
    }
  }
  for (int g = 0; g < global_->m_global; ++g) {
    const double b_i = rhs.global.b[static_cast<std::size_t>(g)];
    rhs_min_kappa_cached_ = std::min(rhs_min_kappa_cached_, b_i);
    rhs_abs_b_max_cached_ = std::max(rhs_abs_b_max_cached_, std::abs(b_i));
  }

  if (!std::isfinite(rhs_min_kappa_cached_)) {
    rhs_min_kappa_cached_ = 0.0;
  }
  rhs_kappa_valid_cached_ = rhs_min_kappa_cached_ >= options_.kappa_min;
}

bool StreamingProjector::solve_active_system(const std::vector<int>& active_ids,
                                             const double* q_prop_n,
                                             std::vector<double>* lambda_out,
                                             std::vector<ConstraintRef>* refs_out) {
  SPQS_CHECK(lambda_out != nullptr, "solve_active_system lambda_out is null");
  SPQS_CHECK(refs_out != nullptr, "solve_active_system refs_out is null");

  const int k = static_cast<int>(active_ids.size());
  if (k == 0) {
    refs_out->clear();
    lambda_out->clear();
    factor_active_ids_.clear();
    factor_refs_.clear();
    factor_l_.clear();
    factor_valid_ = true;
    return true;
  }

  auto rebuild_from_scratch = [&]() -> bool {
    factor_refs_.clear();
    for (int id : active_ids) {
      ConstraintRef ref;
      if (!decode_constraint_id(*local_, *global_, id, &ref)) {
        return false;
      }
      factor_refs_.push_back(ref);
    }

    factor_gram_scratch_.assign(static_cast<std::size_t>(k) * static_cast<std::size_t>(k), 0.0);
    for (int i = 0; i < k; ++i) {
      const ConstraintRef& ri = factor_refs_[static_cast<std::size_t>(i)];
      for (int j = 0; j <= i; ++j) {
        const ConstraintRef& rj = factor_refs_[static_cast<std::size_t>(j)];
        const double v = dot_constraint_constraint(ri, rj, *local_, *global_);
        factor_gram_scratch_[static_cast<std::size_t>(i) * k + j] = v;
        factor_gram_scratch_[static_cast<std::size_t>(j) * k + i] = v;
      }
    }

    factor_l_.assign(factor_gram_scratch_.begin(), factor_gram_scratch_.end());
    if (!cholesky_factorize_lower(&factor_l_, k)) {
      return false;
    }

    factor_active_ids_ = active_ids;
    factor_valid_ = true;
    return true;
  };

  if (!factor_valid_) {
    if (!rebuild_from_scratch()) {
      return false;
    }
  } else if (factor_active_ids_ == active_ids) {
    // keep current factor
  } else if (static_cast<int>(factor_active_ids_.size()) + 1 == k) {
    bool prefix_match = true;
    for (int i = 0; i < static_cast<int>(factor_active_ids_.size()); ++i) {
      if (factor_active_ids_[static_cast<std::size_t>(i)] != active_ids[static_cast<std::size_t>(i)]) {
        prefix_match = false;
        break;
      }
    }
    if (!prefix_match) {
      if (!rebuild_from_scratch()) {
        return false;
      }
    } else {
      ConstraintRef new_ref;
      if (!decode_constraint_id(*local_, *global_, active_ids.back(), &new_ref)) {
        return false;
      }
      g_scratch_.assign(factor_refs_.size(), 0.0);
      for (int i = 0; i < static_cast<int>(factor_refs_.size()); ++i) {
        g_scratch_[static_cast<std::size_t>(i)] =
            dot_constraint_constraint(factor_refs_[static_cast<std::size_t>(i)], new_ref, *local_, *global_);
      }
      const double diag = dot_constraint_constraint(new_ref, new_ref, *local_, *global_);
      if (!chol_append(&factor_l_,
                       static_cast<int>(factor_refs_.size()),
                       g_scratch_,
                       diag,
                       1e-14)) {
        return false;
      }
      factor_active_ids_.push_back(active_ids.back());
      factor_refs_.push_back(new_ref);
    }
  } else if (static_cast<int>(factor_active_ids_.size()) == k + 1) {
    int remove_pos = -1;
    int i_prev = 0;
    int i_now = 0;
    const int prev_n = static_cast<int>(factor_active_ids_.size());
    while (i_prev < prev_n && i_now < k) {
      if (factor_active_ids_[static_cast<std::size_t>(i_prev)] ==
          active_ids[static_cast<std::size_t>(i_now)]) {
        ++i_prev;
        ++i_now;
        continue;
      }
      if (remove_pos >= 0) {
        remove_pos = -1;
        break;
      }
      remove_pos = i_prev;
      ++i_prev;
    }
    if (remove_pos < 0 && i_prev == prev_n - 1 && i_now == k) {
      remove_pos = prev_n - 1;
    }

    if (remove_pos < 0) {
      if (!rebuild_from_scratch()) {
        return false;
      }
    } else {
      if (!chol_remove(&factor_l_,
                       static_cast<int>(factor_refs_.size()),
                       remove_pos,
                       1e-14)) {
        return false;
      }
      factor_active_ids_.erase(factor_active_ids_.begin() + remove_pos);
      factor_refs_.erase(factor_refs_.begin() + remove_pos);
    }
  } else {
    if (!rebuild_from_scratch()) {
      return false;
    }
  }

  if (static_cast<int>(factor_refs_.size()) != k ||
      static_cast<int>(factor_active_ids_.size()) != k ||
      static_cast<int>(factor_l_.size()) != k * k) {
    factor_valid_ = false;
    return false;
  }

  rhs_scratch_.assign(static_cast<std::size_t>(k), 0.0);
  for (int i = 0; i < k; ++i) {
    const ConstraintRef& ri = factor_refs_[static_cast<std::size_t>(i)];
    rhs_scratch_[static_cast<std::size_t>(i)] =
        dot_constraint_q(ri, *local_, *global_, q_prop_n) -
        constraint_rhs_value(ri, rhs_);
  }

  refs_out->assign(factor_refs_.begin(), factor_refs_.end());
  return cholesky_solve_lower_scratch(
      factor_l_, rhs_scratch_, k, &y_scratch_, lambda_out);
}

void StreamingProjector::apply_active_solution(const std::vector<ConstraintRef>& refs,
                                               const std::vector<double>& lambda,
                                               const double* q_prop_n,
                                               std::vector<double>* q_out) {
  SPQS_CHECK(q_out != nullptr, "apply_active_solution q_out is null");
  SPQS_CHECK(refs.size() == lambda.size(), "apply_active_solution size mismatch");

  const int n = local_->layout.n;
  q_out->assign(q_prop_n, q_prop_n + n);

  for (std::size_t i = 0; i < refs.size(); ++i) {
    add_scaled_constraint_to_q(refs[i], *local_, *global_, -lambda[i], q_out->data());
  }
}

void StreamingProjector::sync_oracle_active_set(const std::vector<int>& desired_active_ids) {
  for (int id : oracle_active_ids_) {
    if (!contains_id(desired_active_ids, id)) {
      oracle_->deactivate_constraint_id(id);
    }
  }
  for (int id : desired_active_ids) {
    if (!contains_id(oracle_active_ids_, id)) {
      oracle_->activate_constraint_id(id);
    }
  }
  oracle_active_ids_ = desired_active_ids;
}

void StreamingProjector::changed_blocks(const std::vector<double>& q_prev,
                                        const std::vector<double>& q_next,
                                        std::vector<int>* out_blocks) const {
  SPQS_CHECK(out_blocks != nullptr, "changed_blocks out_blocks is null");
  out_blocks->clear();

  for (int r = 0; r < local_->layout.B; ++r) {
    const int offset = local_->layout.block_offsets[r];
    const int len = local_->layout.block_sizes[r];

    bool changed = false;
    for (int j = 0; j < len; ++j) {
      if (std::abs(q_prev[static_cast<std::size_t>(offset + j)] -
                   q_next[static_cast<std::size_t>(offset + j)]) > options_.q_change_tol) {
        changed = true;
        break;
      }
    }
    if (changed) {
      out_blocks->push_back(r);
    }
  }
}

void StreamingProjector::fallback_to_anchor(const double* q_prop_n,
                                            SolverStats* stats,
                                            double* q_out_n) {
  SPQS_CHECK(stats != nullptr, "fallback_to_anchor stats is null");
  SPQS_CHECK(q_out_n != nullptr, "fallback_to_anchor q_out_n is null");

  const int n = local_->layout.n;
  q_anchor_scratch_.assign(static_cast<std::size_t>(n), 0.0);

  double alpha = 0.0;
  safe_fallback_ray_scale_to_anchor(*local_,
                                    *global_,
                                    rhs_,
                                    q_prop_n,
                                    q_anchor_scratch_.data(),
                                    options_.tau_abs_scale,
                                    q_out_n,
                                    &alpha,
                                    options_.fallback_bisect_iters);

  stats->fallback_used = true;
  stats->fallback_alpha = alpha;
  stats->tau_shrink_used = 0.0;
  stats->active_size_final = 0;
  warm_active_ids_.clear();
  for (int id : oracle_active_ids_) {
    oracle_->deactivate_constraint_id(id);
  }
  oracle_active_ids_.clear();
  factor_active_ids_.clear();
  factor_refs_.clear();
  factor_l_.clear();
  factor_valid_ = true;

  BruteForceScan scan(local_, global_, options_.tau_abs_scale);
  stats->max_violation_certified = scan.max_violation(q_out_n, rhs_).violation;
  stats->min_slack_certified = scan.min_certified_slack(q_out_n, rhs_);

  if (!scan.certified_feasible(q_out_n, rhs_)) {
    std::fill(q_out_n, q_out_n + n, 0.0);
    stats->fallback_alpha = 0.0;
    stats->max_violation_certified = scan.max_violation(q_out_n, rhs_).violation;
    stats->min_slack_certified = scan.min_certified_slack(q_out_n, rhs_);
  }
}

bool StreamingProjector::finalize_with_safety(const std::vector<double>& q_candidate,
                                              const double* q_prop_n,
                                              SolverStats* stats,
                                              double* q_out_n) {
  SPQS_CHECK(stats != nullptr, "finalize_with_safety stats is null");
  SPQS_CHECK(q_out_n != nullptr, "finalize_with_safety q_out_n is null");

  const int n = local_->layout.n;
  q_anchor_scratch_.assign(static_cast<std::size_t>(n), 0.0);

  stats->kappa_valid = rhs_kappa_valid_cached_;
  stats->min_kappa = rhs_min_kappa_cached_;

  if (options_.strict_interior && !stats->kappa_valid) {
    if (options_.fallback_enabled) {
      fallback_to_anchor(q_prop_n, stats, q_out_n);
      return false;
    }
    return false;
  }

  double q_l2_global_sq = 0.0;
  for (int j = 0; j < n; ++j) {
    const double qj = q_candidate[static_cast<std::size_t>(j)];
    q_l2_global_sq += qj * qj;
  }
  const double q_l2_global = std::sqrt(q_l2_global_sq);

  double local_dot_error_bound = 0.0;
  for (int block_id = 0; block_id < local_->layout.B; ++block_id) {
    const int offset = local_->layout.block_offsets[block_id];
    const int len = local_->layout.block_sizes[block_id];
    double q_l2_block_sq = 0.0;
    for (int j = 0; j < len; ++j) {
      const double qj = q_candidate[static_cast<std::size_t>(offset + j)];
      q_l2_block_sq += qj * qj;
    }
    const double q_l2_block = std::sqrt(q_l2_block_sq);
    const double coeff = gamma_s(len) * local_row_l2_max_per_block_[static_cast<std::size_t>(block_id)];
    local_dot_error_bound = std::max(local_dot_error_bound, coeff * q_l2_block);
  }

  const double global_dot_error_bound = gamma_s(global_->n) * global_row_l2_max_ * q_l2_global;
  const double dot_error_bound = std::max(local_dot_error_bound, global_dot_error_bound);
  const double tau_margin = tau_abs(rhs_abs_b_max_cached_, options_.tau_abs_scale);
  const double kappa_floor = std::max(rhs_min_kappa_cached_, options_.kappa_min);
  double tau_required_bound = std::numeric_limits<double>::infinity();
  if (kappa_floor > 0.0) {
    tau_required_bound = (dot_error_bound + tau_margin) / kappa_floor;
  }
  if (!std::isfinite(tau_required_bound)) {
    tau_required_bound = std::numeric_limits<double>::infinity();
  }
  stats->tau_required = tau_required_bound;

  double tau_use = 0.0;
  if (options_.strict_interior) {
    if (tau_required_bound > options_.tau_shrink_max) {
      if (options_.fallback_enabled) {
        fallback_to_anchor(q_prop_n, stats, q_out_n);
        return false;
      }
      return false;
    }

    tau_use = std::max(tau_required_bound, options_.tau_shrink_min);
    if (tau_use <= 0.0) {
      tau_use = std::min(options_.tau_shrink_max, options_.strict_tau_floor);
    }
    if (tau_use > options_.tau_shrink_max) {
      if (options_.fallback_enabled) {
        fallback_to_anchor(q_prop_n, stats, q_out_n);
        return false;
      }
      return false;
    }
  }

  for (int j = 0; j < n; ++j) {
    q_out_n[j] = (1.0 - tau_use) * q_candidate[static_cast<std::size_t>(j)] +
                 tau_use * q_anchor_scratch_[static_cast<std::size_t>(j)];
  }

  double worst_active_violation = -std::numeric_limits<double>::infinity();
  double min_active_slack = std::numeric_limits<double>::infinity();
  for (const ConstraintRef& ref : factor_refs_) {
    const double b_i = constraint_rhs_value(ref, rhs_);
    if (ref.scope == ConstraintScope::LOCAL) {
      const auto& block = local_->A_block[static_cast<std::size_t>(ref.block_id)];
      const int offset = local_->layout.block_offsets[ref.block_id];
      const CertifiedDot cd =
          dot_certified_fma(block.row_ptr(ref.row_in_block), q_out_n + offset, block.cols);
      const double v = certified_violation_from_dot(
          cd.s_hat, cd.t_abs, block.cols, b_i, options_.tau_abs_scale);
      const double s = certified_slack_from_dot(
          cd.s_hat, cd.t_abs, block.cols, b_i, options_.tau_abs_scale);
      worst_active_violation = std::max(worst_active_violation, v);
      min_active_slack = std::min(min_active_slack, s);
      continue;
    }

    const CertifiedDot cd =
        dot_certified_fma(global_->row_ptr(ref.global_row), q_out_n, global_->n);
    const double v = certified_violation_from_dot(
        cd.s_hat, cd.t_abs, global_->n, b_i, options_.tau_abs_scale);
    const double s = certified_slack_from_dot(
        cd.s_hat, cd.t_abs, global_->n, b_i, options_.tau_abs_scale);
    worst_active_violation = std::max(worst_active_violation, v);
    min_active_slack = std::min(min_active_slack, s);
  }
  if (factor_refs_.empty()) {
    worst_active_violation = -std::numeric_limits<double>::infinity();
    min_active_slack = options_.strict_interior ? options_.kappa_min : 0.0;
  }

  if (worst_active_violation > 0.0) {
    if (options_.fallback_enabled) {
      fallback_to_anchor(q_prop_n, stats, q_out_n);
      return false;
    }
    return false;
  }

  stats->tau_shrink_used = tau_use;
  stats->fallback_alpha = 1.0;
  stats->max_violation_certified = worst_active_violation;
  stats->min_slack_certified = min_active_slack;

  if (options_.strict_interior && !(stats->min_slack_certified > 0.0)) {
    if (options_.fallback_enabled) {
      fallback_to_anchor(q_prop_n, stats, q_out_n);
      return false;
    }
    return false;
  }

  return true;
}

SolverStats StreamingProjector::project(const double* q_prop_n, double* q_out_n) {
  SPQS_CHECK(q_prop_n != nullptr, "project received null q_prop_n");
  SPQS_CHECK(q_out_n != nullptr, "project received null q_out_n");
  SPQS_CHECK(local_ != nullptr, "StreamingProjector local_ is null");
  SPQS_CHECK(global_ != nullptr, "StreamingProjector global_ is null");
  SPQS_CHECK(oracle_ != nullptr, "StreamingProjector oracle_ is null");
  SPQS_CHECK(arena_ != nullptr, "StreamingProjector arena_ is null");
  SPQS_CHECK(rhs_set_, "StreamingProjector set_rhs must be called before project");

  const int n = local_->layout.n;

  q_curr_scratch_.assign(q_prop_n, q_prop_n + n);
  q_next_scratch_.clear();
  active_ids_scratch_.clear();
  auto& q_curr = q_curr_scratch_;
  auto& q_next = q_next_scratch_;
  auto& active_ids = active_ids_scratch_;

  if (options_.warm_start) {
    for (int id : warm_active_ids_) {
      if (!contains_id(active_ids, id)) {
        active_ids.push_back(id);
      }
    }
  }

  SolverStats stats;

  if (!active_ids.empty()) {
    lambda_scratch_.clear();
    refs_scratch_.clear();
    if (solve_active_system(active_ids, q_prop_n, &lambda_scratch_, &refs_scratch_)) {
      apply_active_solution(refs_scratch_, lambda_scratch_, q_prop_n, &q_curr);
    } else {
      active_ids.clear();
    }
  }
  if (active_ids.empty()) {
    factor_active_ids_.clear();
    factor_refs_.clear();
    factor_l_.clear();
    factor_valid_ = true;
  }

  sync_oracle_active_set(active_ids);

  if (options_.force_full_rescan || !have_prev_tick_) {
    oracle_->init_tick(q_curr.data(), rhs_);
    touched_blocks_scratch_.clear();
    touched_blocks_scratch_.resize(static_cast<std::size_t>(local_->layout.B));
    for (int r = 0; r < local_->layout.B; ++r) {
      touched_blocks_scratch_[static_cast<std::size_t>(r)] = r;
    }
  } else {
    changed_blocks(q_prev_tick_, q_curr, &touched_blocks_scratch_);
    oracle_->update_blocks(touched_blocks_scratch_.data(),
                           static_cast<int>(touched_blocks_scratch_.size()),
                           q_curr.data(),
                           rhs_);
  }

  q_prev_tick_.assign(q_curr.begin(), q_curr.end());
  have_prev_tick_ = true;
  stats.tick_changed_blocks_count = static_cast<int>(touched_blocks_scratch_.size());
  stats.tick_local_blocks_recomputed = oracle_->last_stats().local_blocks_recomputed;
  stats.max_violation_certified = oracle_->max_violation_inactive().violation;

  for (int iter = 0; iter < options_.I_max; ++iter) {
    const Violator worst_any = oracle_->max_violation_inactive();
    stats.max_violation_certified = worst_any.violation;

    if (worst_any.violation <= 0.0) {
      if (!active_ids.empty()) {
        lambda_scratch_.clear();
        refs_scratch_.clear();
        if (!solve_active_system(active_ids, q_prop_n, &lambda_scratch_, &refs_scratch_)) {
          stats.fallback_used = true;
          break;
        }

        int remove_pos = -1;
        double most_negative = -options_.lambda_negative_tol;
        for (int i = 0; i < static_cast<int>(lambda_scratch_.size()); ++i) {
          if (lambda_scratch_[static_cast<std::size_t>(i)] < most_negative) {
            most_negative = lambda_scratch_[static_cast<std::size_t>(i)];
            remove_pos = i;
          }
        }

        if (remove_pos >= 0) {
          const int removed_id = active_ids[static_cast<std::size_t>(remove_pos)];
          active_ids.erase(active_ids.begin() + remove_pos);
          ++stats.removes;
          if (active_ids.empty()) {
            factor_active_ids_.clear();
            factor_refs_.clear();
            factor_l_.clear();
            factor_valid_ = true;
          }

          active_mask_changed_blocks_scratch_.clear();
          auto& active_mask_changed_blocks = active_mask_changed_blocks_scratch_;
          const int removed_local_block = local_block_from_constraint_id(*local_, removed_id);
          if (removed_local_block >= 0) {
            insert_sorted_unique(&active_mask_changed_blocks, removed_local_block);
          }
          oracle_->deactivate_constraint_id(removed_id);
          erase_if_present(&oracle_active_ids_, removed_id);
          oracle_->update_blocks(active_mask_changed_blocks.data(),
                                 static_cast<int>(active_mask_changed_blocks.size()),
                                 q_curr.data(),
                                 rhs_);
          continue;
        }
      }

      stats.iters = iter;
      stats.active_size_final = static_cast<int>(active_ids.size());
      if (finalize_with_safety(q_curr, q_prop_n, &stats, q_out_n)) {
        warm_active_ids_ = active_ids;
      }
      return stats;
    }

    const Violator entering = worst_any;

    if (entering.violation <= 0.0) {
      stats.iters = iter;
      stats.active_size_final = static_cast<int>(active_ids.size());
      if (finalize_with_safety(q_curr, q_prop_n, &stats, q_out_n)) {
        warm_active_ids_ = active_ids;
      }
      return stats;
    }

    if (contains_id(active_ids, entering.constraint_id)) {
      stats.fallback_used = true;
      break;
    }
    if (static_cast<int>(active_ids.size()) >= options_.a_max) {
      stats.fallback_used = true;
      break;
    }
    append_unique(&active_ids, entering.constraint_id);
    ++stats.adds;

    active_mask_changed_blocks_scratch_.clear();
    auto& active_mask_changed_blocks = active_mask_changed_blocks_scratch_;
    const int entering_local_block = local_block_from_constraint_id(*local_, entering.constraint_id);
    if (entering_local_block >= 0) {
      insert_sorted_unique(&active_mask_changed_blocks, entering_local_block);
    }
    oracle_->activate_constraint_id(entering.constraint_id);
    append_unique(&oracle_active_ids_, entering.constraint_id);

    lambda_scratch_.clear();
    refs_scratch_.clear();
    if (!solve_active_system(active_ids, q_prop_n, &lambda_scratch_, &refs_scratch_)) {
      stats.fallback_used = true;
      break;
    }

    int remove_pos = -1;
    double most_negative = -options_.lambda_negative_tol;
    for (int i = 0; i < static_cast<int>(lambda_scratch_.size()); ++i) {
      if (lambda_scratch_[static_cast<std::size_t>(i)] < most_negative) {
        most_negative = lambda_scratch_[static_cast<std::size_t>(i)];
        remove_pos = i;
      }
    }

    if (remove_pos >= 0) {
      const int removed_id = active_ids[static_cast<std::size_t>(remove_pos)];
      active_ids.erase(active_ids.begin() + remove_pos);
      ++stats.removes;
      if (active_ids.empty()) {
        factor_active_ids_.clear();
        factor_refs_.clear();
        factor_l_.clear();
        factor_valid_ = true;
      }

      const int removed_local_block = local_block_from_constraint_id(*local_, removed_id);
      if (removed_local_block >= 0) {
        insert_sorted_unique(&active_mask_changed_blocks, removed_local_block);
      }
      oracle_->deactivate_constraint_id(removed_id);
      erase_if_present(&oracle_active_ids_, removed_id);

      oracle_->update_blocks(active_mask_changed_blocks.data(),
                             static_cast<int>(active_mask_changed_blocks.size()),
                             q_curr.data(),
                             rhs_);
      continue;
    }

    apply_active_solution(refs_scratch_, lambda_scratch_, q_prop_n, &q_next);

    touched_blocks_scratch_.clear();
    auto& touched_blocks = touched_blocks_scratch_;
    if (options_.force_full_rescan) {
      touched_blocks.resize(static_cast<std::size_t>(local_->layout.B));
      for (int r = 0; r < local_->layout.B; ++r) {
        touched_blocks[static_cast<std::size_t>(r)] = r;
      }
    } else {
      changed_blocks(q_curr, q_next, &touched_blocks);
      for (int block_id : active_mask_changed_blocks) {
        insert_sorted_unique(&touched_blocks, block_id);
      }
    }
    if (static_cast<int>(touched_blocks.size()) > stats.touched_blocks_per_iter_max) {
      stats.touched_blocks_per_iter_max = static_cast<int>(touched_blocks.size());
    }

    q_curr.swap(q_next);
    oracle_->update_blocks(touched_blocks.data(),
                           static_cast<int>(touched_blocks.size()),
                           q_curr.data(),
                           rhs_);

    stats.iters = iter + 1;
  }

  fallback_to_anchor(q_prop_n, &stats, q_out_n);
  return stats;
}

}  // namespace spqs
