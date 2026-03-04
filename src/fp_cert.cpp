#include "spqs/fp_cert.hpp"

#include <cmath>
#include <limits>

namespace spqs {

CertifiedDot dot_certified_fma(const double* a, const double* q, int len) {
  CertifiedDot out;
  for (int j = 0; j < len; ++j) {
    out.s_hat = std::fma(a[j], q[j], out.s_hat);
    out.t_abs += std::abs(a[j] * q[j]);
  }
  return out;
}

double gamma_s(int s) {
  if (s <= 0) {
    return 0.0;
  }
  const double eps = std::numeric_limits<double>::epsilon();
  const double seps = static_cast<double>(s) * eps;
  const double denom = 1.0 - seps;
  if (denom <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  return seps / denom;
}

double tau_abs(double b_i, double tau_abs_scale) {
  const double eps = std::numeric_limits<double>::epsilon();
  return tau_abs_scale * eps * (std::abs(b_i) + 1.0);
}

double certified_violation_from_dot(double s_hat,
                                    double t_abs,
                                    int s,
                                    double b_i,
                                    double tau_abs_scale) {
  return s_hat - b_i + gamma_s(s) * t_abs + tau_abs(b_i, tau_abs_scale);
}

double certified_slack_from_dot(double s_hat,
                                double t_abs,
                                int s,
                                double b_i,
                                double tau_abs_scale) {
  return b_i - (s_hat + gamma_s(s) * t_abs + tau_abs(b_i, tau_abs_scale));
}

}  // namespace spqs
