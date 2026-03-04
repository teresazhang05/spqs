#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "spqs/chol_rank1.hpp"
#include "spqs/linalg_small.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
  double s = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    s += a[i] * b[i];
  }
  return s;
}

std::vector<double> build_gram(const std::vector<int>& active,
                               const std::vector<std::vector<double>>& pool) {
  const int k = static_cast<int>(active.size());
  std::vector<double> gram(static_cast<std::size_t>(k) * static_cast<std::size_t>(k), 0.0);
  for (int i = 0; i < k; ++i) {
    for (int j = 0; j <= i; ++j) {
      const double v = dot(pool[static_cast<std::size_t>(active[static_cast<std::size_t>(i)])],
                           pool[static_cast<std::size_t>(active[static_cast<std::size_t>(j)])]);
      gram[static_cast<std::size_t>(i) * k + j] = v;
      gram[static_cast<std::size_t>(j) * k + i] = v;
    }
  }
  return gram;
}

std::vector<double> reconstruct_from_chol(const std::vector<double>& l, int n) {
  std::vector<double> gram(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double s = 0.0;
      for (int t = 0; t <= std::min(i, j); ++t) {
        s += l[static_cast<std::size_t>(i) * n + t] * l[static_cast<std::size_t>(j) * n + t];
      }
      gram[static_cast<std::size_t>(i) * n + j] = s;
      gram[static_cast<std::size_t>(j) * n + i] = s;
    }
  }
  return gram;
}

bool test_rank1_insert_remove_matches_refactor() {
  constexpr int kVecDim = 24;
  constexpr int kPoolSize = 80;
  constexpr int kMaxActive = 16;
  constexpr int kSteps = 3000;
  constexpr double kDiagEps = 1e-14;
  constexpr double kTol = 1e-8;

  std::mt19937_64 rng(7301);
  std::normal_distribution<double> nd(0.0, 1.0);

  std::vector<std::vector<double>> pool(static_cast<std::size_t>(kPoolSize),
                                        std::vector<double>(kVecDim, 0.0));
  for (int i = 0; i < kPoolSize; ++i) {
    double norm2 = 0.0;
    for (int j = 0; j < kVecDim; ++j) {
      const double v = nd(rng);
      pool[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = v;
      norm2 += v * v;
    }
    const double norm = std::sqrt(std::max(norm2, 1e-30));
    for (int j = 0; j < kVecDim; ++j) {
      pool[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] /= norm;
    }
  }

  std::vector<int> active;
  std::vector<double> l_factor;

  std::uniform_real_distribution<double> action_ud(0.0, 1.0);
  for (int step = 0; step < kSteps; ++step) {
    const bool can_add = static_cast<int>(active.size()) < kMaxActive;
    const bool can_remove = !active.empty();
    const bool do_add = can_add && (!can_remove || action_ud(rng) < 0.6);

    if (do_add) {
      std::vector<int> candidates;
      candidates.reserve(kPoolSize);
      for (int idx = 0; idx < kPoolSize; ++idx) {
        if (std::find(active.begin(), active.end(), idx) == active.end()) {
          candidates.push_back(idx);
        }
      }
      if (!check(!candidates.empty(), "no add candidates despite can_add")) {
        return false;
      }
      std::uniform_int_distribution<int> pick_dist(0, static_cast<int>(candidates.size()) - 1);
      const int new_idx = candidates[static_cast<std::size_t>(pick_dist(rng))];

      const int n = static_cast<int>(active.size());
      std::vector<double> g(static_cast<std::size_t>(n), 0.0);
      for (int i = 0; i < n; ++i) {
        g[static_cast<std::size_t>(i)] =
            dot(pool[static_cast<std::size_t>(active[static_cast<std::size_t>(i)])],
                pool[static_cast<std::size_t>(new_idx)]);
      }
      const double diag = dot(pool[static_cast<std::size_t>(new_idx)],
                              pool[static_cast<std::size_t>(new_idx)]);
      if (!check(spqs::chol_append(&l_factor, n, g, diag, kDiagEps), "chol_append failed")) {
        return false;
      }
      active.push_back(new_idx);
    } else if (can_remove) {
      std::uniform_int_distribution<int> remove_dist(0, static_cast<int>(active.size()) - 1);
      const int remove_pos = remove_dist(rng);
      const int n = static_cast<int>(active.size());
      if (!check(spqs::chol_remove(&l_factor, n, remove_pos, kDiagEps), "chol_remove failed")) {
        return false;
      }
      active.erase(active.begin() + remove_pos);
    }

    const int n = static_cast<int>(active.size());
    if (n == 0) {
      if (!check(l_factor.empty(), "empty active set must have empty factor")) {
        return false;
      }
      continue;
    }

    if (!check(static_cast<int>(l_factor.size()) == n * n, "factor size mismatch")) {
      return false;
    }

    const std::vector<double> gram_ref = build_gram(active, pool);
    std::vector<double> l_ref = gram_ref;
    if (!check(spqs::cholesky_factorize_lower(&l_ref, n), "reference factorization failed")) {
      return false;
    }

    const std::vector<double> gram_from_rank1 = reconstruct_from_chol(l_factor, n);
    double worst_err = 0.0;
    for (int i = 0; i < n; ++i) {
      for (int j = 0; j < n; ++j) {
        const double err = std::abs(
            gram_from_rank1[static_cast<std::size_t>(i) * n + j] -
            gram_ref[static_cast<std::size_t>(i) * n + j]);
        worst_err = std::max(worst_err, err);
      }
    }
    if (!check(worst_err <= kTol, "rank1 factor reconstruction mismatch")) {
      std::cerr << "debug: step=" << step << " n=" << n << " worst_err=" << worst_err << "\n";
      return false;
    }
  }

  return true;
}

}  // namespace

int main() {
  const bool ok = test_rank1_insert_remove_matches_refactor();
  if (ok) {
    std::cout << "test_chol_rank1_insert_remove_matches_refactor: PASS\n";
    return 0;
  }
  std::cerr << "test_chol_rank1_insert_remove_matches_refactor: FAIL\n";
  return 1;
}
