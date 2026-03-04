#include "spqs/violator_oracle.hpp"

#include <algorithm>
#include <limits>

#include "spqs/assert.hpp"
#include "spqs/fp_cert.hpp"

namespace spqs {
namespace {

int ceil_pow2(int x) {
  int p = 1;
  while (p < x) {
    p <<= 1;
  }
  return p;
}

Violator negative_infinity_local() {
  Violator v;
  v.scope = ConstraintScope::LOCAL;
  v.block_id = -1;
  v.row_in_block = -1;
  v.global_row = -1;
  v.constraint_id = std::numeric_limits<int>::max();
  v.violation = -std::numeric_limits<double>::infinity();
  return v;
}

Violator negative_infinity_global() {
  Violator v;
  v.scope = ConstraintScope::GLOBAL;
  v.block_id = -1;
  v.row_in_block = -1;
  v.global_row = -1;
  v.constraint_id = std::numeric_limits<int>::max();
  v.violation = -std::numeric_limits<double>::infinity();
  return v;
}

}  // namespace

ViolatorOracle::ViolatorOracle(const ConstraintsLocal* local,
                               const ConstraintsGlobal* global,
                               double tau_abs_scale)
    : local_(local), global_(global), tau_abs_scale_(tau_abs_scale) {
  SPQS_CHECK(local_ != nullptr, "ViolatorOracle local pointer is null");
  SPQS_CHECK(global_ != nullptr, "ViolatorOracle global pointer is null");
  SPQS_CHECK(local_->layout.B > 0, "ViolatorOracle requires at least one local block");

  block_best_.resize(static_cast<std::size_t>(local_->layout.B), negative_infinity_local());
  tree_leaf_count_ = ceil_pow2(local_->layout.B);
  tree_winner_block_.assign(static_cast<std::size_t>(2 * tree_leaf_count_), -1);
  local_active_.assign(static_cast<std::size_t>(local_->total_rows()), 0U);
  global_active_.assign(static_cast<std::size_t>(global_->m_global), 0U);

  global_best_ = negative_infinity_global();
  last_ = negative_infinity_global();
}

void ViolatorOracle::clear_active() {
  ++total_active_clears_;
  std::fill(local_active_.begin(), local_active_.end(), static_cast<std::uint8_t>(0));
  std::fill(global_active_.begin(), global_active_.end(), static_cast<std::uint8_t>(0));
}

void ViolatorOracle::set_active_from_pos_of_id(const int* pos_of_id, int m_total) {
  clear_active();
  if (pos_of_id == nullptr || m_total <= 0) {
    return;
  }

  const int local_total = local_->total_rows();
  const int global_total = global_->m_global;
  const int total = local_total + global_total;
  const int limit = std::min(m_total, total);

  for (int id = 0; id < limit; ++id) {
    if (pos_of_id[id] < 0) {
      continue;
    }
    if (id < local_total) {
      local_active_[static_cast<std::size_t>(id)] = 1U;
      continue;
    }
    const int g = id - local_total;
    if (g >= 0 && g < global_total) {
      global_active_[static_cast<std::size_t>(g)] = 1U;
    }
  }
}

void ViolatorOracle::set_active_ids(const int* active_ids, int active_count) {
  clear_active();
  if (active_ids == nullptr || active_count <= 0) {
    return;
  }

  const int local_total = local_->total_rows();
  const int global_total = global_->m_global;
  const int total = local_total + global_total;

  for (int i = 0; i < active_count; ++i) {
    const int id = active_ids[i];
    if (id < 0 || id >= total) {
      continue;
    }
    if (id < local_total) {
      local_active_[static_cast<std::size_t>(id)] = 1U;
      continue;
    }
    const int g = id - local_total;
    global_active_[static_cast<std::size_t>(g)] = 1U;
  }
}

void ViolatorOracle::set_active_ids_bulk(const int* active_ids, int active_count) {
  set_active_ids(active_ids, active_count);
}

void ViolatorOracle::activate_constraint_id(int constraint_id) {
  const int local_total = local_->total_rows();
  const int total = local_total + global_->m_global;
  if (constraint_id < 0 || constraint_id >= total) {
    return;
  }
  if (constraint_id < local_total) {
    local_active_[static_cast<std::size_t>(constraint_id)] = 1U;
    return;
  }
  global_active_[static_cast<std::size_t>(constraint_id - local_total)] = 1U;
}

void ViolatorOracle::deactivate_constraint_id(int constraint_id) {
  const int local_total = local_->total_rows();
  const int total = local_total + global_->m_global;
  if (constraint_id < 0 || constraint_id >= total) {
    return;
  }
  if (constraint_id < local_total) {
    local_active_[static_cast<std::size_t>(constraint_id)] = 0U;
    return;
  }
  global_active_[static_cast<std::size_t>(constraint_id - local_total)] = 0U;
}

void ViolatorOracle::recompute_block(int block_id, const double* q_n) {
  SPQS_CHECK(rhs_ != nullptr, "ViolatorOracle rhs must be set before recompute_block");
  SPQS_CHECK(block_id >= 0 && block_id < local_->layout.B, "block_id out of range in recompute_block");

  const auto& block = local_->A_block[static_cast<std::size_t>(block_id)];
  const int offset = local_->layout.block_offsets[block_id];
  const double* q_block = q_n + offset;
  const double* b_block = rhs_->local.block_ptr(block_id);

  Violator best = negative_infinity_local();
  best.block_id = block_id;

  for (int i = 0; i < block.rows; ++i) {
    const int constraint_id = local_->constraint_id(block_id, i);
    if (local_active_[static_cast<std::size_t>(constraint_id)] != 0U) {
      continue;
    }

    const CertifiedDot cd = dot_certified_fma(block.row_ptr(i), q_block, block.cols);
    const double v = certified_violation_from_dot(
        cd.s_hat, cd.t_abs, block.cols, b_block[i], tau_abs_scale_);

    Violator cand;
    cand.scope = ConstraintScope::LOCAL;
    cand.block_id = block_id;
    cand.row_in_block = i;
    cand.global_row = -1;
    cand.constraint_id = constraint_id;
    cand.violation = v;

    if (prefer_lhs(cand, best)) {
      best = cand;
    }
  }

  block_best_[static_cast<std::size_t>(block_id)] = best;
}

void ViolatorOracle::recompute_global(const double* q_n) {
  SPQS_CHECK(rhs_ != nullptr, "ViolatorOracle rhs must be set before recompute_global");

  Violator best = negative_infinity_global();
  const int global_base = local_->total_rows();
  const double* b_global = rhs_->global.data();

  for (int g = 0; g < global_->m_global; ++g) {
    if (global_active_[static_cast<std::size_t>(g)] != 0U) {
      continue;
    }

    const CertifiedDot cd = dot_certified_fma(global_->row_ptr(g), q_n, global_->n);
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

  global_best_ = best;
  last_stats_.global_rows_scanned = global_->m_global;
}

void ViolatorOracle::rebuild_tree() {
  for (int leaf = 0; leaf < tree_leaf_count_; ++leaf) {
    const int node = tree_leaf_count_ + leaf;
    tree_winner_block_[static_cast<std::size_t>(node)] =
        (leaf < local_->layout.B) ? leaf : -1;
  }

  for (int node = tree_leaf_count_ - 1; node >= 1; --node) {
    const int left_block = tree_winner_block_[static_cast<std::size_t>(2 * node)];
    const int right_block = tree_winner_block_[static_cast<std::size_t>(2 * node + 1)];

    if (left_block < 0) {
      tree_winner_block_[static_cast<std::size_t>(node)] = right_block;
      continue;
    }
    if (right_block < 0) {
      tree_winner_block_[static_cast<std::size_t>(node)] = left_block;
      continue;
    }

    const Violator& l = block_best_[static_cast<std::size_t>(left_block)];
    const Violator& r = block_best_[static_cast<std::size_t>(right_block)];
    tree_winner_block_[static_cast<std::size_t>(node)] = prefer_lhs(l, r) ? left_block : right_block;
  }
}

void ViolatorOracle::update_tree_leaf(int block_id) {
  SPQS_CHECK(block_id >= 0 && block_id < local_->layout.B, "block_id out of range in update_tree_leaf");

  int node = tree_leaf_count_ + block_id;
  tree_winner_block_[static_cast<std::size_t>(node)] = block_id;

  node /= 2;
  while (node >= 1) {
    const int left_block = tree_winner_block_[static_cast<std::size_t>(2 * node)];
    const int right_block = tree_winner_block_[static_cast<std::size_t>(2 * node + 1)];

    int winner = -1;
    if (left_block < 0) {
      winner = right_block;
    } else if (right_block < 0) {
      winner = left_block;
    } else {
      const Violator& l = block_best_[static_cast<std::size_t>(left_block)];
      const Violator& r = block_best_[static_cast<std::size_t>(right_block)];
      winner = prefer_lhs(l, r) ? left_block : right_block;
    }

    tree_winner_block_[static_cast<std::size_t>(node)] = winner;
    node /= 2;
  }
}

Violator ViolatorOracle::best_local() const {
  const int winner_block = tree_winner_block_[1];
  if (winner_block < 0) {
    return negative_infinity_local();
  }
  return block_best_[static_cast<std::size_t>(winner_block)];
}

Violator ViolatorOracle::merge_local_global() const {
  const Violator local_best = best_local();
  return prefer_lhs(local_best, global_best_) ? local_best : global_best_;
}

void ViolatorOracle::init_tick(const double* q_n, const RHSAll& rhs) {
  SPQS_CHECK(q_n != nullptr, "ViolatorOracle init_tick q_n is null");

  rhs_ = &rhs;
  last_stats_ = OracleStats{};
  ++total_init_tick_calls_;
  last_stats_.init_tick_calls_total = total_init_tick_calls_;
  last_stats_.active_clears_total = total_active_clears_;

  for (int r = 0; r < local_->layout.B; ++r) {
    recompute_block(r, q_n);
    ++last_stats_.local_blocks_recomputed;
  }

  rebuild_tree();
  recompute_global(q_n);
  last_ = merge_local_global();
}

void ViolatorOracle::update_blocks(const int* block_ids,
                                   int num_blocks,
                                   const double* q_n,
                                   const RHSAll& rhs) {
  SPQS_CHECK(q_n != nullptr, "ViolatorOracle update_blocks q_n is null");
  SPQS_CHECK(num_blocks >= 0, "ViolatorOracle update_blocks num_blocks must be >= 0");

  rhs_ = &rhs;
  last_stats_ = OracleStats{};
  last_stats_.init_tick_calls_total = total_init_tick_calls_;
  last_stats_.active_clears_total = total_active_clears_;

  int prev = -1;
  for (int idx = 0; idx < num_blocks; ++idx) {
    const int block_id = block_ids[idx];
    SPQS_CHECK(block_id > prev, "update_blocks requires unique sorted block_ids");
    SPQS_CHECK(block_id >= 0 && block_id < local_->layout.B,
               "update_blocks block_id out of range");

    recompute_block(block_id, q_n);
    update_tree_leaf(block_id);
    ++last_stats_.local_blocks_recomputed;
    prev = block_id;
  }

  recompute_global(q_n);
  last_ = merge_local_global();
}

Violator ViolatorOracle::max_violation_inactive() const { return last_; }

Violator ViolatorOracle::max_violation() const { return max_violation_inactive(); }

bool ViolatorOracle::certified_feasible() const { return last_.violation <= 0.0; }

OracleStats ViolatorOracle::last_stats() const { return last_stats_; }

std::uint64_t ViolatorOracle::total_init_tick_calls() const {
  return total_init_tick_calls_;
}

std::uint64_t ViolatorOracle::total_active_clears() const {
  return total_active_clears_;
}

}  // namespace spqs
