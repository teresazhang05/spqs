#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spqs {

struct RunConfig {
  std::string run_name = "block256_default";
  std::uint64_t seed = 12345;
  std::string out_dir = "results/block256_default";
  int threads = 1;
  int pin_cpu = 2;
  bool disable_turbo = true;
  bool disable_smt = true;
};

struct ProblemConfig {
  int n = 256;
  int a_max = 96;
  int I_max = 200;
};

struct StructureConfig {
  std::string mode = "block_sparse";
  int B = 16;
  std::vector<int> block_sizes = std::vector<int>(16, 16);
  int m_local_per_block = 64;
  int m_global = 32;
};

struct GeneratorConfig {
  std::string A_local_type = "dense_in_block_normalized";
  std::string A_global_type = "factor_plus_gross";
  int factors = 8;
  std::string row_norm = "l2_unit";
  double b_margin = 10.0;
  double b_noise_std = 0.2;
};

struct SolverConfig {
  bool warm_start = true;
  bool bland_rule = true;
  std::string feasibility = "certified";
  std::string q_anchor = "zero";
  bool strict_interior = true;
  double kappa_min = 1e-6;
  double tau_abs_scale = 8.0;
  double tau_shrink_min = 0.0;
  double tau_shrink_max = 1e-3;
  bool fallback_enabled = true;
  std::string fallback_mode = "ray_scale_to_anchor";
};

struct StreamConfig {
  std::string mode = "iid_dense";
  std::uint64_t seed_stream = 424242;
  std::uint64_t T_warmup = 1000000;
  std::uint64_t T_latency = 50000000;
  std::uint64_t T_feas = 1000000000;
  double ar_rho = 0.995;
  int K_small = 1;
  int K_small_alt = 2;
  double p_K_small_alt = 0.02;
  double delta_small = 0.2;
  double p_jump = 0.001;
  int K_jump = 4;
  double delta_jump = 2.0;
  double clamp_inf = 100.0;

  struct BurstConfig {
    bool enabled = false;
    std::uint64_t every_ticks = 2000000;
    std::uint64_t length_ticks = 20000;
    int K_burst = 6;
    double delta_burst = 2.0;
    double p_jump_in_burst = 0.01;
    int K_jump_in_burst = 10;
    double delta_jump_in_burst = 8.0;
  } burst;

  // Legacy iid_dense knobs retained for explicit stress-mode parity.
  double q_small = 1.0;
  double q_big = 50.0;
  double p_big = 0.005;
};

struct InstrumentationConfig {
  std::string latency_clock = "rdtsc";
  bool perf_enabled = true;
  bool energy_enabled = true;
};

struct Config {
  RunConfig run;
  ProblemConfig problem;
  StructureConfig structure;
  GeneratorConfig generator;
  SolverConfig solver;
  StreamConfig stream;
  InstrumentationConfig instrumentation;
};

}  // namespace spqs
