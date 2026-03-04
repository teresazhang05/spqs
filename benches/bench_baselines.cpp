#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <sys/utsname.h>
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "spqs/alloc_counter.hpp"
#include "spqs/arena.hpp"
#include "spqs/baselines.hpp"
#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/config_loader.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/fp_cert.hpp"
#include "spqs/logschema.hpp"
#include "spqs/projector.hpp"
#include "spqs/rhs.hpp"
#include "spqs/stream_gen.hpp"
#include "spqs/structure_validator.hpp"
#include "spqs/violator_oracle.hpp"

#ifndef SPQS_GIT_COMMIT
#define SPQS_GIT_COMMIT "unknown"
#endif

#ifndef SPQS_COMPILER_STR
#define SPQS_COMPILER_STR "unknown"
#endif

#ifndef SPQS_CFLAGS_STR
#define SPQS_CFLAGS_STR ""
#endif

namespace {

constexpr std::uint64_t kUnsetU64 = std::numeric_limits<std::uint64_t>::max();

struct CliOptions {
  std::string config_path = "configs/default_block256_debug.yaml";
  std::string out_dir_override;
  std::uint64_t warmup_ticks = kUnsetU64;
  std::uint64_t latency_ticks = kUnsetU64;
  std::uint64_t feas_ticks = kUnsetU64;
  std::uint64_t sample_ticks = 10000;
  std::uint64_t progress_every = 10000000;
  int g1_trials = 100000;
  int g1_max_updated_blocks = 4;
  int audit_stride = 10000;
  double g3_target_us = 50.0;
  double g4_abs_tol = 1e-10;
  double g4_rel_tol = 1e-9;
  bool claim_setting = false;
  bool allow_g4_skip = false;
};

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

int read_int_arg(int argc, char** argv, const std::string& key, int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return std::stoi(argv[i + 1]);
    }
  }
  return fallback;
}

double read_double_arg(int argc, char** argv, const std::string& key, double fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      return std::stod(argv[i + 1]);
    }
  }
  return fallback;
}

bool read_bool_arg(int argc, char** argv, const std::string& key, bool fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (key == argv[i]) {
      const std::string value = argv[i + 1];
      if (value == "1" || value == "true") {
        return true;
      }
      if (value == "0" || value == "false") {
        return false;
      }
      return fallback;
    }
  }
  return fallback;
}

bool bitwise_equal(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8U);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

#ifndef NDEBUG
std::vector<std::string> capture_backtrace_symbols(int max_frames) {
  std::vector<std::string> out;
#if defined(__linux__) || defined(__APPLE__)
  if (max_frames <= 0) {
    return out;
  }
  std::vector<void*> frames(static_cast<std::size_t>(max_frames), nullptr);
  const int n = ::backtrace(frames.data(), max_frames);
  if (n <= 0) {
    return out;
  }
  char** symbols = ::backtrace_symbols(frames.data(), n);
  if (symbols == nullptr) {
    return out;
  }
  out.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    out.emplace_back(symbols[i]);
  }
  std::free(symbols);
#else
  (void)max_frames;
#endif
  return out;
}
#endif

std::string detect_kernel_version() {
  struct utsname u {};
  if (uname(&u) == 0) {
    std::string out(u.sysname);
    out += " ";
    out += u.release;
    return out;
  }
  return "unknown";
}

std::string detect_cpu_model() {
#if defined(__linux__)
  std::ifstream in("/proc/cpuinfo");
  std::string line;
  while (std::getline(in, line)) {
    const std::string prefix = "model name";
    if (line.rfind(prefix, 0) == 0) {
      const std::size_t colon = line.find(':');
      if (colon != std::string::npos && colon + 1 < line.size()) {
        return line.substr(colon + 2);
      }
    }
  }
#elif defined(__APPLE__)
  char buf[256] = {};
  std::size_t len = sizeof(buf);
  if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) {
    return std::string(buf);
  }
#endif
  return "unknown";
}

bool sampled_tick(std::uint64_t tick, std::uint64_t total_ticks, std::uint64_t sample_ticks) {
  if (sample_ticks == 0) {
    return false;
  }
  if (sample_ticks >= total_ticks) {
    return true;
  }
  const std::uint64_t stride = std::max<std::uint64_t>(1, total_ticks / sample_ticks);
  return (tick % stride) == 0;
}

double histogram_quantile_int(const std::vector<std::uint64_t>& hist,
                              std::uint64_t count,
                              double q) {
  if (count == 0 || hist.empty()) {
    return 0.0;
  }
  const double clamped_q = std::min(1.0, std::max(0.0, q));
  const std::uint64_t rank = static_cast<std::uint64_t>(
      std::ceil(clamped_q * static_cast<double>(count)));
  std::uint64_t target = (rank == 0U) ? 1U : rank;
  std::uint64_t accum = 0;
  for (std::size_t i = 0; i < hist.size(); ++i) {
    accum += hist[i];
    if (accum >= target) {
      return static_cast<double>(i);
    }
  }
  return static_cast<double>(hist.size() - 1U);
}

struct PinResult {
  bool attempted = false;
  bool verified = false;
  int observed_cpu = -1;
  int error_number = 0;
};

PinResult maybe_pin_current_thread(int pin_cpu) {
  PinResult out;
#if defined(__linux__)
  if (pin_cpu < 0) {
    return out;
  }
  out.attempted = true;
  if (pin_cpu >= CPU_SETSIZE) {
    out.error_number = EINVAL;
    return out;
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(pin_cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    out.error_number = errno;
    return out;
  }

  bool ok = true;
  for (int i = 0; i < 8; ++i) {
    const int cpu = sched_getcpu();
    if (cpu < 0) {
      out.error_number = errno;
      ok = false;
      break;
    }
    out.observed_cpu = cpu;
    if (cpu != pin_cpu) {
      ok = false;
      break;
    }
  }
  out.verified = ok;
#else
  (void)pin_cpu;
#endif
  return out;
}

struct OracleGateResult {
  bool pass = true;
  double worst_mismatch = 0.0;
};

spqs::Violator brute_force_worst_inactive(const spqs::ConstraintsLocal& local,
                                          const spqs::ConstraintsGlobal& global,
                                          const spqs::RHSAll& rhs,
                                          double tau_abs_scale,
                                          const std::vector<int>& pos_of_id,
                                          const double* q_n) {
  spqs::Violator best;
  best.violation = -std::numeric_limits<double>::infinity();
  best.constraint_id = std::numeric_limits<int>::max();

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
      if (spqs::prefer_lhs(cand, best)) {
        best = cand;
      }
    }
  }

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
    if (spqs::prefer_lhs(cand, best)) {
      best = cand;
    }
  }

  return best;
}

OracleGateResult run_g1_oracle_gate(const spqs::ConstraintsLocal& local,
                                    const spqs::ConstraintsGlobal& global,
                                    const spqs::RHSAll& rhs,
                                    double tau_abs_scale,
                                    std::uint64_t seed,
                                    int trials,
                                    int max_updated_blocks) {
  spqs::ViolatorOracle oracle(&local, &global, tau_abs_scale);

  std::mt19937_64 rng(seed + 0x777ULL);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);

  std::vector<double> q(static_cast<std::size_t>(local.layout.n), 0.0);
  for (double& x : q) {
    x = ud(rng);
  }

  const int total_constraints = local.total_rows() + global.m_global;
  const int max_active = std::min(32, std::max(1, total_constraints / 4));
  std::uniform_int_distribution<int> active_dist(0, std::min(max_active, total_constraints));
  std::vector<int> all_ids(static_cast<std::size_t>(total_constraints), 0);
  std::vector<int> pos_of_id(static_cast<std::size_t>(total_constraints), -1);
  for (int i = 0; i < total_constraints; ++i) {
    all_ids[static_cast<std::size_t>(i)] = i;
  }

  std::shuffle(all_ids.begin(), all_ids.end(), rng);
  const int k_active = active_dist(rng);
  for (int i = 0; i < k_active; ++i) {
    pos_of_id[static_cast<std::size_t>(all_ids[static_cast<std::size_t>(i)])] = i;
  }
  oracle.set_active_from_pos_of_id(pos_of_id.data(), total_constraints);
  oracle.init_tick(q.data(), rhs);
  spqs::Violator v_oracle = oracle.max_violation_inactive();
  spqs::Violator v_brute = brute_force_worst_inactive(
      local, global, rhs, tau_abs_scale, pos_of_id, q.data());

  OracleGateResult out;
  if (!(v_oracle.constraint_id == v_brute.constraint_id &&
        bitwise_equal(v_oracle.violation, v_brute.violation))) {
    out.pass = false;
    out.worst_mismatch = std::max(out.worst_mismatch, std::abs(v_oracle.violation - v_brute.violation));
  }

  const spqs::OracleStats init_stats = oracle.last_stats();
  if (init_stats.local_blocks_recomputed != local.layout.B ||
      init_stats.global_rows_scanned != global.m_global) {
    out.pass = false;
  }

  std::vector<int> block_ids(static_cast<std::size_t>(local.layout.B), 0);
  for (int r = 0; r < local.layout.B; ++r) {
    block_ids[static_cast<std::size_t>(r)] = r;
  }
  std::uniform_int_distribution<int> h_dist(0, std::min(max_updated_blocks, local.layout.B));

  for (int t = 0; t < trials; ++t) {
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
    v_oracle = oracle.max_violation_inactive();
    v_brute = brute_force_worst_inactive(local, global, rhs, tau_abs_scale, pos_of_id, q.data());
    if (!(v_oracle.constraint_id == v_brute.constraint_id &&
          bitwise_equal(v_oracle.violation, v_brute.violation))) {
      out.pass = false;
      out.worst_mismatch = std::max(out.worst_mismatch, std::abs(v_oracle.violation - v_brute.violation));
      break;
    }

    const spqs::OracleStats s = oracle.last_stats();
    if (s.local_blocks_recomputed != static_cast<int>(changed.size()) ||
        s.global_rows_scanned != global.m_global) {
      out.pass = false;
      break;
    }
  }

  return out;
}

}  // namespace

int main(int argc, char** argv) {
  CliOptions cli;
  cli.config_path = read_arg(argc, argv, "--config", cli.config_path);
  cli.out_dir_override = read_arg(argc, argv, "--out-dir", "");
  cli.warmup_ticks = read_u64_arg(argc, argv, "--warmup-ticks", cli.warmup_ticks);
  cli.latency_ticks = read_u64_arg(argc, argv, "--latency-ticks", cli.latency_ticks);
  cli.feas_ticks = read_u64_arg(argc, argv, "--feas-ticks", cli.feas_ticks);
  cli.sample_ticks = read_u64_arg(argc, argv, "--sample-ticks", cli.sample_ticks);
  cli.progress_every = read_u64_arg(argc, argv, "--progress-every", cli.progress_every);
  cli.g1_trials = read_int_arg(argc, argv, "--g1-trials", cli.g1_trials);
  cli.g1_max_updated_blocks = read_int_arg(argc, argv, "--g1-max-updated-blocks", cli.g1_max_updated_blocks);
  cli.audit_stride = read_int_arg(argc, argv, "--audit-stride", cli.audit_stride);
  cli.g3_target_us = read_double_arg(argc, argv, "--g3-target-us", cli.g3_target_us);
  cli.g4_abs_tol = read_double_arg(argc, argv, "--g4-abs-tol", cli.g4_abs_tol);
  cli.g4_rel_tol = read_double_arg(argc, argv, "--g4-rel-tol", cli.g4_rel_tol);
  cli.claim_setting = read_bool_arg(argc, argv, "--claim-setting", cli.claim_setting);
  cli.allow_g4_skip = read_bool_arg(argc, argv, "--allow-g4-skip", cli.allow_g4_skip);

  spqs::Config cfg;
  std::string err;
  if (!spqs::load_config(cli.config_path, &cfg, &err)) {
    std::cerr << "bench_baselines failed to load config: " << err << "\n";
    return 2;
  }

  const spqs::StructureValidationResult valid = spqs::validate_structure_from_config(cfg);
  const spqs::StreamMode stream_mode = spqs::parse_stream_mode_or_default(cfg.stream.mode);

  if (cli.claim_setting) {
#if !defined(__linux__)
    std::cerr << "bench_baselines claim-setting requires Linux x86_64 environment\n";
    return 8;
#endif
#if !defined(__x86_64__) && !defined(_M_X64)
    std::cerr << "bench_baselines claim-setting requires x86_64 CPU architecture\n";
    return 8;
#endif
    if (!valid.structure_valid) {
      std::cerr << "bench_baselines claim-setting requires structure_valid=true\n";
      return 8;
    }
    if (stream_mode == spqs::StreamMode::IID_DENSE) {
      std::cerr << "bench_baselines claim-setting rejects stream.mode=iid_dense\n";
      return 8;
    }
#if !SPQS_ENABLE_OSQP
    if (!cli.allow_g4_skip) {
      std::cerr << "bench_baselines claim-setting requires OSQP-enabled build\n";
      return 8;
    }
#endif
  }

  const PinResult pin = maybe_pin_current_thread(cfg.run.pin_cpu);
  if (cli.claim_setting && (!pin.attempted || !pin.verified)) {
    std::cerr << "bench_baselines claim-setting requires verified CPU pinning; "
              << "pin_attempted=" << (pin.attempted ? "true" : "false")
              << " "
              << "pin_cpu=" << cfg.run.pin_cpu
              << " observed_cpu=" << pin.observed_cpu
              << " errno=" << pin.error_number << "\n";
    return 8;
  }

  const std::uint64_t warmup_ticks =
      (cli.warmup_ticks == kUnsetU64) ? cfg.stream.T_warmup : cli.warmup_ticks;
  const std::uint64_t latency_ticks =
      (cli.latency_ticks == kUnsetU64) ? cfg.stream.T_latency : cli.latency_ticks;
  const std::uint64_t feas_ticks =
      (cli.feas_ticks == kUnsetU64) ? cfg.stream.T_feas : cli.feas_ticks;

  const std::string out_dir = cli.out_dir_override.empty() ? cfg.run.out_dir : cli.out_dir_override;
  std::filesystem::create_directories(out_dir);

  std::vector<int> block_sizes = cfg.structure.block_sizes;
  if (!valid.structure_valid) {
    block_sizes.assign(1, cfg.problem.n);
  }

  spqs::BlockLayout layout;
  if (!spqs::make_block_layout(cfg.problem.n, block_sizes, &layout, &err)) {
    std::cerr << "bench_baselines failed to build layout: " << err << "\n";
    return 3;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(
      layout, cfg.structure.m_local_per_block, cfg.run.seed);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(
      cfg.problem.n, cfg.structure.m_global, cfg.generator.factors, cfg.run.seed + 7U);
  const spqs::RHSAll rhs = spqs::generate_rhs(
      local, global, cfg.generator.b_margin, cfg.generator.b_noise_std, cfg.run.seed + 19U);

  spqs::BruteForceScan scan(&local, &global, cfg.solver.tau_abs_scale);

  spqs::Arena arena(1U << 22U);
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

  spqs::Arena arena_dense(1U << 22U);
  spqs::ViolatorOracle oracle_dense(&local, &global, cfg.solver.tau_abs_scale);
  spqs::StreamingProjector dense_reference(&local, &global, &oracle_dense, &arena_dense);
  spqs::ProjectorOptions opts_dense = opts;
  opts_dense.force_full_rescan = true;
  dense_reference.set_options(opts_dense);
  dense_reference.set_rhs(rhs);

  spqs::CorrelatedBlockSparseStream stream(cfg.stream, layout);
  std::vector<int> stream_changed_blocks;
  stream_changed_blocks.reserve(static_cast<std::size_t>(layout.B));
  spqs::StreamTickInfo stream_tick_info;

  std::vector<double> q_prop(static_cast<std::size_t>(cfg.problem.n), 0.0);
  std::vector<double> q_out(static_cast<std::size_t>(cfg.problem.n), 0.0);
  std::vector<double> q_ref(static_cast<std::size_t>(cfg.problem.n), 0.0);
  std::vector<double> q_ray(static_cast<std::size_t>(cfg.problem.n), 0.0);

  const auto run_t0 = std::chrono::steady_clock::now();
  auto maybe_print_progress = [&](const char* phase, std::uint64_t done, std::uint64_t total) {
    if (cli.progress_every == 0 || total == 0) {
      return;
    }
    if (done == 0 || done == total || (done % cli.progress_every) == 0) {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed_s =
          std::chrono::duration_cast<std::chrono::duration<double>>(now - run_t0).count();
      const double pct = (100.0 * static_cast<double>(done)) / static_cast<double>(total);
      std::cout << "progress phase=" << phase
                << " done=" << done
                << "/" << total
                << " pct=" << pct
                << " elapsed_s=" << elapsed_s
                << "\n"
                << std::flush;
    }
  };

  spqs::alloc_counter_reset();
  spqs::alloc_counter_set_enabled(true);

  maybe_print_progress("warmup", 0, warmup_ticks);
  for (std::uint64_t t = 0; t < warmup_ticks; ++t) {
    stream.next(t, q_prop.data(), &stream_changed_blocks, &stream_tick_info);
    (void)projector.project(q_prop.data(), q_out.data());
    maybe_print_progress("warmup", t + 1, warmup_ticks);
  }
  const spqs::AllocCounters alloc_after_warmup = spqs::alloc_counter_snapshot();

  spqs::GateSummary gates;
  gates.structure_valid = valid.structure_valid;
  gates.claim_setting = cli.claim_setting;
  gates.mode = (valid.mode == spqs::RunMode::STRUCTURED_FAST)
                   ? (cli.claim_setting ? "STRUCTURED_FAST_CLAIM" : "STRUCTURED_FAST_DEV")
                   : "BRUTEFORCE_ONLY";
  gates.pinning_enforced = pin.attempted;
  gates.pinning_verified = (!pin.attempted) ? true : pin.verified;
  gates.pin_cpu_observed = pin.observed_cpu;
  gates.G2_worst_violation = -std::numeric_limits<double>::infinity();
  gates.g4_reference_backend = "INTERNAL_DENSE";

  std::vector<std::uint64_t> latency_ns;
  latency_ns.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(latency_ticks, 5000000ULL)));
  std::vector<spqs::TickSampleRow> samples;
  samples.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(cli.sample_ticks, 20000ULL)));

  std::uint64_t fallback_count = 0;
  std::uint64_t alloc_calls_during_project_loop = 0;
  std::uint64_t bytes_allocated_during_project_loop = 0;
  double rayscale_gap_abs_max = 0.0;
  std::uint64_t compared = 0;
  std::uint64_t stream_outside_count = 0;
  double stream_outside_changed_sum = 0.0;
  std::vector<std::uint64_t> stream_outside_changed_hist(
      static_cast<std::size_t>(layout.B + 1), 0U);
  int stream_inside_max_changed_blocks = 0;
  bool stream_inside_limit_ok = true;
  const int stream_inside_limit = std::max(cfg.stream.burst.K_burst, cfg.stream.burst.K_jump_in_burst);

  bool alloc_trap_triggered = false;
  std::string alloc_trap_phase;
  std::uint64_t alloc_trap_tick = 0;
  std::uint64_t alloc_trap_delta_calls = 0;
  std::uint64_t alloc_trap_delta_bytes = 0;
  std::vector<std::string> alloc_trap_backtrace;

  spqs::OsqpBaselineParams osqp_params{};
  osqp_params.polish = cli.claim_setting;

  maybe_print_progress("latency", 0, latency_ticks);
  for (std::uint64_t t = 0; t < latency_ticks; ++t) {
    const std::uint64_t global_tick = warmup_ticks + t;
    stream.next(global_tick, q_prop.data(), &stream_changed_blocks, &stream_tick_info);
    if (stream_tick_info.in_burst) {
      stream_inside_max_changed_blocks =
          std::max(stream_inside_max_changed_blocks, stream_tick_info.changed_blocks_count);
      if (stream_tick_info.changed_blocks_count > stream_inside_limit) {
        stream_inside_limit_ok = false;
      }
    } else {
      ++stream_outside_count;
      stream_outside_changed_sum += static_cast<double>(stream_tick_info.changed_blocks_count);
      const int clamped = std::max(0, std::min(layout.B, stream_tick_info.changed_blocks_count));
      ++stream_outside_changed_hist[static_cast<std::size_t>(clamped)];
    }

    const spqs::AllocCounters alloc_before_project = spqs::alloc_counter_snapshot();
    const auto t0 = std::chrono::steady_clock::now();
    const spqs::SolverStats st = projector.project(q_prop.data(), q_out.data());
    const auto t1 = std::chrono::steady_clock::now();
    const spqs::AllocCounters alloc_after_project = spqs::alloc_counter_snapshot();
    const std::uint64_t alloc_before_calls = alloc_before_project.alloc_calls_total();
    const std::uint64_t alloc_after_calls = alloc_after_project.alloc_calls_total();
    if (alloc_after_calls >= alloc_before_calls) {
      const std::uint64_t delta_calls = (alloc_after_calls - alloc_before_calls);
      alloc_calls_during_project_loop += delta_calls;
      if (!alloc_trap_triggered && delta_calls > 0U) {
        alloc_trap_triggered = true;
        alloc_trap_phase = "latency";
        alloc_trap_tick = t;
        alloc_trap_delta_calls = delta_calls;
#ifndef NDEBUG
        alloc_trap_backtrace = capture_backtrace_symbols(32);
#endif
      }
    }
    if (alloc_after_project.bytes_allocated >= alloc_before_project.bytes_allocated) {
      const std::uint64_t delta_bytes =
          (alloc_after_project.bytes_allocated - alloc_before_project.bytes_allocated);
      bytes_allocated_during_project_loop += delta_bytes;
      if (alloc_trap_triggered && alloc_trap_delta_bytes == 0U && delta_bytes > 0U) {
        alloc_trap_delta_bytes = delta_bytes;
      }
    }
    const std::uint64_t dt_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    latency_ns.push_back(dt_ns);

    if (st.fallback_used) {
      ++fallback_count;
    }

    const spqs::Violator out_v = scan.max_violation(q_out.data(), rhs);
    gates.G2_worst_violation = std::max(gates.G2_worst_violation, out_v.violation);
    if (out_v.violation > 0.0) {
      ++gates.G2_feas_violations;
    }

    if (cli.audit_stride > 0 && (t % static_cast<std::uint64_t>(cli.audit_stride) == 0)) {
      if (!scan.audit_feasible_long_double(q_out.data(), rhs)) {
        ++gates.G2_audit_failures;
      }
    }

    double g4_gap_abs = 0.0;
    double g4_gap_rel = 0.0;
    if (sampled_tick(t, latency_ticks, cli.sample_ticks) && !st.fallback_used) {
      const double f_ours = spqs::objective_half_l2_sq(q_out.data(), q_prop.data(), cfg.problem.n);

      spqs::BaselineSolveResult ref_res = spqs::solve_osqp_projection_baseline(
          local, global, rhs, q_prop.data(), osqp_params, q_ref.data());
      if (ref_res.available && ref_res.success) {
        gates.osqp_available = true;
        gates.g4_reference_backend = "OSQP";
      } else {
        ref_res.backend = "INTERNAL_DENSE";
        ref_res.available = true;
        const spqs::SolverStats dense_stats = dense_reference.project(q_prop.data(), q_ref.data());
        ref_res.success = !dense_stats.fallback_used && (dense_stats.max_violation_certified <= 0.0);
      }

      if (ref_res.available && ref_res.success) {
        const double f_ref = spqs::objective_half_l2_sq(q_ref.data(), q_prop.data(), cfg.problem.n);
        g4_gap_abs = std::abs(f_ours - f_ref);
        g4_gap_rel = g4_gap_abs / std::max(1.0, std::abs(f_ref));
        gates.G4_obj_gap_abs_max = std::max(gates.G4_obj_gap_abs_max, g4_gap_abs);
        gates.G4_obj_gap_rel_max = std::max(gates.G4_obj_gap_rel_max, g4_gap_rel);
        ++compared;
      }

      const spqs::BaselineSolveResult ray = spqs::solve_rayscale_baseline(
          local, global, rhs, q_prop.data(), cfg.solver.tau_abs_scale, q_ray.data());
      if (ray.available && ray.success) {
        const double f_ray = spqs::objective_half_l2_sq(q_ray.data(), q_prop.data(), cfg.problem.n);
        rayscale_gap_abs_max = std::max(rayscale_gap_abs_max, std::abs(f_ours - f_ray));
      }
    }

    if (sampled_tick(t, latency_ticks, cli.sample_ticks)) {
      spqs::TickSampleRow row;
      row.tick = t;
      row.latency_ns = dt_ns;
      row.iters = st.iters;
      row.adds = st.adds;
      row.removes = st.removes;
      row.active_size_final = st.active_size_final;
      row.touched_blocks_per_iter_max = st.touched_blocks_per_iter_max;
      row.fallback_used = st.fallback_used;
      row.kappa_valid = st.kappa_valid;
      row.min_kappa = st.min_kappa;
      row.tau_required = st.tau_required;
      row.tau_shrink_used = st.tau_shrink_used;
      row.fallback_alpha = st.fallback_alpha;
      row.max_violation_certified = st.max_violation_certified;
      row.min_slack_certified = st.min_slack_certified;
      row.changed_blocks_count = st.tick_changed_blocks_count;
      row.oracle_local_blocks_recomputed = st.tick_local_blocks_recomputed;
      row.stream_in_burst = stream_tick_info.in_burst;
      row.stream_delta_l2 = stream_tick_info.delta_l2;
      row.g4_obj_gap_abs = g4_gap_abs;
      row.g4_obj_gap_rel = g4_gap_rel;
      samples.push_back(row);
    }
    maybe_print_progress("latency", t + 1, latency_ticks);
  }

  maybe_print_progress("feas", latency_ticks, feas_ticks);
  for (std::uint64_t t = latency_ticks; t < feas_ticks; ++t) {
    const std::uint64_t global_tick = warmup_ticks + t;
    stream.next(global_tick, q_prop.data(), &stream_changed_blocks, &stream_tick_info);
    if (stream_tick_info.in_burst) {
      stream_inside_max_changed_blocks =
          std::max(stream_inside_max_changed_blocks, stream_tick_info.changed_blocks_count);
      if (stream_tick_info.changed_blocks_count > stream_inside_limit) {
        stream_inside_limit_ok = false;
      }
    } else {
      ++stream_outside_count;
      stream_outside_changed_sum += static_cast<double>(stream_tick_info.changed_blocks_count);
      const int clamped = std::max(0, std::min(layout.B, stream_tick_info.changed_blocks_count));
      ++stream_outside_changed_hist[static_cast<std::size_t>(clamped)];
    }

    const spqs::AllocCounters alloc_before_project = spqs::alloc_counter_snapshot();
    const spqs::SolverStats st = projector.project(q_prop.data(), q_out.data());
    const spqs::AllocCounters alloc_after_project = spqs::alloc_counter_snapshot();
    const std::uint64_t alloc_before_calls = alloc_before_project.alloc_calls_total();
    const std::uint64_t alloc_after_calls = alloc_after_project.alloc_calls_total();
    if (alloc_after_calls >= alloc_before_calls) {
      const std::uint64_t delta_calls = (alloc_after_calls - alloc_before_calls);
      alloc_calls_during_project_loop += delta_calls;
      if (!alloc_trap_triggered && delta_calls > 0U) {
        alloc_trap_triggered = true;
        alloc_trap_phase = "feas";
        alloc_trap_tick = t;
        alloc_trap_delta_calls = delta_calls;
#ifndef NDEBUG
        alloc_trap_backtrace = capture_backtrace_symbols(32);
#endif
      }
    }
    if (alloc_after_project.bytes_allocated >= alloc_before_project.bytes_allocated) {
      const std::uint64_t delta_bytes =
          (alloc_after_project.bytes_allocated - alloc_before_project.bytes_allocated);
      bytes_allocated_during_project_loop += delta_bytes;
      if (alloc_trap_triggered && alloc_trap_delta_bytes == 0U && delta_bytes > 0U) {
        alloc_trap_delta_bytes = delta_bytes;
      }
    }
    if (st.fallback_used) {
      ++fallback_count;
    }
    const spqs::Violator out_v = scan.max_violation(q_out.data(), rhs);
    gates.G2_worst_violation = std::max(gates.G2_worst_violation, out_v.violation);
    if (out_v.violation > 0.0) {
      ++gates.G2_feas_violations;
    }
    if (cli.audit_stride > 0 && (t % static_cast<std::uint64_t>(cli.audit_stride) == 0)) {
      if (!scan.audit_feasible_long_double(q_out.data(), rhs)) {
        ++gates.G2_audit_failures;
      }
    }
    maybe_print_progress("feas", t + 1, feas_ticks);
  }
  const spqs::AllocCounters alloc_after_measured = spqs::alloc_counter_snapshot();
  spqs::alloc_counter_set_enabled(false);
  const std::uint64_t alloc_calls_total_after_warmup =
      (alloc_after_measured.alloc_calls_total() >= alloc_after_warmup.alloc_calls_total())
          ? (alloc_after_measured.alloc_calls_total() - alloc_after_warmup.alloc_calls_total())
          : 0;
  const std::uint64_t alloc_bytes_total_after_warmup =
      (alloc_after_measured.bytes_allocated >= alloc_after_warmup.bytes_allocated)
          ? (alloc_after_measured.bytes_allocated - alloc_after_warmup.bytes_allocated)
          : 0;

  if (!std::isfinite(gates.G2_worst_violation)) {
    gates.G2_worst_violation = 0.0;
  }

  const double stream_outside_mean =
      (stream_outside_count == 0U)
          ? 0.0
          : (stream_outside_changed_sum / static_cast<double>(stream_outside_count));
  const double stream_outside_p99_9 =
      histogram_quantile_int(stream_outside_changed_hist, stream_outside_count, 0.999);
  gates.G_stream_outside_mean_changed_blocks = stream_outside_mean;
  gates.G_stream_outside_p99_9_changed_blocks = stream_outside_p99_9;
  gates.G_stream_inside_max_changed_blocks = stream_inside_max_changed_blocks;
  if (stream_mode == spqs::StreamMode::IID_DENSE) {
    gates.G_stream_pass = false;
  } else {
    gates.G_stream_pass =
        (stream_outside_mean <= 1.2) &&
        (stream_outside_p99_9 <= 4.0) &&
        stream_inside_limit_ok;
  }

  gates.oracle_init_tick_calls = oracle.total_init_tick_calls();
  gates.oracle_active_clears = oracle.total_active_clears();
  if (valid.mode == spqs::RunMode::STRUCTURED_FAST && !opts.force_full_rescan) {
    gates.G6_pass = (gates.oracle_init_tick_calls == 1U) &&
                    (gates.oracle_active_clears == 0U);
  } else {
    gates.G6_pass = true;
  }

  gates.alloc_calls_during_loop = alloc_calls_during_project_loop;
  gates.bytes_allocated_during_loop = bytes_allocated_during_project_loop;
  gates.G5_pass = (gates.alloc_calls_during_loop == 0U) &&
                  (gates.bytes_allocated_during_loop == 0U);

  const OracleGateResult g1 = run_g1_oracle_gate(
      local,
      global,
      rhs,
      cfg.solver.tau_abs_scale,
      cfg.run.seed + 333U,
      cli.g1_trials,
      cli.g1_max_updated_blocks);
  gates.G1_oracle_match = g1.pass;
  gates.G1_worst_mismatch = g1.worst_mismatch;

  const spqs::LatencySummary lat = spqs::summarize_latency_us(latency_ns);
  gates.G3_p99_99_us = lat.p99_99_us;
  gates.G3_pass = valid.structure_valid && (lat.p99_99_us <= cli.g3_target_us);

  gates.compared_samples = compared;
  if (gates.osqp_available) {
    const bool g4_ok = (compared > 0) &&
                       (gates.G4_obj_gap_abs_max <= cli.g4_abs_tol) &&
                       (gates.G4_obj_gap_rel_max <= cli.g4_rel_tol) &&
                       (gates.G2_feas_violations == 0);
    gates.G4_status = g4_ok ? "PASS" : "FAIL";
    gates.G4_pass = g4_ok;
  } else {
    gates.G4_status = "SKIPPED";
    gates.G4_pass = cli.allow_g4_skip;
    if (gates.g4_reference_backend == "INTERNAL_DENSE") {
      gates.g4_reference_backend = "INTERNAL_DENSE_DIAGNOSTIC";
    }
  }

  spqs::SummaryPayload summary;
  summary.git_commit = SPQS_GIT_COMMIT;
  summary.compiler = SPQS_COMPILER_STR;
  summary.cflags = SPQS_CFLAGS_STR;
  summary.cpu_model = detect_cpu_model();
  summary.kernel_version = detect_kernel_version();
  summary.turbo_disabled = cfg.run.disable_turbo;
  summary.smt_disabled = cfg.run.disable_smt;
  summary.pin_cpu = cfg.run.pin_cpu;
  summary.claim_setting = cli.claim_setting;
  summary.pinning_enforced = pin.attempted;
  summary.pinning_verified = (!pin.attempted) ? true : pin.verified;
  summary.pin_cpu_observed = pin.observed_cpu;
  summary.config = cfg;
  summary.latency = lat;
  summary.warmup_ticks = warmup_ticks;
  summary.latency_ticks = latency_ticks;
  summary.feas_ticks = feas_ticks;
  summary.fallback_count = fallback_count;
  summary.fallback_rate =
      (feas_ticks == 0U) ? 0.0 : (static_cast<double>(fallback_count) / static_cast<double>(feas_ticks));
  summary.alloc_calls_during_loop = gates.alloc_calls_during_loop;
  summary.bytes_allocated_during_loop = gates.bytes_allocated_during_loop;
  summary.stream_outside_mean_changed_blocks = stream_outside_mean;
  summary.stream_outside_p99_9_changed_blocks = stream_outside_p99_9;
  summary.stream_inside_max_changed_blocks = stream_inside_max_changed_blocks;

  std::string write_err;
  const std::string summary_path = out_dir + "/summary.json";
  const std::string gates_path = out_dir + "/gates.json";
  const std::string samples_path = out_dir + "/tick_samples.csv";
  const std::string latency_path = out_dir + "/latency.hdr";

  if (!spqs::write_summary_json(summary_path, summary, &write_err)) {
    std::cerr << "failed to write summary.json: " << write_err << "\n";
    return 4;
  }
  if (!spqs::write_gates_json(gates_path, gates, &write_err)) {
    std::cerr << "failed to write gates.json: " << write_err << "\n";
    return 5;
  }
  if (!spqs::write_tick_samples_csv(samples_path, samples, &write_err)) {
    std::cerr << "failed to write tick_samples.csv: " << write_err << "\n";
    return 6;
  }
  if (!spqs::write_latency_hdr(latency_path, latency_ns, lat, &write_err)) {
    std::cerr << "failed to write latency.hdr: " << write_err << "\n";
    return 7;
  }

  if (alloc_trap_triggered) {
    std::ofstream alloc_trace(out_dir + "/alloc_trace.json");
    if (alloc_trace.is_open()) {
      alloc_trace << std::setprecision(17);
      alloc_trace << "{\n";
      alloc_trace << "  \"schema_version\": \"spqs.alloc_trace.v1\",\n";
      alloc_trace << "  \"phase\": \"" << alloc_trap_phase << "\",\n";
      alloc_trace << "  \"tick\": " << alloc_trap_tick << ",\n";
      alloc_trace << "  \"delta_calls\": " << alloc_trap_delta_calls << ",\n";
      alloc_trace << "  \"delta_bytes\": " << alloc_trap_delta_bytes << ",\n";
      alloc_trace << "  \"backtrace\": [";
      for (std::size_t i = 0; i < alloc_trap_backtrace.size(); ++i) {
        if (i > 0) {
          alloc_trace << ',';
        }
        alloc_trace << "\n    \"" << json_escape(alloc_trap_backtrace[i]) << "\"";
      }
      if (!alloc_trap_backtrace.empty()) {
        alloc_trace << "\n  ";
      }
      alloc_trace << "]\n";
      alloc_trace << "}\n";
    }
  }

  std::cout << "run_name=" << cfg.run.run_name << "\n";
  std::cout << "mode=" << gates.mode << "\n";
  std::cout << "structure_valid=" << (gates.structure_valid ? "true" : "false") << "\n";
  std::cout << "G_stream_pass=" << (gates.G_stream_pass ? "true" : "false")
            << " outside_mean=" << gates.G_stream_outside_mean_changed_blocks
            << " outside_p99_9=" << gates.G_stream_outside_p99_9_changed_blocks
            << " inside_max=" << gates.G_stream_inside_max_changed_blocks << "\n";
  std::cout << "G1_oracle_match=" << (gates.G1_oracle_match ? "true" : "false")
            << " worst_mismatch=" << gates.G1_worst_mismatch << "\n";
  std::cout << "G2_feas_violations=" << gates.G2_feas_violations
            << " G2_worst_violation=" << gates.G2_worst_violation
            << " G2_audit_failures=" << gates.G2_audit_failures << "\n";
  std::cout << "G3_p99_99_us=" << gates.G3_p99_99_us
            << " G3_pass=" << (gates.G3_pass ? "true" : "false") << "\n";
  std::cout << "G5_pass=" << (gates.G5_pass ? "true" : "false")
            << " alloc_calls_during_loop=" << gates.alloc_calls_during_loop
            << " bytes_allocated_during_loop=" << gates.bytes_allocated_during_loop << "\n";
  std::cout << "G6_pass=" << (gates.G6_pass ? "true" : "false")
            << " oracle_init_tick_calls=" << gates.oracle_init_tick_calls
            << " oracle_active_clears=" << gates.oracle_active_clears << "\n";
  std::cout << "G4_backend=" << gates.g4_reference_backend
            << " osqp_available=" << (gates.osqp_available ? "true" : "false")
            << " compared_samples=" << gates.compared_samples
            << " G4_abs_max=" << gates.G4_obj_gap_abs_max
            << " G4_rel_max=" << gates.G4_obj_gap_rel_max
            << " G4_status=" << gates.G4_status
            << " G4_pass=" << (gates.G4_pass ? "true" : "false") << "\n";
  std::cout << "pinning_enforced=" << (gates.pinning_enforced ? "true" : "false")
            << " pinning_verified=" << (gates.pinning_verified ? "true" : "false")
            << " pin_cpu_observed=" << gates.pin_cpu_observed << "\n";
  std::cout << "alloc_total_after_warmup_calls=" << alloc_calls_total_after_warmup
            << " alloc_total_after_warmup_bytes=" << alloc_bytes_total_after_warmup << "\n";
  std::cout << "fallback_count=" << fallback_count
            << " fallback_rate="
            << ((feas_ticks == 0U) ? 0.0 : (static_cast<double>(fallback_count) / static_cast<double>(feas_ticks)))
            << " rayscale_gap_abs_max=" << rayscale_gap_abs_max << "\n";
  std::cout << "artifacts=" << out_dir << "\n";
  std::cout << "bench_baselines: OK\n";

  return 0;
}
