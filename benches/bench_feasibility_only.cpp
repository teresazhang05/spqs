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
#include "spqs/structure_validator.hpp"

namespace {

std::string read_arg(int argc, char** argv, const std::string& key, const std::string& fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return argv[i + 1];
    }
  }
  return fallback;
}

std::uint64_t read_u64_arg(int argc, char** argv, const std::string& key, std::uint64_t fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return static_cast<std::uint64_t>(std::stoull(argv[i + 1]));
    }
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string cfg_path = read_arg(argc, argv, "--config", "configs/default_block256_debug.yaml");
  const std::uint64_t ticks = read_u64_arg(argc, argv, "--ticks", 20000);

  spqs::Config cfg;
  std::string err;
  if (!spqs::load_config(cfg_path, &cfg, &err)) {
    std::cerr << "bench_feasibility_only failed to load config: " << err << "\n";
    return 2;
  }

  const spqs::StructureValidationResult valid = spqs::validate_structure_from_config(cfg);
  std::vector<int> block_sizes = cfg.structure.block_sizes;
  if (!valid.structure_valid) {
    block_sizes.assign(1, cfg.problem.n);
  }

  spqs::BlockLayout layout;
  if (!spqs::make_block_layout(cfg.problem.n, block_sizes, &layout, &err)) {
    std::cerr << "bench_feasibility_only failed to build layout: " << err << "\n";
    return 3;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(
      layout, cfg.structure.m_local_per_block, cfg.run.seed);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(
      cfg.problem.n, cfg.structure.m_global, cfg.generator.factors, cfg.run.seed + 7U);
  const spqs::RHSAll rhs = spqs::generate_rhs(
      local, global, cfg.generator.b_margin, cfg.generator.b_noise_std, cfg.run.seed + 19U);

  spqs::BruteForceScan scan(&local, &global, cfg.solver.tau_abs_scale);

  std::mt19937_64 rng(cfg.run.seed + 1001U);
  std::uniform_real_distribution<double> small(-cfg.stream.q_small, cfg.stream.q_small);
  std::uniform_real_distribution<double> big(-cfg.stream.q_big, cfg.stream.q_big);
  std::bernoulli_distribution choose_big(cfg.stream.p_big);

  std::vector<double> q(static_cast<std::size_t>(cfg.problem.n), 0.0);
  std::uint64_t certified_ok = 0;
  std::uint64_t audit_fail = 0;

  for (std::uint64_t t = 0; t < ticks; ++t) {
    const bool is_big = choose_big(rng);
    for (double& v : q) {
      v = is_big ? big(rng) : small(rng);
    }

    if (scan.certified_feasible(q.data(), rhs)) {
      ++certified_ok;
      if (!scan.audit_feasible_long_double(q.data(), rhs)) {
        ++audit_fail;
      }
    }
  }

  std::cout << "ticks=" << ticks << "\n";
  std::cout << "certified_ok=" << certified_ok << "\n";
  std::cout << "audit_fail=" << audit_fail << "\n";
  std::cout << "bench_feasibility_only: OK\n";
  return (audit_fail == 0) ? 0 : 4;
}
