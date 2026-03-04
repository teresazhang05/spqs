#pragma once

#include <vector>

namespace spqs {

struct ActiveFactor {
  int n = 0;
  std::vector<double> gram;
  std::vector<double> l_factor;
  double jitter_used = 0.0;
};

bool factorize_active_gram(const std::vector<double>& gram, int n, ActiveFactor* out);
bool solve_active_gram(const ActiveFactor& factor,
                       const std::vector<double>& rhs,
                       std::vector<double>* x_out);

}  // namespace spqs
