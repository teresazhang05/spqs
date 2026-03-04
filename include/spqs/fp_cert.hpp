#pragma once

#include <cstddef>

namespace spqs {

struct CertifiedDot {
  double s_hat = 0.0;
  double t_abs = 0.0;
};

CertifiedDot dot_certified_fma(const double* a, const double* q, int len);
double gamma_s(int s);
double tau_abs(double b_i, double tau_abs_scale);

double certified_violation_from_dot(double s_hat,
                                    double t_abs,
                                    int s,
                                    double b_i,
                                    double tau_abs_scale);

double certified_slack_from_dot(double s_hat,
                                double t_abs,
                                int s,
                                double b_i,
                                double tau_abs_scale);

}  // namespace spqs
