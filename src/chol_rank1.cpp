#include "spqs/chol_rank1.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "spqs/assert.hpp"
#include "spqs/linalg_small.hpp"

namespace spqs {
namespace {

thread_local std::vector<double> tl_gram_scratch;
thread_local std::vector<double> tl_l_before_scratch;
thread_local std::vector<double> tl_out_scratch;
thread_local std::vector<double> tl_y_scratch;
thread_local std::vector<double> tl_r_upper_scratch;

bool chol_remove_refactor_fallback(const std::vector<double>& l_old,
                                   int n,
                                   int remove_pos,
                                   std::vector<double>* l_out) {
  const int new_n = n - 1;
  tl_gram_scratch.assign(static_cast<std::size_t>(new_n) * static_cast<std::size_t>(new_n), 0.0);

  for (int i_new = 0; i_new < new_n; ++i_new) {
    const int i_old = (i_new < remove_pos) ? i_new : (i_new + 1);
    for (int j_new = 0; j_new <= i_new; ++j_new) {
      const int j_old = (j_new < remove_pos) ? j_new : (j_new + 1);
      double s = 0.0;
      for (int t = 0; t < n; ++t) {
        s += l_old[static_cast<std::size_t>(i_old) * n + t] *
             l_old[static_cast<std::size_t>(j_old) * n + t];
      }
      tl_gram_scratch[static_cast<std::size_t>(i_new) * new_n + j_new] = s;
      tl_gram_scratch[static_cast<std::size_t>(j_new) * new_n + i_new] = s;
    }
  }

  if (!cholesky_factorize_lower(&tl_gram_scratch, new_n)) {
    return false;
  }
  l_out->assign(tl_gram_scratch.begin(), tl_gram_scratch.end());
  return true;
}

bool chol_append_refactor_fallback(const std::vector<double>& l_old,
                                   int n,
  const std::vector<double>& g,
                                   double diag,
                                   std::vector<double>* l_out) {
  const int new_n = n + 1;
  tl_gram_scratch.assign(static_cast<std::size_t>(new_n) * static_cast<std::size_t>(new_n), 0.0);

  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double s = 0.0;
      for (int t = 0; t < n; ++t) {
        s += l_old[static_cast<std::size_t>(i) * n + t] *
             l_old[static_cast<std::size_t>(j) * n + t];
      }
      tl_gram_scratch[static_cast<std::size_t>(i) * new_n + j] = s;
      tl_gram_scratch[static_cast<std::size_t>(j) * new_n + i] = s;
    }
  }

  for (int i = 0; i < n; ++i) {
    tl_gram_scratch[static_cast<std::size_t>(i) * new_n + n] = g[static_cast<std::size_t>(i)];
    tl_gram_scratch[static_cast<std::size_t>(n) * new_n + i] = g[static_cast<std::size_t>(i)];
  }
  tl_gram_scratch[static_cast<std::size_t>(n) * new_n + n] = diag;

  if (!cholesky_factorize_lower(&tl_gram_scratch, new_n)) {
    return false;
  }
  l_out->assign(tl_gram_scratch.begin(), tl_gram_scratch.end());
  return true;
}

}  // namespace

void chol_rank1_prealloc(int max_n) {
  if (max_n <= 0) {
    return;
  }
  const std::size_t n = static_cast<std::size_t>(max_n);
  const std::size_t sq = n * n;
  tl_gram_scratch.reserve(sq);
  tl_l_before_scratch.reserve(sq);
  tl_out_scratch.reserve(sq);
  tl_y_scratch.reserve(n);
  tl_r_upper_scratch.reserve(sq);
}

bool chol_append(std::vector<double>* l_lower,
                 int n,
                 const std::vector<double>& g,
                 double diag,
                 double diag_eps) {
  SPQS_CHECK(l_lower != nullptr, "chol_append requires non-null l_lower");
  SPQS_CHECK(n >= 0, "chol_append n must be >= 0");
  SPQS_CHECK(static_cast<int>(g.size()) == n, "chol_append g size mismatch");
  SPQS_CHECK(static_cast<int>(l_lower->size()) == n * n, "chol_append l_lower size mismatch");

  const int new_n = n + 1;
  tl_l_before_scratch.assign(l_lower->begin(), l_lower->end());
  tl_out_scratch.assign(static_cast<std::size_t>(new_n) * static_cast<std::size_t>(new_n), 0.0);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      tl_out_scratch[static_cast<std::size_t>(i) * new_n + j] =
          (*l_lower)[static_cast<std::size_t>(i) * n + j];
    }
  }

  tl_y_scratch.assign(static_cast<std::size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    double s = g[static_cast<std::size_t>(i)];
    for (int j = 0; j < i; ++j) {
      s -= tl_out_scratch[static_cast<std::size_t>(i) * new_n + j] *
           tl_y_scratch[static_cast<std::size_t>(j)];
    }
    const double lii = tl_out_scratch[static_cast<std::size_t>(i) * new_n + i];
    if (!(lii > diag_eps)) {
      return chol_append_refactor_fallback(tl_l_before_scratch, n, g, diag, l_lower);
    }
    tl_y_scratch[static_cast<std::size_t>(i)] = s / lii;
  }

  double alpha = diag;
  for (int i = 0; i < n; ++i) {
    const double yi = tl_y_scratch[static_cast<std::size_t>(i)];
    alpha -= yi * yi;
  }
  if (!(alpha > diag_eps)) {
    return chol_append_refactor_fallback(tl_l_before_scratch, n, g, diag, l_lower);
  }

  const int new_row = new_n - 1;
  for (int j = 0; j < n; ++j) {
    tl_out_scratch[static_cast<std::size_t>(new_row) * new_n + j] =
        tl_y_scratch[static_cast<std::size_t>(j)];
  }
  tl_out_scratch[static_cast<std::size_t>(new_row) * new_n + new_row] = std::sqrt(alpha);

  l_lower->assign(tl_out_scratch.begin(), tl_out_scratch.end());
  return true;
}

bool chol_remove(std::vector<double>* l_lower,
                 int n,
                 int remove_pos,
                 double diag_eps) {
  SPQS_CHECK(l_lower != nullptr, "chol_remove requires non-null l_lower");
  SPQS_CHECK(n > 0, "chol_remove n must be > 0");
  SPQS_CHECK(remove_pos >= 0 && remove_pos < n, "chol_remove remove_pos out of range");
  SPQS_CHECK(static_cast<int>(l_lower->size()) == n * n, "chol_remove l_lower size mismatch");

  if (n == 1) {
    l_lower->clear();
    return true;
  }

  tl_l_before_scratch.assign(l_lower->begin(), l_lower->end());
  tl_r_upper_scratch.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      tl_r_upper_scratch[static_cast<std::size_t>(j) * n + i] =
          (*l_lower)[static_cast<std::size_t>(i) * n + j];
    }
  }

  for (int i = remove_pos; i < n - 1; ++i) {
    for (int j = i; j < n; ++j) {
      tl_r_upper_scratch[static_cast<std::size_t>(i) * n + j] =
          tl_r_upper_scratch[static_cast<std::size_t>(i + 1) * n + j];
    }
  }
  for (int j = 0; j < n; ++j) {
    tl_r_upper_scratch[static_cast<std::size_t>(n - 1) * n + j] = 0.0;
  }

  for (int i = remove_pos; i < n - 1; ++i) {
    const double x = tl_r_upper_scratch[static_cast<std::size_t>(i) * n + i];
    const double y = tl_r_upper_scratch[static_cast<std::size_t>(i + 1) * n + i];
    const double r = std::hypot(x, y);
    const double c = (r > 0.0) ? (x / r) : 1.0;
    const double s = (r > 0.0) ? (y / r) : 0.0;

    for (int j = i; j < n; ++j) {
      const double rij = tl_r_upper_scratch[static_cast<std::size_t>(i) * n + j];
      const double rip1j = tl_r_upper_scratch[static_cast<std::size_t>(i + 1) * n + j];
      tl_r_upper_scratch[static_cast<std::size_t>(i) * n + j] = c * rij + s * rip1j;
      tl_r_upper_scratch[static_cast<std::size_t>(i + 1) * n + j] = -s * rij + c * rip1j;
    }
  }

  const int new_n = n - 1;
  tl_out_scratch.assign(static_cast<std::size_t>(new_n) * static_cast<std::size_t>(new_n), 0.0);
  for (int i = 0; i < new_n; ++i) {
    for (int j = 0; j <= i; ++j) {
      tl_out_scratch[static_cast<std::size_t>(i) * new_n + j] =
          tl_r_upper_scratch[static_cast<std::size_t>(j) * n + i];
    }
    if (!(tl_out_scratch[static_cast<std::size_t>(i) * new_n + i] > diag_eps)) {
      return chol_remove_refactor_fallback(tl_l_before_scratch, n, remove_pos, l_lower);
    }
  }

  l_lower->assign(tl_out_scratch.begin(), tl_out_scratch.end());
  return true;
}

}  // namespace spqs
