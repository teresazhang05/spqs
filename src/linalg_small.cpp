#include "spqs/linalg_small.hpp"

#include <algorithm>
#include <cmath>

#include "spqs/assert.hpp"

namespace spqs {

bool cholesky_factorize_lower(std::vector<double>* a_lower, int n) {
  SPQS_CHECK(a_lower != nullptr, "cholesky_factorize_lower requires matrix pointer");
  SPQS_CHECK(n >= 0, "cholesky_factorize_lower n must be >= 0");
  SPQS_CHECK(static_cast<int>(a_lower->size()) == n * n,
             "cholesky_factorize_lower matrix size mismatch");

  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double sum = (*a_lower)[static_cast<std::size_t>(i) * n + j];
      for (int k = 0; k < j; ++k) {
        sum -= (*a_lower)[static_cast<std::size_t>(i) * n + k] *
               (*a_lower)[static_cast<std::size_t>(j) * n + k];
      }

      if (i == j) {
        if (!(sum > 0.0)) {
          return false;
        }
        (*a_lower)[static_cast<std::size_t>(i) * n + j] = std::sqrt(sum);
      } else {
        const double diag = (*a_lower)[static_cast<std::size_t>(j) * n + j];
        if (!(diag > 0.0)) {
          return false;
        }
        (*a_lower)[static_cast<std::size_t>(i) * n + j] = sum / diag;
      }
    }

    for (int j = i + 1; j < n; ++j) {
      (*a_lower)[static_cast<std::size_t>(i) * n + j] = 0.0;
    }
  }

  return true;
}

bool cholesky_solve_lower(const std::vector<double>& l_lower,
                          const std::vector<double>& rhs,
                          int n,
                          std::vector<double>* x_out) {
  std::vector<double> y;
  return cholesky_solve_lower_scratch(l_lower, rhs, n, &y, x_out);
}

bool cholesky_solve_lower_scratch(const std::vector<double>& l_lower,
                                  const std::vector<double>& rhs,
                                  int n,
                                  std::vector<double>* y_scratch,
                                  std::vector<double>* x_out) {
  SPQS_CHECK(y_scratch != nullptr, "cholesky_solve_lower_scratch requires y_scratch");
  SPQS_CHECK(x_out != nullptr, "cholesky_solve_lower requires output pointer");
  SPQS_CHECK(n >= 0, "cholesky_solve_lower n must be >= 0");
  SPQS_CHECK(static_cast<int>(l_lower.size()) == n * n,
             "cholesky_solve_lower L size mismatch");
  SPQS_CHECK(static_cast<int>(rhs.size()) == n,
             "cholesky_solve_lower rhs size mismatch");

  x_out->assign(static_cast<std::size_t>(n), 0.0);
  y_scratch->assign(static_cast<std::size_t>(n), 0.0);

  for (int i = 0; i < n; ++i) {
    double sum = rhs[static_cast<std::size_t>(i)];
    for (int j = 0; j < i; ++j) {
      sum -= l_lower[static_cast<std::size_t>(i) * n + j] * (*y_scratch)[static_cast<std::size_t>(j)];
    }
    const double diag = l_lower[static_cast<std::size_t>(i) * n + i];
    if (!(diag > 0.0)) {
      return false;
    }
    (*y_scratch)[static_cast<std::size_t>(i)] = sum / diag;
  }

  for (int i = n - 1; i >= 0; --i) {
    double sum = (*y_scratch)[static_cast<std::size_t>(i)];
    for (int j = i + 1; j < n; ++j) {
      sum -= l_lower[static_cast<std::size_t>(j) * n + i] * (*x_out)[static_cast<std::size_t>(j)];
    }
    const double diag = l_lower[static_cast<std::size_t>(i) * n + i];
    if (!(diag > 0.0)) {
      return false;
    }
    (*x_out)[static_cast<std::size_t>(i)] = sum / diag;
  }

  return true;
}

bool solve_spd_with_jitter(const std::vector<double>& gram,
                           const std::vector<double>& rhs,
                           int n,
                           std::vector<double>* x_out,
                           double* jitter_used) {
  SPQS_CHECK(x_out != nullptr, "solve_spd_with_jitter requires output pointer");
  SPQS_CHECK(n >= 0, "solve_spd_with_jitter n must be >= 0");
  SPQS_CHECK(static_cast<int>(gram.size()) == n * n,
             "solve_spd_with_jitter gram size mismatch");
  SPQS_CHECK(static_cast<int>(rhs.size()) == n,
             "solve_spd_with_jitter rhs size mismatch");

  static constexpr double kJitters[] = {0.0, 1e-12, 1e-10, 1e-8, 1e-6};

  for (double jitter : kJitters) {
    std::vector<double> a = gram;
    if (jitter > 0.0) {
      for (int i = 0; i < n; ++i) {
        a[static_cast<std::size_t>(i) * n + i] += jitter;
      }
    }

    if (!cholesky_factorize_lower(&a, n)) {
      continue;
    }
    if (!cholesky_solve_lower(a, rhs, n, x_out)) {
      continue;
    }

    if (jitter_used != nullptr) {
      *jitter_used = jitter;
    }
    return true;
  }

  if (jitter_used != nullptr) {
    *jitter_used = -1.0;
  }
  return false;
}

}  // namespace spqs
