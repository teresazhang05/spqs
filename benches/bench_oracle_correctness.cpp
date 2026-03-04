#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/config_loader.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_oracle.hpp"

namespace {

std::string read_arg(int argc, char** argv, const std::string& key, const std::string& fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return argv[i + 1];
    }
  }
  return fallback;
}

int read_int_arg(int argc, char** argv, const std::string& key, int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return std::stoi(argv[i + 1]);
    }
  }
  return fallback;
}

bool bitwise_equal(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

bool same_violator(const spqs::Violator& a, const spqs::Violator& b) {
  return a.constraint_id == b.constraint_id && bitwise_equal(a.violation, b.violation);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string cfg_path = read_arg(argc, argv, "--config", "configs/default_block256_debug.yaml");
  const int trials = read_int_arg(argc, argv, "--trials", 100000);
  const int max_updated_blocks = read_int_arg(argc, argv, "--max-updated-blocks", 4);

  spqs::Config cfg;
  std::string err;
  if (!spqs::load_config(cfg_path, &cfg, &err)) {
    std::cerr << "bench_oracle_correctness failed to load config: " << err << "\n";
    return 2;
  }

  spqs::BlockLayout layout;
  if (!spqs::make_block_layout(cfg.problem.n, cfg.structure.block_sizes, &layout, &err)) {
    std::cerr << "bench_oracle_correctness failed to build layout: " << err << "\n";
    return 3;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(
      layout, cfg.structure.m_local_per_block, cfg.run.seed);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(
      cfg.problem.n, cfg.structure.m_global, cfg.generator.factors, cfg.run.seed + 7U);
  const spqs::RHSAll rhs = spqs::generate_rhs(
      local, global, cfg.generator.b_margin, cfg.generator.b_noise_std, cfg.run.seed + 19U);

  spqs::BruteForceScan brute(&local, &global, cfg.solver.tau_abs_scale);
  spqs::ViolatorOracle oracle(&local, &global, cfg.solver.tau_abs_scale);

  std::mt19937_64 rng(cfg.run.seed + 555U);
  std::uniform_real_distribution<double> ud(-cfg.stream.q_small, cfg.stream.q_small);

  std::vector<double> q(static_cast<std::size_t>(cfg.problem.n), 0.0);
  for (double& x : q) {
    x = ud(rng);
  }

  oracle.init_tick(q.data(), rhs);
  spqs::Violator v_oracle = oracle.max_violation();
  spqs::Violator v_brute = brute.max_violation(q.data(), rhs);

  int mismatches = 0;
  int stats_mismatches = 0;

  if (!same_violator(v_oracle, v_brute)) {
    ++mismatches;
    std::cerr << "init mismatch"
              << " oracle.id=" << v_oracle.constraint_id
              << " brute.id=" << v_brute.constraint_id
              << " oracle.v=" << v_oracle.violation
              << " brute.v=" << v_brute.violation << "\n";
  }

  const spqs::OracleStats init_stats = oracle.last_stats();
  if (init_stats.local_blocks_recomputed != local.layout.B ||
      init_stats.global_rows_scanned != global.m_global) {
    ++stats_mismatches;
    std::cerr << "init stats mismatch"
              << " local_blocks_recomputed=" << init_stats.local_blocks_recomputed
              << " expected=" << local.layout.B
              << " global_rows_scanned=" << init_stats.global_rows_scanned
              << " expected=" << global.m_global << "\n";
  }

  std::vector<int> block_ids(static_cast<std::size_t>(local.layout.B), 0);
  for (int r = 0; r < local.layout.B; ++r) {
    block_ids[static_cast<std::size_t>(r)] = r;
  }

  std::uniform_int_distribution<int> h_dist(0, std::min(max_updated_blocks, local.layout.B));

  for (int t = 0; t < trials && mismatches == 0 && stats_mismatches == 0; ++t) {
    std::shuffle(block_ids.begin(), block_ids.end(), rng);
    const int h = h_dist(rng);

    std::vector<int> changed(block_ids.begin(), block_ids.begin() + h);
    std::sort(changed.begin(), changed.end());

    for (int block_id : changed) {
      const int offset = local.layout.block_offsets[block_id];
      const int len = local.layout.block_sizes[block_id];
      for (int j = 0; j < len; ++j) {
        q[static_cast<std::size_t>(offset + j)] = ud(rng);
      }
    }

    oracle.update_blocks(changed.data(), static_cast<int>(changed.size()), q.data(), rhs);
    v_oracle = oracle.max_violation();
    v_brute = brute.max_violation(q.data(), rhs);

    if (!same_violator(v_oracle, v_brute)) {
      ++mismatches;
      std::cerr << "trial mismatch at t=" << t
                << " changed_blocks=" << changed.size()
                << " oracle.id=" << v_oracle.constraint_id
                << " brute.id=" << v_brute.constraint_id
                << " oracle.v=" << v_oracle.violation
                << " brute.v=" << v_brute.violation << "\n";
      break;
    }

    const spqs::OracleStats s = oracle.last_stats();
    if (s.local_blocks_recomputed != static_cast<int>(changed.size()) ||
        s.global_rows_scanned != global.m_global) {
      ++stats_mismatches;
      std::cerr << "stats mismatch at t=" << t
                << " local_blocks_recomputed=" << s.local_blocks_recomputed
                << " expected=" << changed.size()
                << " global_rows_scanned=" << s.global_rows_scanned
                << " expected=" << global.m_global << "\n";
      break;
    }
  }

  std::cout << "trials=" << trials
            << " mismatches=" << mismatches
            << " stats_mismatches=" << stats_mismatches << "\n";
  std::cout << "bench_oracle_correctness: "
            << ((mismatches == 0 && stats_mismatches == 0) ? "OK" : "FAIL")
            << "\n";

  return (mismatches == 0 && stats_mismatches == 0) ? 0 : 4;
}
