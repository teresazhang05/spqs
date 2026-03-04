#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "spqs/active_set.hpp"
#include "spqs/arena.hpp"
#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/cholesky_update.hpp"
#include "spqs/config_loader.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/projector.hpp"
#include "spqs/rhs.hpp"
#include "spqs/structure_validator.hpp"
#include "spqs/violator_oracle.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

double objective(const std::vector<double>& q,
                 const std::vector<double>& q_prop) {
  double s = 0.0;
  for (std::size_t i = 0; i < q.size(); ++i) {
    const double d = q[i] - q_prop[i];
    s += d * d;
  }
  return 0.5 * s;
}

bool exact_feasible(const spqs::ConstraintsLocal& local,
                    const spqs::ConstraintsGlobal& global,
                    const spqs::RHSAll& rhs,
                    const std::vector<double>& q,
                    long double tol = 1e-12L) {
  for (int r = 0; r < local.layout.B; ++r) {
    const auto& block = local.A_block[static_cast<std::size_t>(r)];
    const int offset = local.layout.block_offsets[r];
    const double* b_block = rhs.local.block_ptr(r);

    for (int i = 0; i < block.rows; ++i) {
      long double s = 0.0L;
      const double* row = block.row_ptr(i);
      for (int j = 0; j < block.cols; ++j) {
        s += static_cast<long double>(row[j]) *
             static_cast<long double>(q[static_cast<std::size_t>(offset + j)]);
      }
      if (s > static_cast<long double>(b_block[i]) + tol) {
        return false;
      }
    }
  }

  for (int g = 0; g < global.m_global; ++g) {
    const double* row = global.row_ptr(g);
    long double s = 0.0L;
    for (int j = 0; j < global.n; ++j) {
      s += static_cast<long double>(row[j]) *
           static_cast<long double>(q[static_cast<std::size_t>(j)]);
    }
    if (s > static_cast<long double>(rhs.global.b[static_cast<std::size_t>(g)]) + tol) {
      return false;
    }
  }
  return true;
}

bool solve_subset_projection(const spqs::ConstraintsLocal& local,
                             const spqs::ConstraintsGlobal& global,
                             const spqs::RHSAll& rhs,
                             const std::vector<double>& q_prop,
                             const std::vector<int>& active_ids,
                             std::vector<double>* q_out,
                             std::vector<double>* lambda_out) {
  const int n = local.layout.n;
  std::vector<spqs::ConstraintRef> refs;
  refs.reserve(active_ids.size());

  for (int id : active_ids) {
    spqs::ConstraintRef ref;
    if (!spqs::decode_constraint_id(local, global, id, &ref)) {
      return false;
    }
    refs.push_back(ref);
  }

  if (refs.empty()) {
    *q_out = q_prop;
    lambda_out->clear();
    return true;
  }

  const int k = static_cast<int>(refs.size());
  std::vector<double> gram(static_cast<std::size_t>(k) * static_cast<std::size_t>(k), 0.0);
  std::vector<double> rhs_vec(static_cast<std::size_t>(k), 0.0);

  for (int i = 0; i < k; ++i) {
    rhs_vec[static_cast<std::size_t>(i)] =
        spqs::dot_constraint_q(refs[static_cast<std::size_t>(i)], local, global, q_prop.data()) -
        spqs::constraint_rhs_value(refs[static_cast<std::size_t>(i)], rhs);

    for (int j = 0; j <= i; ++j) {
      const double v = spqs::dot_constraint_constraint(
          refs[static_cast<std::size_t>(i)], refs[static_cast<std::size_t>(j)], local, global);
      gram[static_cast<std::size_t>(i) * k + j] = v;
      gram[static_cast<std::size_t>(j) * k + i] = v;
    }
  }

  spqs::ActiveFactor factor;
  if (!spqs::factorize_active_gram(gram, k, &factor)) {
    return false;
  }

  if (!spqs::solve_active_gram(factor, rhs_vec, lambda_out)) {
    return false;
  }

  q_out->assign(q_prop.begin(), q_prop.end());
  for (int i = 0; i < k; ++i) {
    spqs::add_scaled_constraint_to_q(refs[static_cast<std::size_t>(i)],
                                     local,
                                     global,
                                     -(*lambda_out)[static_cast<std::size_t>(i)],
                                     q_out->data());
  }

  if (static_cast<int>(q_out->size()) != n) {
    return false;
  }
  return true;
}

bool reference_project_exact_enumeration(const spqs::ConstraintsLocal& local,
                                         const spqs::ConstraintsGlobal& global,
                                         const spqs::RHSAll& rhs,
                                         const std::vector<double>& q_prop,
                                         std::vector<double>* q_best) {
  const int m = spqs::total_constraints(local, global);
  if (m > 20) {
    return false;
  }

  const std::uint64_t max_mask = (1ULL << static_cast<std::uint64_t>(m));
  bool found = false;
  double best_obj = std::numeric_limits<double>::infinity();

  for (std::uint64_t mask = 0; mask < max_mask; ++mask) {
    std::vector<int> active_ids;
    for (int i = 0; i < m; ++i) {
      if (((mask >> static_cast<std::uint64_t>(i)) & 1ULL) != 0ULL) {
        active_ids.push_back(i);
      }
    }

    std::vector<double> q_candidate;
    std::vector<double> lambda;
    if (!solve_subset_projection(local, global, rhs, q_prop, active_ids, &q_candidate, &lambda)) {
      continue;
    }

    bool lambda_ok = true;
    for (double l : lambda) {
      if (l < -1e-10) {
        lambda_ok = false;
        break;
      }
    }
    if (!lambda_ok) {
      continue;
    }

    if (!exact_feasible(local, global, rhs, q_candidate)) {
      continue;
    }

    const double obj = objective(q_candidate, q_prop);
    if (obj < best_obj) {
      best_obj = obj;
      *q_best = q_candidate;
      found = true;
    }
  }

  return found;
}

bool test_layout_and_structure_basics() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(8, {3, 5}, &layout, &why)) {
    std::cerr << "FAIL: layout build failed: " << why << "\n";
    return false;
  }
  if (!check(layout.var_to_block.at(0) == 0, "var 0 in block 0")) return false;
  if (!check(layout.var_to_block.at(7) == 1, "var 7 in block 1")) return false;

  if (spqs::make_block_layout(8, {4, 3}, &layout, &why)) {
    std::cerr << "FAIL: invalid layout should be rejected\n";
    return false;
  }

  spqs::Config cfg = spqs::default_config();
  cfg.problem.n = 64;
  cfg.structure.B = 4;
  cfg.structure.block_sizes = {16, 16, 16, 16};
  cfg.structure.m_local_per_block = 8;
  cfg.structure.m_global = 4;
  cfg.structure.mode = "block_sparse";

  const spqs::StructureValidationResult good = spqs::validate_structure_from_config(cfg);
  if (!check(good.structure_valid, "structured config validates")) return false;
  if (!check(good.mode == spqs::RunMode::STRUCTURED_FAST, "structured mode expected")) return false;

  cfg.structure.block_sizes = {16, 16, 16};
  const spqs::StructureValidationResult bad = spqs::validate_structure_from_config(cfg);
  if (!check(!bad.structure_valid, "invalid structure rejected")) return false;
  return check(bad.mode == spqs::RunMode::BRUTEFORCE_ONLY, "invalid structure routes to bruteforce");
}

bool test_projector_matches_exact_reference_small() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(4, {2, 2}, &layout, &why)) {
    std::cerr << "FAIL: layout build failed: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 2, 1111);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(4, 1, 1, 1112);

  spqs::Arena arena(1U << 18U);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);
  spqs::StreamingProjector projector(&local, &global, &oracle, &arena);

  spqs::ProjectorOptions opts;
  opts.a_max = 10;
  opts.I_max = 100;
  opts.warm_start = false;
  opts.strict_interior = false;
  projector.set_options(opts);

  std::mt19937_64 rng(1113);
  std::uniform_real_distribution<double> qd(-2.0, 2.0);
  int fallback_skips = 0;
  int compared = 0;

  for (int t = 0; t < 120; ++t) {
    const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 2.0, 0.1, 1200 + t);
    projector.set_rhs(rhs);

    std::vector<double> q_prop(4, 0.0);
    for (double& v : q_prop) {
      v = qd(rng);
    }

    std::vector<double> q_out(4, 0.0);
    const spqs::SolverStats st = projector.project(q_prop.data(), q_out.data());

    if (st.fallback_used) {
      ++fallback_skips;
      continue;
    }

    if (!check(st.max_violation_certified <= 1e-12,
               "projector output must be certified-feasible")) {
      return false;
    }

    if (!check(exact_feasible(local, global, rhs, q_out),
               "projector output must be exact-feasible")) {
      return false;
    }

    std::vector<double> q_ref;
    if (!reference_project_exact_enumeration(local, global, rhs, q_prop, &q_ref)) {
      std::cerr << "FAIL: exact reference enumeration did not find solution\n";
      return false;
    }

    const double f_out = objective(q_out, q_prop);
    const double f_ref = objective(q_ref, q_prop);
    const double abs_gap = std::abs(f_out - f_ref);
    const double rel_gap = abs_gap / std::max(1.0, std::abs(f_ref));

    if (!check(abs_gap <= 1e-8 || rel_gap <= 1e-8,
               "projector objective must match exact reference")) {
      std::cerr << "debug: t=" << t << " f_out=" << f_out << " f_ref=" << f_ref
                << " abs_gap=" << abs_gap << " rel_gap=" << rel_gap
                << " fallback=" << (st.fallback_used ? 1 : 0)
                << " iters=" << st.iters
                << " adds=" << st.adds
                << " removes=" << st.removes
                << " active_final=" << st.active_size_final
                << " max_v=" << st.max_violation_certified
                << "\n";
      return false;
    }
    ++compared;
  }

  if (!check(compared >= 80, "too few non-fallback samples for exact comparison")) {
    return false;
  }
  return check(fallback_skips < 40, "too many fallback skips in exact reference test");
}

bool test_churn_stress_feasible() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(32, {8, 8, 8, 8}, &layout, &why)) {
    std::cerr << "FAIL: layout build failed: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 8, 2201);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(32, 4, 2, 2202);

  spqs::Arena arena(1U << 20U);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);
  spqs::StreamingProjector projector(&local, &global, &oracle, &arena);

  spqs::ProjectorOptions opts;
  opts.a_max = 64;
  opts.I_max = 240;
  opts.warm_start = true;
  opts.strict_interior = true;
  projector.set_options(opts);

  std::mt19937_64 rng(2203);
  std::uniform_real_distribution<double> small(-2.0, 2.0);
  std::uniform_real_distribution<double> big(-12.0, 12.0);
  std::bernoulli_distribution choose_big(0.2);

  int fallback_count = 0;
  int touched_positive = 0;

  for (int t = 0; t < 1500; ++t) {
    const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 2.5, 0.15, 3000 + t);
    projector.set_rhs(rhs);

    std::vector<double> q_prop(32, 0.0);
    const bool is_big = choose_big(rng);
    for (double& v : q_prop) {
      v = is_big ? big(rng) : small(rng);
    }

    std::vector<double> q_out(32, 0.0);
    const spqs::SolverStats st = projector.project(q_prop.data(), q_out.data());

    if (st.fallback_used) {
      ++fallback_count;
    }
    if (st.touched_blocks_per_iter_max > 0) {
      ++touched_positive;
    }

    if (!check(st.max_violation_certified <= 1e-12,
               "churn test output must remain certified-feasible")) {
      return false;
    }

    if (!check(st.touched_blocks_per_iter_max <= layout.B,
               "touched_blocks_per_iter_max exceeds block count")) {
      return false;
    }
  }

  if (!check(touched_positive > 400, "expected significant touched-block activity in churn run")) {
    return false;
  }

  // Fallbacks are allowed, but should remain uncommon in this stress profile.
  return check(fallback_count < 500, "fallback frequency too high in churn run");
}

bool test_stage5_tau_cap_triggers_fallback() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(16, {4, 4, 4, 4}, &layout, &why)) {
    std::cerr << "FAIL: layout build failed: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 6, 4101);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(16, 3, 2, 4102);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 2.0, 0.05, 4103);

  spqs::Arena arena(1U << 20U);
  spqs::ViolatorOracle oracle(&local, &global, 8.0);
  spqs::StreamingProjector projector(&local, &global, &oracle, &arena);

  spqs::ProjectorOptions opts;
  opts.a_max = 16;
  opts.I_max = 120;
  opts.warm_start = false;
  opts.strict_interior = true;
  opts.kappa_min = 1e-6;
  opts.tau_shrink_min = 0.0;
  opts.tau_shrink_max = 1e-18;  // force fail-closed fallback on tau requirement
  opts.fallback_enabled = true;
  projector.set_options(opts);
  projector.set_rhs(rhs);

  std::vector<double> q_prop(16, 5.0);
  std::vector<double> q_out(16, 0.0);
  const spqs::SolverStats st = projector.project(q_prop.data(), q_out.data());

  spqs::BruteForceScan scan(&local, &global, 8.0);
  if (!check(st.fallback_used, "tau cap violation should trigger fallback")) {
    return false;
  }
  if (!check(st.fallback_alpha >= 0.0 && st.fallback_alpha <= 1.0,
             "fallback alpha must be in [0,1]")) {
    return false;
  }
  return check(scan.certified_feasible(q_out.data(), rhs),
               "fallback output must remain certified feasible");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_layout_and_structure_basics() && ok;
  ok = test_projector_matches_exact_reference_small() && ok;
  ok = test_churn_stress_feasible() && ok;
  ok = test_stage5_tau_cap_triggers_fallback() && ok;

  if (ok) {
    std::cout << "test_projector_vs_reference: PASS\n";
    return 0;
  }
  std::cerr << "test_projector_vs_reference: FAIL\n";
  return 1;
}
