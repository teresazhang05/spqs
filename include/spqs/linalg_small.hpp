#pragma once

#include <vector>

namespace spqs {

bool cholesky_factorize_lower(std::vector<double>* a_lower, int n);
bool cholesky_solve_lower(const std::vector<double>& l_lower,
                          const std::vector<double>& rhs,
                          int n,
                          std::vector<double>* x_out);
bool cholesky_solve_lower_scratch(const std::vector<double>& l_lower,
                                  const std::vector<double>& rhs,
                                  int n,
                                  std::vector<double>* y_scratch,
                                  std::vector<double>* x_out);

bool solve_spd_with_jitter(const std::vector<double>& gram,
                           const std::vector<double>& rhs,
                           int n,
                           std::vector<double>* x_out,
                           double* jitter_used);

}  // namespace spqs
