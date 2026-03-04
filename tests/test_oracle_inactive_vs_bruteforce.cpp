#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/fp_cert.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_oracle.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool bitwise_equal(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

bool same_violator(const spqs::Violator& a, const spqs::Violator& b) {
  return a.constraint_id == b.constraint_id && bitwise_equal(a.violation, b.violation);
}

spqs::Violator negative_infinity_local() {
  spqs::Violator v;
  v.scope = spqs::ConstraintScope::LOCAL;
  v.block_id = -1;
  v.row_in_block = -1;
  v.global_row = -1;
  v.constraint_id = std::numeric_limits<int>::max();
  v.violation = -std::numeric_limits<double>::infinity();
  return v;
}

spqs::Violator negative_infinity_global() {
  spqs::Violator v;
  v.scope = spqs::ConstraintScope::GLOBAL;
  v.block_id = -1;
  v.row_in_block = -1;
  v.global_row = -1;
  v.constraint_id = std::numeric_limits<int>::max();
  v.violation = -std::numeric_limits<double>::infinity();
  return v;
}

spqs::Violator brute_force_worst_inactive(const spqs::ConstraintsLocal& local,
                                          const spqs::ConstraintsGlobal& global,
                                          const spqs::RHSAll& rhs,
                                          double tau_abs_scale,
                                          const std::vector<int>& pos_of_id,
                                          const double* q_n) {
  spqs::Violator best_local = negative_infinity_local();
  for (int r = 0; r < local.layout.B; ++r) {
    const auto& block = local.A_block[static_cast<std::size_t>(r)];
    const int offset = local.layout.block_offsets[r];
    const double* q_block = q_n + offset;
    const double* b_block = rhs.local.block_ptr(r);

    for (int i = 0; i < block.rows; ++i) {
      const int id = local.constraint_id(r, i);
      if (id >= 0 && id < static_cast<int>(pos_of_id.size()) && pos_of_id[static_cast<std::size_t>(id)] >= 0) {
        continue;
      }

      const spqs::CertifiedDot cd = spqs::dot_certified_fma(block.row_ptr(i), q_block, block.cols);
      const double v = spqs::certified_violation_from_dot(
          cd.s_hat, cd.t_abs, block.cols, b_block[i], tau_abs_scale);

      spqs::Violator cand;
      cand.scope = spqs::ConstraintScope::LOCAL;
      cand.block_id = r;
      cand.row_in_block = i;
      cand.global_row = -1;
      cand.constraint_id = id;
      cand.violation = v;

      if (spqs::prefer_lhs(cand, best_local)) {
        best_local = cand;
      }
    }
  }

  spqs::Violator best_global = negative_infinity_global();
  const int global_base = local.total_rows();
  const double* b_global = rhs.global.data();
  for (int g = 0; g < global.m_global; ++g) {
    const int id = global_base + g;
    if (id >= 0 && id < static_cast<int>(pos_of_id.size()) && pos_of_id[static_cast<std::size_t>(id)] >= 0) {
      continue;
    }

    const spqs::CertifiedDot cd = spqs::dot_certified_fma(global.row_ptr(g), q_n, global.n);
    const double v = spqs::certified_violation_from_dot(
        cd.s_hat, cd.t_abs, global.n, b_global[g], tau_abs_scale);

    spqs::Violator cand;
    cand.scope = spqs::ConstraintScope::GLOBAL;
    cand.block_id = -1;
    cand.row_in_block = -1;
    cand.global_row = g;
    cand.constraint_id = id;
    cand.violation = v;

    if (spqs::prefer_lhs(cand, best_global)) {
      best_global = cand;
    }
  }

  return spqs::prefer_lhs(best_local, best_global) ? best_local : best_global;
}

void random_active_pos_map(std::mt19937_64* rng,
                           int total_constraints,
                           int max_active,
                           std::vector<int>* id_pool,
                           std::vector<int>* pos_of_id) {
  std::fill(pos_of_id->begin(), pos_of_id->end(), -1);
  std::shuffle(id_pool->begin(), id_pool->end(), *rng);
  const int cap = std::min(max_active, total_constraints);
  std::uniform_int_distribution<int> k_dist(0, cap);
  const int k = k_dist(*rng);
  for (int i = 0; i < k; ++i) {
    const int id = (*id_pool)[static_cast<std::size_t>(i)];
    (*pos_of_id)[static_cast<std::size_t>(id)] = i;
  }
}

bool test_inactive_oracle_random_init_matches_bruteforce() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(64, {16, 16, 16, 16}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 12, 4101);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(64, 8, 4, 4102);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 6.0, 0.2, 4103);

  spqs::ViolatorOracle oracle(&local, &global, 8.0);

  std::mt19937_64 rng(4104);
  std::uniform_real_distribution<double> ud(-2.0, 2.0);

  std::vector<double> q(64, 0.0);
  const int total = local.total_rows() + global.m_global;
  const int max_active = std::min(32, total / 3);
  std::vector<int> id_pool(static_cast<std::size_t>(total), 0);
  for (int i = 0; i < total; ++i) {
    id_pool[static_cast<std::size_t>(i)] = i;
  }
  std::vector<int> pos_of_id(static_cast<std::size_t>(total), -1);

  for (int t = 0; t < 5000; ++t) {
    for (double& x : q) {
      x = ud(rng);
    }
    random_active_pos_map(&rng, total, max_active, &id_pool, &pos_of_id);

    oracle.set_active_from_pos_of_id(pos_of_id.data(), total);
    oracle.init_tick(q.data(), rhs);

    const spqs::Violator vo = oracle.max_violation_inactive();
    const spqs::Violator vb =
        brute_force_worst_inactive(local, global, rhs, 8.0, pos_of_id, q.data());
    if (!check(same_violator(vo, vb), "inactive oracle init mismatch vs brute force")) {
      return false;
    }
  }
  return true;
}

bool test_inactive_oracle_incremental_matches_bruteforce() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(96, {24, 24, 24, 24}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 10, 4201);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(96, 6, 3, 4202);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 8.0, 0.2, 4203);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);

  std::mt19937_64 rng(4204);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);
  std::uniform_int_distribution<int> h_dist(0, 3);

  std::vector<double> q(96, 0.0);
  for (double& x : q) {
    x = ud(rng);
  }

  const int total = local.total_rows() + global.m_global;
  std::vector<int> id_pool(static_cast<std::size_t>(total), 0);
  for (int i = 0; i < total; ++i) {
    id_pool[static_cast<std::size_t>(i)] = i;
  }
  std::vector<int> pos_of_id(static_cast<std::size_t>(total), -1);
  random_active_pos_map(&rng, total, std::min(24, total / 4), &id_pool, &pos_of_id);

  oracle.set_active_from_pos_of_id(pos_of_id.data(), total);
  oracle.init_tick(q.data(), rhs);

  std::vector<int> all_blocks(local.layout.B, 0);
  for (int r = 0; r < local.layout.B; ++r) {
    all_blocks[static_cast<std::size_t>(r)] = r;
  }

  for (int t = 0; t < 5000; ++t) {
    std::shuffle(all_blocks.begin(), all_blocks.end(), rng);
    const int h = h_dist(rng);
    std::vector<int> changed(all_blocks.begin(), all_blocks.begin() + h);
    std::sort(changed.begin(), changed.end());

    for (int block_id : changed) {
      const int offset = local.layout.block_offsets[block_id];
      const int len = local.layout.block_sizes[block_id];
      for (int j = 0; j < len; ++j) {
        q[static_cast<std::size_t>(offset + j)] = ud(rng);
      }
    }

    oracle.update_blocks(changed.data(), static_cast<int>(changed.size()), q.data(), rhs);
    const spqs::Violator vo = oracle.max_violation_inactive();
    const spqs::Violator vb =
        brute_force_worst_inactive(local, global, rhs, 8.0, pos_of_id, q.data());
    if (!check(same_violator(vo, vb), "inactive oracle incremental mismatch vs brute force")) {
      return false;
    }

    const spqs::OracleStats s = oracle.last_stats();
    if (!check(s.local_blocks_recomputed == static_cast<int>(changed.size()),
               "update_blocks should recompute only changed local blocks")) {
      return false;
    }
    if (!check(s.global_rows_scanned == global.m_global,
               "update_blocks should scan all global rows")) {
      return false;
    }
  }

  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_inactive_oracle_random_init_matches_bruteforce() && ok;
  ok = test_inactive_oracle_incremental_matches_bruteforce() && ok;

  if (ok) {
    std::cout << "test_oracle_inactive_vs_bruteforce: PASS\n";
    return 0;
  }
  std::cerr << "test_oracle_inactive_vs_bruteforce: FAIL\n";
  return 1;
}
