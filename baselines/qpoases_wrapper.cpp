#include "spqs/baselines.hpp"

namespace spqs {

BaselineSolveResult solve_qpoases_projection_baseline_stub() {
  BaselineSolveResult out;
  out.backend = "QPOASES";
  out.available = false;
  out.success = false;
  out.detail = "qpOASES wrapper deferred; build with -DSPQS_ENABLE_QPOASES=ON";
  return out;
}

}  // namespace spqs
