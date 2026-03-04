#include "spqs/logschema.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace spqs {
namespace {

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

double quantile_us(const std::vector<std::uint64_t>& sorted_ns, double q) {
  if (sorted_ns.empty()) {
    return 0.0;
  }
  const double clamped_q = std::min(1.0, std::max(0.0, q));
  const double idx_f = clamped_q * static_cast<double>(sorted_ns.size() - 1U);
  const std::size_t idx = static_cast<std::size_t>(idx_f);
  return static_cast<double>(sorted_ns[idx]) / 1000.0;
}

void write_config_json(std::ostream& os, const Config& cfg) {
  os << "  \"config\": {\n";
  os << "    \"run\": {\n";
  os << "      \"run_name\": \"" << json_escape(cfg.run.run_name) << "\",\n";
  os << "      \"seed\": " << cfg.run.seed << ",\n";
  os << "      \"out_dir\": \"" << json_escape(cfg.run.out_dir) << "\",\n";
  os << "      \"threads\": " << cfg.run.threads << ",\n";
  os << "      \"pin_cpu\": " << cfg.run.pin_cpu << ",\n";
  os << "      \"disable_turbo\": " << (cfg.run.disable_turbo ? "true" : "false") << ",\n";
  os << "      \"disable_smt\": " << (cfg.run.disable_smt ? "true" : "false") << "\n";
  os << "    },\n";

  os << "    \"problem\": {\n";
  os << "      \"n\": " << cfg.problem.n << ",\n";
  os << "      \"a_max\": " << cfg.problem.a_max << ",\n";
  os << "      \"I_max\": " << cfg.problem.I_max << "\n";
  os << "    },\n";

  os << "    \"structure\": {\n";
  os << "      \"mode\": \"" << json_escape(cfg.structure.mode) << "\",\n";
  os << "      \"B\": " << cfg.structure.B << ",\n";
  os << "      \"block_sizes\": [";
  for (std::size_t i = 0; i < cfg.structure.block_sizes.size(); ++i) {
    if (i > 0) {
      os << ',';
    }
    os << cfg.structure.block_sizes[i];
  }
  os << "],\n";
  os << "      \"m_local_per_block\": " << cfg.structure.m_local_per_block << ",\n";
  os << "      \"m_global\": " << cfg.structure.m_global << "\n";
  os << "    },\n";

  os << "    \"generator\": {\n";
  os << "      \"A_local_type\": \"" << json_escape(cfg.generator.A_local_type) << "\",\n";
  os << "      \"A_global_type\": \"" << json_escape(cfg.generator.A_global_type) << "\",\n";
  os << "      \"factors\": " << cfg.generator.factors << ",\n";
  os << "      \"row_norm\": \"" << json_escape(cfg.generator.row_norm) << "\",\n";
  os << "      \"b_margin\": " << cfg.generator.b_margin << ",\n";
  os << "      \"b_noise_std\": " << cfg.generator.b_noise_std << "\n";
  os << "    },\n";

  os << "    \"solver\": {\n";
  os << "      \"warm_start\": " << (cfg.solver.warm_start ? "true" : "false") << ",\n";
  os << "      \"bland_rule\": " << (cfg.solver.bland_rule ? "true" : "false") << ",\n";
  os << "      \"feasibility\": \"" << json_escape(cfg.solver.feasibility) << "\",\n";
  os << "      \"q_anchor\": \"" << json_escape(cfg.solver.q_anchor) << "\",\n";
  os << "      \"strict_interior\": " << (cfg.solver.strict_interior ? "true" : "false") << ",\n";
  os << "      \"kappa_min\": " << cfg.solver.kappa_min << ",\n";
  os << "      \"tau_abs_scale\": " << cfg.solver.tau_abs_scale << ",\n";
  os << "      \"tau_shrink_min\": " << cfg.solver.tau_shrink_min << ",\n";
  os << "      \"tau_shrink_max\": " << cfg.solver.tau_shrink_max << ",\n";
  os << "      \"fallback_enabled\": " << (cfg.solver.fallback_enabled ? "true" : "false") << ",\n";
  os << "      \"fallback_mode\": \"" << json_escape(cfg.solver.fallback_mode) << "\"\n";
  os << "    },\n";

  os << "    \"stream\": {\n";
  os << "      \"mode\": \"" << json_escape(cfg.stream.mode) << "\",\n";
  os << "      \"seed_stream\": " << cfg.stream.seed_stream << ",\n";
  os << "      \"T_warmup\": " << cfg.stream.T_warmup << ",\n";
  os << "      \"T_latency\": " << cfg.stream.T_latency << ",\n";
  os << "      \"T_feas\": " << cfg.stream.T_feas << ",\n";
  os << "      \"ar_rho\": " << cfg.stream.ar_rho << ",\n";
  os << "      \"K_small\": " << cfg.stream.K_small << ",\n";
  os << "      \"K_small_alt\": " << cfg.stream.K_small_alt << ",\n";
  os << "      \"p_K_small_alt\": " << cfg.stream.p_K_small_alt << ",\n";
  os << "      \"delta_small\": " << cfg.stream.delta_small << ",\n";
  os << "      \"p_jump\": " << cfg.stream.p_jump << ",\n";
  os << "      \"K_jump\": " << cfg.stream.K_jump << ",\n";
  os << "      \"delta_jump\": " << cfg.stream.delta_jump << ",\n";
  os << "      \"clamp_inf\": " << cfg.stream.clamp_inf << ",\n";
  os << "      \"burst\": {\n";
  os << "        \"enabled\": " << (cfg.stream.burst.enabled ? "true" : "false") << ",\n";
  os << "        \"every_ticks\": " << cfg.stream.burst.every_ticks << ",\n";
  os << "        \"length_ticks\": " << cfg.stream.burst.length_ticks << ",\n";
  os << "        \"K_burst\": " << cfg.stream.burst.K_burst << ",\n";
  os << "        \"delta_burst\": " << cfg.stream.burst.delta_burst << ",\n";
  os << "        \"p_jump_in_burst\": " << cfg.stream.burst.p_jump_in_burst << ",\n";
  os << "        \"K_jump_in_burst\": " << cfg.stream.burst.K_jump_in_burst << ",\n";
  os << "        \"delta_jump_in_burst\": " << cfg.stream.burst.delta_jump_in_burst << "\n";
  os << "      },\n";
  os << "      \"q_small\": " << cfg.stream.q_small << ",\n";
  os << "      \"q_big\": " << cfg.stream.q_big << ",\n";
  os << "      \"p_big\": " << cfg.stream.p_big << "\n";
  os << "    },\n";

  os << "    \"instrumentation\": {\n";
  os << "      \"latency_clock\": \"" << json_escape(cfg.instrumentation.latency_clock) << "\",\n";
  os << "      \"perf_enabled\": " << (cfg.instrumentation.perf_enabled ? "true" : "false") << ",\n";
  os << "      \"energy_enabled\": " << (cfg.instrumentation.energy_enabled ? "true" : "false") << "\n";
  os << "    }\n";
  os << "  }\n";
}

}  // namespace

LatencySummary summarize_latency_us(const std::vector<std::uint64_t>& latency_ns) {
  LatencySummary out;
  out.count = latency_ns.size();
  if (latency_ns.empty()) {
    return out;
  }

  std::vector<std::uint64_t> sorted = latency_ns;
  std::sort(sorted.begin(), sorted.end());

  out.p50_us = quantile_us(sorted, 0.50);
  out.p95_us = quantile_us(sorted, 0.95);
  out.p99_us = quantile_us(sorted, 0.99);
  out.p99_9_us = quantile_us(sorted, 0.999);
  out.p99_99_us = quantile_us(sorted, 0.9999);
  out.max_us = static_cast<double>(sorted.back()) / 1000.0;
  return out;
}

bool write_summary_json(const std::string& path,
                        const SummaryPayload& payload,
                        std::string* error) {
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "failed to open summary path: " + path;
    }
    return false;
  }

  out << std::setprecision(17);
  out << "{\n";
  out << "  \"schema_version\": \"" << json_escape(payload.schema_version) << "\",\n";
  out << "  \"git_commit\": \"" << json_escape(payload.git_commit) << "\",\n";
  out << "  \"compiler\": \"" << json_escape(payload.compiler) << "\",\n";
  out << "  \"cflags\": \"" << json_escape(payload.cflags) << "\",\n";
  out << "  \"cpu_model\": \"" << json_escape(payload.cpu_model) << "\",\n";
  out << "  \"kernel_version\": \"" << json_escape(payload.kernel_version) << "\",\n";
  out << "  \"turbo_disabled\": " << (payload.turbo_disabled ? "true" : "false") << ",\n";
  out << "  \"smt_disabled\": " << (payload.smt_disabled ? "true" : "false") << ",\n";
  out << "  \"pin_cpu\": " << payload.pin_cpu << ",\n";
  out << "  \"claim_setting\": " << (payload.claim_setting ? "true" : "false") << ",\n";
  out << "  \"pinning_enforced\": " << (payload.pinning_enforced ? "true" : "false") << ",\n";
  out << "  \"pinning_verified\": " << (payload.pinning_verified ? "true" : "false") << ",\n";
  out << "  \"pin_cpu_observed\": " << payload.pin_cpu_observed << ",\n";
  out << "  \"warmup_ticks\": " << payload.warmup_ticks << ",\n";
  out << "  \"latency_ticks\": " << payload.latency_ticks << ",\n";
  out << "  \"feas_ticks\": " << payload.feas_ticks << ",\n";
  out << "  \"fallback_count\": " << payload.fallback_count << ",\n";
  out << "  \"fallback_rate\": " << payload.fallback_rate << ",\n";
  out << "  \"alloc_calls_during_loop\": " << payload.alloc_calls_during_loop << ",\n";
  out << "  \"bytes_allocated_during_loop\": " << payload.bytes_allocated_during_loop << ",\n";
  out << "  \"stream_outside_mean_changed_blocks\": " << payload.stream_outside_mean_changed_blocks << ",\n";
  out << "  \"stream_outside_p99_9_changed_blocks\": " << payload.stream_outside_p99_9_changed_blocks << ",\n";
  out << "  \"stream_inside_max_changed_blocks\": " << payload.stream_inside_max_changed_blocks << ",\n";
  out << "  \"latency\": {\n";
  out << "    \"count\": " << payload.latency.count << ",\n";
  out << "    \"p50_us\": " << payload.latency.p50_us << ",\n";
  out << "    \"p95_us\": " << payload.latency.p95_us << ",\n";
  out << "    \"p99_us\": " << payload.latency.p99_us << ",\n";
  out << "    \"p99_9_us\": " << payload.latency.p99_9_us << ",\n";
  out << "    \"p99_99_us\": " << payload.latency.p99_99_us << ",\n";
  out << "    \"max_us\": " << payload.latency.max_us << "\n";
  out << "  },\n";
  write_config_json(out, payload.config);
  out << "}\n";

  if (!out.good()) {
    if (error != nullptr) {
      *error = "failed while writing summary json: " + path;
    }
    return false;
  }
  return true;
}

bool write_gates_json(const std::string& path,
                      const GateSummary& gates,
                      std::string* error) {
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "failed to open gates path: " + path;
    }
    return false;
  }

  out << std::setprecision(17);
  out << "{\n";
  out << "  \"schema_version\": \"" << kSchemaVersion << "\",\n";
  out << "  \"G_stream_pass\": " << (gates.G_stream_pass ? "true" : "false") << ",\n";
  out << "  \"G_stream_outside_mean_changed_blocks\": "
      << gates.G_stream_outside_mean_changed_blocks << ",\n";
  out << "  \"G_stream_outside_p99_9_changed_blocks\": "
      << gates.G_stream_outside_p99_9_changed_blocks << ",\n";
  out << "  \"G_stream_inside_max_changed_blocks\": "
      << gates.G_stream_inside_max_changed_blocks << ",\n";
  out << "  \"G1_oracle_match\": " << (gates.G1_oracle_match ? "true" : "false") << ",\n";
  out << "  \"G1_worst_mismatch\": " << gates.G1_worst_mismatch << ",\n";
  out << "  \"G2_feas_violations\": " << gates.G2_feas_violations << ",\n";
  out << "  \"G2_worst_violation\": " << gates.G2_worst_violation << ",\n";
  out << "  \"G2_audit_failures\": " << gates.G2_audit_failures << ",\n";
  out << "  \"G3_p99_99_us\": " << gates.G3_p99_99_us << ",\n";
  out << "  \"G3_pass\": " << (gates.G3_pass ? "true" : "false") << ",\n";
  out << "  \"G4_obj_gap_abs_max\": " << gates.G4_obj_gap_abs_max << ",\n";
  out << "  \"G4_obj_gap_rel_max\": " << gates.G4_obj_gap_rel_max << ",\n";
  out << "  \"G4_status\": \"" << json_escape(gates.G4_status) << "\",\n";
  out << "  \"G4_pass\": " << (gates.G4_pass ? "true" : "false") << ",\n";
  out << "  \"G5_pass\": " << (gates.G5_pass ? "true" : "false") << ",\n";
  out << "  \"G6_pass\": " << (gates.G6_pass ? "true" : "false") << ",\n";
  out << "  \"oracle_init_tick_calls\": " << gates.oracle_init_tick_calls << ",\n";
  out << "  \"oracle_active_clears\": " << gates.oracle_active_clears << ",\n";
  out << "  \"alloc_calls_during_loop\": " << gates.alloc_calls_during_loop << ",\n";
  out << "  \"bytes_allocated_during_loop\": " << gates.bytes_allocated_during_loop << ",\n";
  out << "  \"structure_valid\": " << (gates.structure_valid ? "true" : "false") << ",\n";
  out << "  \"mode\": \"" << json_escape(gates.mode) << "\",\n";
  out << "  \"claim_setting\": " << (gates.claim_setting ? "true" : "false") << ",\n";
  out << "  \"osqp_available\": " << (gates.osqp_available ? "true" : "false") << ",\n";
  out << "  \"pinning_enforced\": " << (gates.pinning_enforced ? "true" : "false") << ",\n";
  out << "  \"pinning_verified\": " << (gates.pinning_verified ? "true" : "false") << ",\n";
  out << "  \"pin_cpu_observed\": " << gates.pin_cpu_observed << ",\n";
  out << "  \"g4_reference_backend\": \"" << json_escape(gates.g4_reference_backend) << "\",\n";
  out << "  \"compared_samples\": " << gates.compared_samples << "\n";
  out << "}\n";

  if (!out.good()) {
    if (error != nullptr) {
      *error = "failed while writing gates json: " + path;
    }
    return false;
  }
  return true;
}

bool write_tick_samples_csv(const std::string& path,
                            const std::vector<TickSampleRow>& rows,
                            std::string* error) {
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "failed to open tick samples path: " + path;
    }
    return false;
  }

  out << std::setprecision(17);
  out << "tick,latency_ns,iters,adds,removes,active_size_final,touched_blocks_per_iter_max,"
         "fallback_used,kappa_valid,min_kappa,tau_required,tau_shrink_used,fallback_alpha,"
         "max_violation_certified,min_slack_certified,changed_blocks_count,"
         "oracle_local_blocks_recomputed,stream_in_burst,stream_delta_l2,"
         "g4_obj_gap_abs,g4_obj_gap_rel\n";

  for (const TickSampleRow& row : rows) {
    out << row.tick << ','
        << row.latency_ns << ','
        << row.iters << ','
        << row.adds << ','
        << row.removes << ','
        << row.active_size_final << ','
        << row.touched_blocks_per_iter_max << ','
        << (row.fallback_used ? 1 : 0) << ','
        << (row.kappa_valid ? 1 : 0) << ','
        << row.min_kappa << ','
        << row.tau_required << ','
        << row.tau_shrink_used << ','
        << row.fallback_alpha << ','
        << row.max_violation_certified << ','
        << row.min_slack_certified << ','
        << row.changed_blocks_count << ','
        << row.oracle_local_blocks_recomputed << ','
        << (row.stream_in_burst ? 1 : 0) << ','
        << row.stream_delta_l2 << ','
        << row.g4_obj_gap_abs << ','
        << row.g4_obj_gap_rel << '\n';
  }

  if (!out.good()) {
    if (error != nullptr) {
      *error = "failed while writing tick samples csv: " + path;
    }
    return false;
  }
  return true;
}

bool write_latency_hdr(const std::string& path,
                       const std::vector<std::uint64_t>& latency_ns,
                       const LatencySummary& summary,
                       std::string* error) {
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "failed to open latency hdr path: " + path;
    }
    return false;
  }

  out << std::setprecision(17);
  out << "# spqs.latency.hdr.v1\n";
  out << "count=" << summary.count << "\n";
  out << "p50_us=" << summary.p50_us << "\n";
  out << "p95_us=" << summary.p95_us << "\n";
  out << "p99_us=" << summary.p99_us << "\n";
  out << "p99_9_us=" << summary.p99_9_us << "\n";
  out << "p99_99_us=" << summary.p99_99_us << "\n";
  out << "max_us=" << summary.max_us << "\n";
  out << "samples_ns_begin\n";
  for (std::uint64_t v : latency_ns) {
    out << v << "\n";
  }
  out << "samples_ns_end\n";

  if (!out.good()) {
    if (error != nullptr) {
      *error = "failed while writing latency hdr: " + path;
    }
    return false;
  }
  return true;
}

}  // namespace spqs
