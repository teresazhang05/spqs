#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
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

bool test_tie_break_local_over_global() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(1, {1}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  spqs::ConstraintsLocal local;
  local.layout = layout;
  local.m_local_per_block = 1;
  local.row_id_base = {0};
  local.A_block.resize(1);
  local.A_block[0].rows = 1;
  local.A_block[0].cols = 1;
  local.A_block[0].data.resize(1);
  local.A_block[0].data[0] = 0.0;

  spqs::ConstraintsGlobal global;
  global.n = 1;
  global.m_global = 1;
  global.A.resize(1);
  global.A[0] = 0.0;

  spqs::RHSAll rhs;
  rhs.local.B = 1;
  rhs.local.m_local_per_block = 1;
  rhs.local.b_block.resize(1);
  rhs.local.b_block[0].resize(1);
  rhs.local.b_block[0][0] = 0.0;
  rhs.global.m_global = 1;
  rhs.global.b.resize(1);
  rhs.global.b[0] = 0.0;

  const double q[1] = {0.0};

  spqs::ViolatorOracle oracle(&local, &global, 8.0);
  oracle.init_tick(q, rhs);
  const spqs::Violator v = oracle.max_violation();
  if (!check(v.scope == spqs::ConstraintScope::LOCAL,
             "tie-break must prefer LOCAL over GLOBAL under bitwise-equal violation")) {
    return false;
  }
  return check(v.constraint_id == 0, "expected local constraint_id=0");
}

bool test_init_tick_matches_bruteforce() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(64, {16, 16, 16, 16}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 8, 801);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(64, 8, 4, 802);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 10.0, 0.2, 803);

  spqs::BruteForceScan brute(&local, &global, 8.0);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);

  std::mt19937_64 rng(804);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);
  std::vector<double> q(64, 0.0);

  for (int t = 0; t < 2000; ++t) {
    for (double& x : q) {
      x = ud(rng);
    }

    oracle.init_tick(q.data(), rhs);
    const spqs::Violator vo = oracle.max_violation();
    const spqs::Violator vb = brute.max_violation(q.data(), rhs);

    if (!check(same_violator(vo, vb), "init_tick oracle mismatch vs brute force")) {
      return false;
    }

    const spqs::OracleStats s = oracle.last_stats();
    if (!check(s.local_blocks_recomputed == local.layout.B,
               "init_tick should recompute all local blocks")) {
      return false;
    }
    if (!check(s.global_rows_scanned == global.m_global,
               "init_tick should scan all global rows")) {
      return false;
    }
  }

  return true;
}

bool test_incremental_update_matches_bruteforce() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(96, {24, 24, 24, 24}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 10, 901);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(96, 6, 3, 902);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 8.0, 0.2, 903);

  spqs::BruteForceScan brute(&local, &global, 8.0);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);

  std::mt19937_64 rng(904);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);
  std::uniform_int_distribution<int> h_dist(0, 3);

  std::vector<double> q(96, 0.0);
  for (double& x : q) {
    x = ud(rng);
  }
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

    const spqs::Violator vo = oracle.max_violation();
    const spqs::Violator vb = brute.max_violation(q.data(), rhs);
    if (!check(same_violator(vo, vb), "incremental oracle mismatch vs brute force")) {
      return false;
    }

    const spqs::OracleStats s = oracle.last_stats();
    if (!check(s.local_blocks_recomputed == static_cast<int>(changed.size()),
               "update_blocks should recompute only touched local blocks")) {
      return false;
    }
    if (!check(s.global_rows_scanned == global.m_global,
               "update_blocks should rescan full global rows each iteration")) {
      return false;
    }
  }

  return true;
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_tie_break_local_over_global() && ok;
  ok = test_init_tick_matches_bruteforce() && ok;
  ok = test_incremental_update_matches_bruteforce() && ok;

  if (ok) {
    std::cout << "test_oracle_vs_bruteforce: PASS\n";
    return 0;
  }
  std::cerr << "test_oracle_vs_bruteforce: FAIL\n";
  return 1;
}
