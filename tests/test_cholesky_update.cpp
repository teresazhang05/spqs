#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "spqs/cholesky_update.hpp"

namespace {

bool check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    return false;
  }
  return true;
}

bool test_factor_and_solve() {
  // SPD matrix [[4,1,0],[1,3,1],[0,1,2]] with known solution x=[1,2,3]
  const int n = 3;
  std::vector<double> g = {
      4.0, 1.0, 0.0,
      1.0, 3.0, 1.0,
      0.0, 1.0, 2.0,
  };
  std::vector<double> rhs = {6.0, 10.0, 8.0};

  spqs::ActiveFactor factor;
  if (!check(spqs::factorize_active_gram(g, n, &factor), "factorization failed")) {
    return false;
  }

  std::vector<double> x;
  if (!check(spqs::solve_active_gram(factor, rhs, &x), "solve failed")) {
    return false;
  }

  if (!check(x.size() == 3, "solution size mismatch")) {
    return false;
  }

  return check(std::abs(x[0] - 1.0) < 1e-10 &&
                   std::abs(x[1] - 2.0) < 1e-10 &&
                   std::abs(x[2] - 3.0) < 1e-10,
               "solution mismatch");
}

bool test_nearly_singular_regularizes() {
  const int n = 2;
  std::vector<double> g = {
      1.0, 1.0,
      1.0, 1.0,
  };
  std::vector<double> rhs = {1.0, 1.0};

  spqs::ActiveFactor factor;
  if (!check(spqs::factorize_active_gram(g, n, &factor),
             "near-singular factorization should succeed with jitter")) {
    return false;
  }

  std::vector<double> x;
  if (!check(spqs::solve_active_gram(factor, rhs, &x),
             "near-singular solve should succeed")) {
    return false;
  }

  return check(x.size() == 2, "near-singular solution size mismatch");
}

}  // namespace

int main() {
  bool ok = true;
  ok = test_factor_and_solve() && ok;
  ok = test_nearly_singular_regularizes() && ok;

  if (ok) {
    std::cout << "test_cholesky_update: PASS\n";
    return 0;
  }
  std::cerr << "test_cholesky_update: FAIL\n";
  return 1;
}
