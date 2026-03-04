#include "spqs/config_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace spqs {
namespace {

std::string trim(const std::string& s) {
  std::size_t first = 0;
  while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first])) != 0) {
    ++first;
  }
  if (first == s.size()) {
    return "";
  }
  std::size_t last = s.size() - 1;
  while (last > first && std::isspace(static_cast<unsigned char>(s[last])) != 0) {
    --last;
  }
  return s.substr(first, last - first + 1);
}

std::string strip_quotes(std::string s) {
  s = trim(s);
  if (s.size() >= 2U) {
    if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
      return s.substr(1, s.size() - 2);
    }
  }
  return s;
}

std::string strip_comment(const std::string& line) {
  const std::size_t pos = line.find('#');
  if (pos == std::string::npos) {
    return line;
  }
  return line.substr(0, pos);
}

bool parse_scalar_map(const std::string& path,
                      std::unordered_map<std::string, std::string>* out,
                      std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "parse_scalar_map output pointer is null";
    }
    return false;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open config: " + path;
    }
    return false;
  }

  out->clear();
  std::vector<std::string> section_stack;

  std::string line;
  while (std::getline(in, line)) {
    line = strip_comment(line);
    if (trim(line).empty()) {
      continue;
    }

    const std::size_t indent = line.find_first_not_of(' ');
    if (indent == std::string::npos) {
      continue;
    }

    const std::string stripped = line.substr(indent);
    if (!stripped.empty() && stripped.front() == '-') {
      continue;
    }

    const std::size_t colon = stripped.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    if ((indent % 2U) != 0U) {
      if (error != nullptr) {
        *error = "Config indentation must be multiples of 2 spaces";
      }
      return false;
    }
    const std::size_t depth = indent / 2U;
    while (section_stack.size() > depth) {
      section_stack.pop_back();
    }

    const std::string key = trim(stripped.substr(0, colon));
    const std::string value = trim(stripped.substr(colon + 1));

    if (value.empty()) {
      if (section_stack.size() == depth) {
        section_stack.push_back(key);
      } else if (section_stack.size() > depth) {
        section_stack[depth] = key;
      } else {
        section_stack.push_back(key);
      }
      continue;
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < section_stack.size(); ++i) {
      if (i > 0) {
        oss << '.';
      }
      oss << section_stack[i];
    }
    if (!section_stack.empty()) {
      oss << '.';
    }
    oss << key;
    out->emplace(oss.str(), value);
  }

  return true;
}

bool parse_bool(const std::string& raw, bool* out, std::string* error) {
  const std::string v = strip_quotes(trim(raw));
  if (v == "true") {
    *out = true;
    return true;
  }
  if (v == "false") {
    *out = false;
    return true;
  }
  if (error != nullptr) {
    *error = "Invalid bool literal: " + raw;
  }
  return false;
}

bool parse_int(const std::string& raw, int* out, std::string* error) {
  const std::string v = strip_quotes(trim(raw));
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(v.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0') {
    if (error != nullptr) {
      *error = "Invalid int literal: " + raw;
    }
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

bool parse_u64(const std::string& raw, std::uint64_t* out, std::string* error) {
  const std::string v = strip_quotes(trim(raw));
  char* end = nullptr;
  errno = 0;
  const unsigned long long parsed = std::strtoull(v.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0') {
    if (error != nullptr) {
      *error = "Invalid u64 literal: " + raw;
    }
    return false;
  }
  *out = static_cast<std::uint64_t>(parsed);
  return true;
}

bool parse_double(const std::string& raw, double* out, std::string* error) {
  const std::string v = strip_quotes(trim(raw));
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(v.c_str(), &end);
  if (errno != 0 || end == nullptr || *end != '\0') {
    if (error != nullptr) {
      *error = "Invalid double literal: " + raw;
    }
    return false;
  }
  *out = parsed;
  return true;
}

bool parse_int_list(const std::string& raw,
                    std::vector<int>* out,
                    std::string* error) {
  std::string s = trim(raw);
  if (s.size() < 2U || s.front() != '[' || s.back() != ']') {
    if (error != nullptr) {
      *error = "Invalid int list literal: " + raw;
    }
    return false;
  }
  s = s.substr(1, s.size() - 2);

  out->clear();
  std::stringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    const std::string t = trim(tok);
    if (t.empty()) {
      continue;
    }
    int value = 0;
    if (!parse_int(t, &value, error)) {
      return false;
    }
    out->push_back(value);
  }
  return true;
}

template <typename T, typename F>
bool maybe_set(const std::unordered_map<std::string, std::string>& kv,
               const std::string& key,
               T* dst,
               F parse_fn,
               std::string* error) {
  const auto it = kv.find(key);
  if (it == kv.end()) {
    return true;
  }
  return parse_fn(it->second, dst, error);
}

bool parse_string(const std::string& raw, std::string* out, std::string* /*error*/) {
  *out = strip_quotes(raw);
  return true;
}

}  // namespace

Config default_config() { return Config{}; }

bool load_config(const std::string& yaml_path, Config* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "Config output pointer is null";
    }
    return false;
  }

  Config cfg = default_config();
  std::unordered_map<std::string, std::string> kv;
  if (!parse_scalar_map(yaml_path, &kv, error)) {
    return false;
  }

  if (!maybe_set(kv, "run.run_name", &cfg.run.run_name, parse_string, error)) return false;
  if (!maybe_set(kv, "run.seed", &cfg.run.seed, parse_u64, error)) return false;
  if (!maybe_set(kv, "run.out_dir", &cfg.run.out_dir, parse_string, error)) return false;
  if (!maybe_set(kv, "run.threads", &cfg.run.threads, parse_int, error)) return false;
  if (!maybe_set(kv, "run.pin_cpu", &cfg.run.pin_cpu, parse_int, error)) return false;
  if (!maybe_set(kv, "run.disable_turbo", &cfg.run.disable_turbo, parse_bool, error)) return false;
  if (!maybe_set(kv, "run.disable_smt", &cfg.run.disable_smt, parse_bool, error)) return false;

  if (!maybe_set(kv, "problem.n", &cfg.problem.n, parse_int, error)) return false;
  if (!maybe_set(kv, "problem.a_max", &cfg.problem.a_max, parse_int, error)) return false;
  if (!maybe_set(kv, "problem.I_max", &cfg.problem.I_max, parse_int, error)) return false;

  if (!maybe_set(kv, "structure.mode", &cfg.structure.mode, parse_string, error)) return false;
  if (!maybe_set(kv, "structure.B", &cfg.structure.B, parse_int, error)) return false;
  if (!maybe_set(kv, "structure.block_sizes", &cfg.structure.block_sizes, parse_int_list, error)) return false;
  if (!maybe_set(kv, "structure.m_local_per_block", &cfg.structure.m_local_per_block, parse_int, error)) return false;
  if (!maybe_set(kv, "structure.m_global", &cfg.structure.m_global, parse_int, error)) return false;

  if (!maybe_set(kv, "generator.A_local_type", &cfg.generator.A_local_type, parse_string, error)) return false;
  if (!maybe_set(kv, "generator.A_global_type", &cfg.generator.A_global_type, parse_string, error)) return false;
  if (!maybe_set(kv, "generator.factors", &cfg.generator.factors, parse_int, error)) return false;
  if (!maybe_set(kv, "generator.row_norm", &cfg.generator.row_norm, parse_string, error)) return false;
  if (!maybe_set(kv, "generator.b_margin", &cfg.generator.b_margin, parse_double, error)) return false;
  if (!maybe_set(kv, "generator.b_noise_std", &cfg.generator.b_noise_std, parse_double, error)) return false;

  if (!maybe_set(kv, "solver.warm_start", &cfg.solver.warm_start, parse_bool, error)) return false;
  if (!maybe_set(kv, "solver.bland_rule", &cfg.solver.bland_rule, parse_bool, error)) return false;
  if (!maybe_set(kv, "solver.feasibility", &cfg.solver.feasibility, parse_string, error)) return false;
  if (!maybe_set(kv, "solver.q_anchor", &cfg.solver.q_anchor, parse_string, error)) return false;
  if (!maybe_set(kv, "solver.strict_interior", &cfg.solver.strict_interior, parse_bool, error)) return false;
  if (!maybe_set(kv, "solver.kappa_min", &cfg.solver.kappa_min, parse_double, error)) return false;
  if (!maybe_set(kv, "solver.tau_abs_scale", &cfg.solver.tau_abs_scale, parse_double, error)) return false;
  if (!maybe_set(kv, "solver.tau_shrink_min", &cfg.solver.tau_shrink_min, parse_double, error)) return false;
  if (!maybe_set(kv, "solver.tau_shrink_max", &cfg.solver.tau_shrink_max, parse_double, error)) return false;
  if (!maybe_set(kv, "solver.fallback_enabled", &cfg.solver.fallback_enabled, parse_bool, error)) return false;
  if (!maybe_set(kv, "solver.fallback_mode", &cfg.solver.fallback_mode, parse_string, error)) return false;

  if (!maybe_set(kv, "stream.mode", &cfg.stream.mode, parse_string, error)) return false;
  if (!maybe_set(kv, "stream.seed_stream", &cfg.stream.seed_stream, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.T_warmup", &cfg.stream.T_warmup, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.T_latency", &cfg.stream.T_latency, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.T_feas", &cfg.stream.T_feas, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.ar_rho", &cfg.stream.ar_rho, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.K_small", &cfg.stream.K_small, parse_int, error)) return false;
  if (!maybe_set(kv, "stream.K_small_alt", &cfg.stream.K_small_alt, parse_int, error)) return false;
  if (!maybe_set(kv, "stream.p_K_small_alt", &cfg.stream.p_K_small_alt, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.delta_small", &cfg.stream.delta_small, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.p_jump", &cfg.stream.p_jump, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.K_jump", &cfg.stream.K_jump, parse_int, error)) return false;
  if (!maybe_set(kv, "stream.delta_jump", &cfg.stream.delta_jump, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.clamp_inf", &cfg.stream.clamp_inf, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.burst.enabled", &cfg.stream.burst.enabled, parse_bool, error)) return false;
  if (!maybe_set(kv, "stream.burst.every_ticks", &cfg.stream.burst.every_ticks, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.burst.length_ticks", &cfg.stream.burst.length_ticks, parse_u64, error)) return false;
  if (!maybe_set(kv, "stream.burst.K_burst", &cfg.stream.burst.K_burst, parse_int, error)) return false;
  if (!maybe_set(kv, "stream.burst.delta_burst", &cfg.stream.burst.delta_burst, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.burst.p_jump_in_burst", &cfg.stream.burst.p_jump_in_burst, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.burst.K_jump_in_burst", &cfg.stream.burst.K_jump_in_burst, parse_int, error)) return false;
  if (!maybe_set(kv, "stream.burst.delta_jump_in_burst", &cfg.stream.burst.delta_jump_in_burst, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.q_small", &cfg.stream.q_small, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.q_big", &cfg.stream.q_big, parse_double, error)) return false;
  if (!maybe_set(kv, "stream.p_big", &cfg.stream.p_big, parse_double, error)) return false;

  if (!maybe_set(kv, "instrumentation.latency_clock", &cfg.instrumentation.latency_clock, parse_string, error)) return false;
  if (!maybe_set(kv, "instrumentation.perf.enabled", &cfg.instrumentation.perf_enabled, parse_bool, error)) return false;
  if (!maybe_set(kv, "instrumentation.energy.enabled", &cfg.instrumentation.energy_enabled, parse_bool, error)) return false;

  *out = std::move(cfg);
  if (error != nullptr) {
    *error = "ok";
  }
  return true;
}

}  // namespace spqs
