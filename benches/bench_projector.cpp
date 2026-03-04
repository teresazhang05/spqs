#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/arena.hpp"
#include "spqs/block_layout.hpp"
#include "spqs/config_loader.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/projector.hpp"
#include "spqs/rhs.hpp"
#include "spqs/structure_validator.hpp"
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

}  // namespace

int main(int argc, char** argv) {
  const std::string cfg_path = read_arg(argc, argv, std::string("--config"), "configs/default_block256_debug.yaml");

  spqs::Config cfg;
  std::string err;
  if (!spqs::load_config(cfg_path, &cfg, &err)) {
    std::cerr << "bench_projector failed to load config: " << err << "\n";
    return 2;
  }

  const spqs::StructureValidationResult valid = spqs::validate_structure_from_config(cfg);

  std::vector<int> block_sizes = cfg.structure.block_sizes;
  if (!valid.structure_valid) {
    block_sizes.assign(1, cfg.problem.n);
  }

  spqs::BlockLayout layout;
  if (!spqs::make_block_layout(cfg.problem.n, block_sizes, &layout, &err)) {
    std::cerr << "bench_projector failed to build layout: " << err << "\n";
    return 3;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(
      layout, cfg.structure.m_local_per_block, cfg.run.seed);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(
      cfg.problem.n, cfg.structure.m_global, cfg.generator.factors, cfg.run.seed + 7U);
  const spqs::RHSAll rhs = spqs::generate_rhs(
      local, global, cfg.generator.b_margin, cfg.generator.b_noise_std, cfg.run.seed + 19U);

  std::vector<double> q_prop(static_cast<std::size_t>(cfg.problem.n), 0.0);
  std::mt19937_64 rng(cfg.run.seed + 123U);
  std::uniform_real_distribution<double> ud(-cfg.stream.q_small, cfg.stream.q_small);
  for (double& v : q_prop) {
    v = ud(rng);
  }

  std::vector<double> q_out = q_prop;
  spqs::Arena arena(1U << 20U);
  spqs::ViolatorOracle oracle(&local, &global, cfg.solver.tau_abs_scale);
  spqs::StreamingProjector projector(&local, &global, &oracle, &arena);
  spqs::ProjectorOptions opts;
  opts.a_max = cfg.problem.a_max;
  opts.I_max = cfg.problem.I_max;
  opts.warm_start = cfg.solver.warm_start;
  opts.bland_rule = cfg.solver.bland_rule;
  opts.force_full_rescan = (valid.mode == spqs::RunMode::BRUTEFORCE_ONLY);
  opts.tau_abs_scale = cfg.solver.tau_abs_scale;
  opts.strict_interior = cfg.solver.strict_interior;
  opts.kappa_min = cfg.solver.kappa_min;
  opts.tau_shrink_min = cfg.solver.tau_shrink_min;
  opts.tau_shrink_max = cfg.solver.tau_shrink_max;
  opts.fallback_enabled = cfg.solver.fallback_enabled;
  projector.set_options(opts);
  projector.set_rhs(rhs);
  const spqs::SolverStats stats = projector.project(q_prop.data(), q_out.data());

  std::cout << "run_name=" << cfg.run.run_name << "\n";
  std::cout << "mode="
            << (valid.mode == spqs::RunMode::STRUCTURED_FAST ? "STRUCTURED_FAST" : "BRUTEFORCE_ONLY")
            << "\n";
  std::cout << "structure_valid=" << (valid.structure_valid ? "true" : "false") << "\n";
  std::cout << "max_violation_certified=" << stats.max_violation_certified << "\n";
  std::cout << "min_slack_certified=" << stats.min_slack_certified << "\n";
  std::cout << "tau_required=" << stats.tau_required << "\n";
  std::cout << "tau_shrink_used=" << stats.tau_shrink_used << "\n";
  std::cout << "kappa_valid=" << (stats.kappa_valid ? "true" : "false") << "\n";
  std::cout << "min_kappa=" << stats.min_kappa << "\n";
  std::cout << "fallback_used=" << (stats.fallback_used ? "true" : "false") << "\n";
  std::cout << "fallback_alpha=" << stats.fallback_alpha << "\n";
  std::cout << "bench_projector: OK\n";
  return 0;
}
