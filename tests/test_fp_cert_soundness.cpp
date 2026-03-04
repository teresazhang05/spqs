#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/brute_force_scan.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/fp_cert.hpp"
#include "spqs/rhs.hpp"
#include "spqs/safefallback.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool test_dot_error_bound() {
  std::mt19937_64 rng(111);
  std::normal_distribution<double> nd(0.0, 1.0);

  constexpr int len = 32;
  std::vector<double> a(len, 0.0);
  std::vector<double> q(len, 0.0);

  for (int trial = 0; trial < 5000; ++trial) {
    for (int i = 0; i < len; ++i) {
      a[static_cast<std::size_t>(i)] = nd(rng);
      q[static_cast<std::size_t>(i)] = nd(rng);
    }

    const spqs::CertifiedDot cd = spqs::dot_certified_fma(a.data(), q.data(), len);

    long double exact = 0.0L;
    for (int i = 0; i < len; ++i) {
      exact += static_cast<long double>(a[static_cast<std::size_t>(i)]) *
               static_cast<long double>(q[static_cast<std::size_t>(i)]);
    }

    const long double err = std::abs(static_cast<long double>(cd.s_hat) - exact);
    const long double bound = static_cast<long double>(spqs::gamma_s(len)) *
                              static_cast<long double>(cd.t_abs);

    if (!check(err <= bound * 1.000000000001L + 1e-24L,
               "dot error exceeded certified gamma_s bound")) {
      return false;
    }
  }
  return true;
}

bool test_certified_feasible_implies_audit_feasible() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(64, {16, 16, 16, 16}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 8, 777);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(64, 4, 2, 778);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 10.0, 0.2, 779);

  spqs::BruteForceScan scan(&local, &global, 8.0);

  std::vector<double> q0(64, 0.0);
  if (!check(scan.certified_feasible(q0.data(), rhs), "q=0 should be certified feasible")) return false;
  if (!check(scan.audit_feasible_long_double(q0.data(), rhs), "q=0 should pass long double audit")) return false;

  std::mt19937_64 rng(780);
  std::uniform_real_distribution<double> small(-1e-3, 1e-3);

  int feasible_count = 0;
  for (int t = 0; t < 3000; ++t) {
    for (double& v : q0) {
      v = small(rng);
    }
    if (scan.certified_feasible(q0.data(), rhs)) {
      ++feasible_count;
      if (!check(scan.audit_feasible_long_double(q0.data(), rhs),
                 "certified feasible sample failed long double audit")) {
        return false;
      }
    }
  }

  return check(feasible_count > 100, "too few certified-feasible random samples; test not meaningful");
}

bool test_tau_required_and_interiorization() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(32, {8, 8, 8, 8}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 8, 1701);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(32, 4, 2, 1702);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 5.0, 0.1, 1703);

  std::mt19937_64 rng(1704);
  std::uniform_real_distribution<double> ud(-3.0, 3.0);

  std::vector<double> q_prop(32, 0.0);
  for (double& x : q_prop) {
    x = ud(rng);
  }

  std::vector<double> q_anchor(32, 0.0);
  std::vector<double> q_feas(32, 0.0);
  spqs::safe_fallback_ray_scale_to_anchor(
      local, global, rhs, q_prop.data(), q_anchor.data(), 8.0, q_feas.data(), nullptr);

  spqs::BruteForceScan scan(&local, &global, 8.0);
  if (!check(scan.certified_feasible(q_feas.data(), rhs),
             "ray-scale output must be certified feasible")) {
    return false;
  }

  spqs::InteriorizationReport rep;
  spqs::compute_tau_required_to_anchor_certified(
      local, global, rhs, q_feas.data(), q_anchor.data(), 8.0, 1e-6, &rep);

  if (!check(rep.kappa_valid, "kappa validation should pass for generated RHS")) {
    return false;
  }
  if (!check(rep.tau_required >= 0.0 && std::isfinite(rep.tau_required),
             "tau_required must be finite and nonnegative")) {
    return false;
  }

  const double tau = std::max(rep.tau_required, 1e-6);
  std::vector<double> q_out(32, 0.0);
  for (int j = 0; j < 32; ++j) {
    q_out[static_cast<std::size_t>(j)] = (1.0 - tau) * q_feas[static_cast<std::size_t>(j)];
  }

  if (!check(scan.certified_feasible(q_out.data(), rhs),
             "interiorized output must remain certified feasible")) {
    return false;
  }

  const double min_slack = scan.min_certified_slack(q_out.data(), rhs);
  return check(min_slack > 0.0, "interiorized output must have strict positive certified slack");
}

bool test_ray_scale_fallback_certified() {
  spqs::BlockLayout layout;
  std::string why;
  if (!spqs::make_block_layout(16, {4, 4, 4, 4}, &layout, &why)) {
    std::cerr << "FAIL: unable to build layout: " << why << "\n";
    return false;
  }

  const spqs::ConstraintsLocal local = spqs::generate_local_constraints(layout, 6, 2701);
  const spqs::ConstraintsGlobal global = spqs::generate_global_constraints(16, 3, 2, 2702);
  const spqs::RHSAll rhs = spqs::generate_rhs(local, global, 2.0, 0.05, 2703);

  std::vector<double> q_anchor(16, 0.0);
  std::vector<double> q_prop(16, 100.0);
  std::vector<double> q_out(16, 0.0);

  double alpha = -1.0;
  spqs::safe_fallback_ray_scale_to_anchor(
      local, global, rhs, q_prop.data(), q_anchor.data(), 8.0, q_out.data(), &alpha);

  spqs::BruteForceScan scan(&local, &global, 8.0);
  if (!check(alpha >= 0.0 && alpha <= 1.0, "fallback alpha must lie in [0,1]")) {
    return false;
  }
  return check(scan.certified_feasible(q_out.data(), rhs),
               "ray-scale fallback output must be certified feasible");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_dot_error_bound() && ok;
  ok = test_certified_feasible_implies_audit_feasible() && ok;
  ok = test_tau_required_and_interiorization() && ok;
  ok = test_ray_scale_fallback_certified() && ok;

  if (ok) {
    std::cout << "test_fp_cert_soundness: PASS\n";
    return 0;
  }
  std::cerr << "test_fp_cert_soundness: FAIL\n";
  return 1;
}
