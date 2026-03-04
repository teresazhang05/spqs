#include "spqs/cholesky_update.hpp"

#include "spqs/assert.hpp"
#include "spqs/linalg_small.hpp"

namespace spqs {

bool factorize_active_gram(const std::vector<double>& gram, int n, ActiveFactor* out) {
  SPQS_CHECK(out != nullptr, "factorize_active_gram requires output pointer");

  ActiveFactor f;
  f.n = n;
  f.gram = gram;

  std::vector<double> x_dummy;
  if (!solve_spd_with_jitter(gram, std::vector<double>(static_cast<std::size_t>(n), 0.0),
                             n, &x_dummy, &f.jitter_used)) {
    return false;
  }

  f.l_factor = gram;
  if (f.jitter_used > 0.0) {
    for (int i = 0; i < n; ++i) {
      f.l_factor[static_cast<std::size_t>(i) * n + i] += f.jitter_used;
    }
  }

  if (!cholesky_factorize_lower(&f.l_factor, n)) {
    return false;
  }

  *out = std::move(f);
  return true;
}

bool solve_active_gram(const ActiveFactor& factor,
                       const std::vector<double>& rhs,
                       std::vector<double>* x_out) {
  SPQS_CHECK(factor.n >= 0, "solve_active_gram invalid factor.n");
  return cholesky_solve_lower(factor.l_factor, rhs, factor.n, x_out);
}

}  // namespace spqs
