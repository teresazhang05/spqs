#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "spqs/config.hpp"

namespace spqs {

inline constexpr const char* kSchemaVersion = "spqs.v2.2";

struct GateSummary {
  bool G_stream_pass = false;
  double G_stream_outside_mean_changed_blocks = 0.0;
  double G_stream_outside_p99_9_changed_blocks = 0.0;
  int G_stream_inside_max_changed_blocks = 0;
  bool G1_oracle_match = false;
  double G1_worst_mismatch = 0.0;
  int G2_feas_violations = 0;
  double G2_worst_violation = 0.0;
  int G2_audit_failures = 0;
  double G3_p99_99_us = 0.0;
  bool G3_pass = false;
  double G4_obj_gap_abs_max = 0.0;
  double G4_obj_gap_rel_max = 0.0;
  std::string G4_status = "SKIPPED";
  bool G4_pass = false;
  bool G5_pass = false;
  bool G6_pass = false;
  std::uint64_t oracle_init_tick_calls = 0;
  std::uint64_t oracle_active_clears = 0;
  std::uint64_t alloc_calls_during_loop = 0;
  std::uint64_t bytes_allocated_during_loop = 0;
  bool structure_valid = false;
  std::string mode = "BRUTEFORCE_ONLY";
  bool claim_setting = false;
  bool osqp_available = false;
  std::string g4_reference_backend = "NONE";
  std::uint64_t compared_samples = 0;
  bool pinning_enforced = false;
  bool pinning_verified = false;
  int pin_cpu_observed = -1;
};

struct LatencySummary {
  std::uint64_t count = 0;
  double p50_us = 0.0;
  double p95_us = 0.0;
  double p99_us = 0.0;
  double p99_9_us = 0.0;
  double p99_99_us = 0.0;
  double max_us = 0.0;
};

struct SummaryPayload {
  std::string schema_version = kSchemaVersion;
  std::string git_commit = "unknown";
  std::string compiler = "unknown";
  std::string cflags = "";
  std::string cpu_model = "unknown";
  std::string kernel_version = "unknown";
  bool turbo_disabled = false;
  bool smt_disabled = false;
  int pin_cpu = -1;
  bool claim_setting = false;
  bool pinning_enforced = false;
  bool pinning_verified = false;
  int pin_cpu_observed = -1;
  Config config;
  LatencySummary latency;
  std::uint64_t warmup_ticks = 0;
  std::uint64_t latency_ticks = 0;
  std::uint64_t feas_ticks = 0;
  std::uint64_t fallback_count = 0;
  double fallback_rate = 0.0;
  std::uint64_t alloc_calls_during_loop = 0;
  std::uint64_t bytes_allocated_during_loop = 0;
  double stream_outside_mean_changed_blocks = 0.0;
  double stream_outside_p99_9_changed_blocks = 0.0;
  int stream_inside_max_changed_blocks = 0;
};

struct TickSampleRow {
  std::uint64_t tick = 0;
  std::uint64_t latency_ns = 0;
  int iters = 0;
  int adds = 0;
  int removes = 0;
  int active_size_final = 0;
  int touched_blocks_per_iter_max = 0;
  bool fallback_used = false;
  bool kappa_valid = true;
  double min_kappa = 0.0;
  double tau_required = 0.0;
  double tau_shrink_used = 0.0;
  double fallback_alpha = 1.0;
  double max_violation_certified = 0.0;
  double min_slack_certified = 0.0;
  int changed_blocks_count = 0;
  int oracle_local_blocks_recomputed = 0;
  bool stream_in_burst = false;
  double stream_delta_l2 = 0.0;
  double g4_obj_gap_abs = 0.0;
  double g4_obj_gap_rel = 0.0;
};

LatencySummary summarize_latency_us(const std::vector<std::uint64_t>& latency_ns);

bool write_summary_json(const std::string& path,
                        const SummaryPayload& payload,
                        std::string* error);
bool write_gates_json(const std::string& path,
                      const GateSummary& gates,
                      std::string* error);
bool write_tick_samples_csv(const std::string& path,
                            const std::vector<TickSampleRow>& rows,
                            std::string* error);
bool write_latency_hdr(const std::string& path,
                       const std::vector<std::uint64_t>& latency_ns,
                       const LatencySummary& summary,
                       std::string* error);

}  // namespace spqs
