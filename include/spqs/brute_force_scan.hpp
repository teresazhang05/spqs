#pragma once

#include <vector>

#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/rhs.hpp"
#include "spqs/violator_types.hpp"

namespace spqs {

class BruteForceScan {
 public:
  BruteForceScan(const ConstraintsLocal* local,
                 const ConstraintsGlobal* global,
                 double tau_abs_scale);

  [[nodiscard]] Violator max_violation(const double* q_n,
                                       const RHSAll& rhs) const;
  [[nodiscard]] bool certified_feasible(const double* q_n,
                                        const RHSAll& rhs) const;
  [[nodiscard]] double min_certified_slack(const double* q_n,
                                           const RHSAll& rhs) const;

  [[nodiscard]] bool audit_feasible_long_double(const double* q_n,
                                                const RHSAll& rhs,
                                                long double tol = 0.0L) const;

 private:
  const ConstraintsLocal* local_;
  const ConstraintsGlobal* global_;
  double tau_abs_scale_;
};

}  // namespace spqs
