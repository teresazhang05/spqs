#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/arena.hpp"
#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/config_loader.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/logschema.hpp"
#include "spqs/projector.hpp"
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

double objective(const std::vector<double>& q, const std::vector<double>& q_prop) {
  double s = 0.0;
  for (std::size_t i = 0; i < q.size(); ++i) {
    const double d = q[i] - q_prop[i];
    s += d * d;
  }
  return 0.5 * s;
}

bool test_dense_reference_matches_structured() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(32, {8, 8, 8, 8}, &layout, &why)) {
    std::cerr << "FAIL: layout build failed: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 8, 5101);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(32, 4, 2, 5102);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 4.0, 0.1, 5103);
  spqs::BruteForceScan scan(&local, &global, 8.0);

  spqs::Arena arena_fast(1U << 20U);
  spqs::ViolatorOracle oracle_fast(&local, &global, 8.0);
  spqs::StreamingProjector fast(&local, &global, &oracle_fast, &arena_fast);

  spqs::Arena arena_dense(1U << 20U);
  spqs::ViolatorOracle oracle_dense(&local, &global, 8.0);
  spqs::StreamingProjector dense(&local, &global, &oracle_dense, &arena_dense);

  spqs::ProjectorOptions opts;
  opts.a_max = 64;
  opts.I_max = 200;
  opts.strict_interior = false;
  opts.warm_start = true;
  fast.set_options(opts);
  fast.set_rhs(rhs);

  opts.warm_start = false;
  opts.force_full_rescan = true;
  dense.set_options(opts);
  dense.set_rhs(rhs);

  std::mt19937_64 rng(5104);
  std::uniform_real_distribution<double> ud(-5.0, 5.0);

  std::vector<double> q_prop(32, 0.0);
  std::vector<double> q_fast(32, 0.0);
  std::vector<double> q_dense(32, 0.0);

  for (int t = 0; t < 250; ++t) {
    for (double& v : q_prop) {
      v = ud(rng);
    }
    (void)fast.project(q_prop.data(), q_fast.data());
    (void)dense.project(q_prop.data(), q_dense.data());

    if (!check(scan.certified_feasible(q_fast.data(), rhs),
               "structured projector output must remain certified feasible")) {
      return false;
    }
    if (!check(scan.certified_feasible(q_dense.data(), rhs),
               "dense reference output must remain certified feasible")) {
      return false;
    }

    const double f_fast = objective(q_fast, q_prop);
    const double f_dense = objective(q_dense, q_prop);
    const double abs_gap = std::abs(f_fast - f_dense);
    const double rel_gap = abs_gap / std::max(1.0, std::abs(f_dense));
    if (!check(abs_gap <= 1e-8 || rel_gap <= 1e-8,
               "dense reference objective should match structured mode")) {
      std::cerr << "debug: f_fast=" << f_fast
                << " f_dense=" << f_dense
                << " abs_gap=" << abs_gap
                << " rel_gap=" << rel_gap << "\n";
      return false;
    }
  }

  return true;
}

bool test_logschema_writers() {
  const std::filesystem::path out_dir =
      std::filesystem::temp_directory_path() / "spqs_stage6_test";
  std::filesystem::create_directories(out_dir);

  spqs::GateSummary gates;
  gates.G1_oracle_match = true;
  gates.G1_worst_mismatch = 0.0;
  gates.G2_feas_violations = 0;
  gates.G2_worst_violation = -1.0;
  gates.G3_p99_99_us = 42.0;
  gates.G3_pass = true;
  gates.G4_obj_gap_abs_max = 1e-12;
  gates.G4_obj_gap_rel_max = 1e-11;
  gates.G4_status = "PASS";
  gates.G4_pass = true;
  gates.G5_pass = true;
  gates.alloc_calls_during_loop = 0;
  gates.bytes_allocated_during_loop = 0;
  gates.structure_valid = true;
  gates.mode = "STRUCTURED_FAST_DEV";
  gates.claim_setting = false;
  gates.g4_reference_backend = "INTERNAL_DENSE";
  gates.compared_samples = 10;
  gates.pinning_enforced = false;
  gates.pinning_verified = true;
  gates.pin_cpu_observed = -1;

  spqs::SummaryPayload summary;
  summary.git_commit = "test";
  summary.compiler = "clang";
  summary.cflags = "-O3";
  summary.cpu_model = "cpu";
  summary.kernel_version = "kernel";
  summary.turbo_disabled = true;
  summary.smt_disabled = true;
  summary.pin_cpu = 2;
  summary.claim_setting = false;
  summary.pinning_enforced = false;
  summary.pinning_verified = true;
  summary.pin_cpu_observed = -1;
  summary.config = spqs::default_config();
  summary.warmup_ticks = 1;
  summary.latency_ticks = 2;
  summary.feas_ticks = 3;
  summary.fallback_count = 0;
  summary.alloc_calls_during_loop = 0;
  summary.bytes_allocated_during_loop = 0;

  const std::vector<std::uint64_t> latency_ns = {1000, 2000, 3000, 4000};
  summary.latency = spqs::summarize_latency_us(latency_ns);

  std::vector<spqs::TickSampleRow> rows(1);
  rows[0].tick = 7;
  rows[0].latency_ns = 1234;
  rows[0].iters = 2;
  rows[0].g4_obj_gap_abs = 1e-12;
  rows[0].g4_obj_gap_rel = 1e-11;

  std::string err;
  const std::filesystem::path summary_path = out_dir / "summary.json";
  const std::filesystem::path gates_path = out_dir / "gates.json";
  const std::filesystem::path ticks_path = out_dir / "tick_samples.csv";
  const std::filesystem::path hdr_path = out_dir / "latency.hdr";

  if (!check(spqs::write_summary_json(summary_path.string(), summary, &err),
             "write_summary_json failed")) {
    std::cerr << "error=" << err << "\n";
    return false;
  }
  if (!check(spqs::write_gates_json(gates_path.string(), gates, &err),
             "write_gates_json failed")) {
    std::cerr << "error=" << err << "\n";
    return false;
  }
  if (!check(spqs::write_tick_samples_csv(ticks_path.string(), rows, &err),
             "write_tick_samples_csv failed")) {
    std::cerr << "error=" << err << "\n";
    return false;
  }
  if (!check(spqs::write_latency_hdr(hdr_path.string(), latency_ns, summary.latency, &err),
             "write_latency_hdr failed")) {
    std::cerr << "error=" << err << "\n";
    return false;
  }

  if (!check(std::filesystem::exists(summary_path), "summary.json missing")) return false;
  if (!check(std::filesystem::exists(gates_path), "gates.json missing")) return false;
  if (!check(std::filesystem::exists(ticks_path), "tick_samples.csv missing")) return false;
  if (!check(std::filesystem::exists(hdr_path), "latency.hdr missing")) return false;

  std::ifstream gates_in(gates_path);
  std::string gates_text((std::istreambuf_iterator<char>(gates_in)),
                         std::istreambuf_iterator<char>());
  if (!check(gates_text.find("\"G3_p99_99_us\"") != std::string::npos,
             "gates json missing G3 field")) {
    return false;
  }
  if (!check(gates_text.find("\"mode\": \"STRUCTURED_FAST_DEV\"") != std::string::npos,
             "gates json missing mode field")) {
    return false;
  }
  if (!check(gates_text.find("\"G4_status\": \"PASS\"") != std::string::npos,
             "gates json missing G4 status field")) {
    return false;
  }
  return check(gates_text.find("\"G5_pass\": true") != std::string::npos,
               "gates json missing G5 field");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_dense_reference_matches_structured() && ok;
  ok = test_logschema_writers() && ok;

  if (ok) {
    std::cout << "test_stage6_baselines_and_logs: PASS\n";
    return 0;
  }
  std::cerr << "test_stage6_baselines_and_logs: FAIL\n";
  return 1;
}
