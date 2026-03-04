#pragma once

#include <cstdint>
#include <vector>

#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_types.hpp"

namespace spqs {

struct OracleStats {
  int local_blocks_recomputed = 0;
  int global_rows_scanned = 0;
  std::uint64_t init_tick_calls_total = 0;
  std::uint64_t active_clears_total = 0;
};

class ViolatorOracle {
 public:
  ViolatorOracle(const ConstraintsLocal* local,
                 const ConstraintsGlobal* global,
                 double tau_abs_scale);

  void clear_active();
  void set_active_from_pos_of_id(const int* pos_of_id, int m_total);
  void set_active_ids(const int* active_ids, int active_count);
  void set_active_ids_bulk(const int* active_ids, int active_count);
  void activate_constraint_id(int constraint_id);
  void deactivate_constraint_id(int constraint_id);

  void init_tick(const double* q_n, const RHSAll& rhs);
  void update_blocks(const int* block_ids, int num_blocks, const double* q_n, const RHSAll& rhs);
  [[nodiscard]] Violator max_violation_inactive() const;
  [[nodiscard]] Violator max_violation() const;
  [[nodiscard]] bool certified_feasible() const;
  [[nodiscard]] OracleStats last_stats() const;
  [[nodiscard]] std::uint64_t total_init_tick_calls() const;
  [[nodiscard]] std::uint64_t total_active_clears() const;

 private:
  void recompute_block(int block_id, const double* q_n);
  void recompute_global(const double* q_n);
  void rebuild_tree();
  void update_tree_leaf(int block_id);
  [[nodiscard]] Violator best_local() const;
  [[nodiscard]] Violator merge_local_global() const;

  const ConstraintsLocal* local_;
  const ConstraintsGlobal* global_;
  double tau_abs_scale_;
  const RHSAll* rhs_ = nullptr;

  std::vector<Violator> block_best_;
  std::vector<int> tree_winner_block_;
  int tree_leaf_count_ = 0;

  std::vector<std::uint8_t> local_active_;
  std::vector<std::uint8_t> global_active_;
  std::uint64_t total_init_tick_calls_ = 0;
  std::uint64_t total_active_clears_ = 0;

  Violator global_best_;
  Violator last_;
  OracleStats last_stats_;
};

}  // namespace spqs
