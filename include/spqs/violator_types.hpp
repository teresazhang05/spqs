#pragma once

#include <limits>

namespace spqs {

enum class ConstraintScope {
  LOCAL,
  GLOBAL,
};

struct Violator {
  ConstraintScope scope = ConstraintScope::GLOBAL;
  int block_id = -1;
  int row_in_block = -1;
  int global_row = -1;
  int constraint_id = -1;
  double violation = -std::numeric_limits<double>::infinity();
};

bool prefer_lhs(const Violator& lhs, const Violator& rhs);

}  // namespace spqs
