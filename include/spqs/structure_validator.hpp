#pragma once

#include <string>

#include "spqs/config.hpp"
#include "spqs/constraints_global.hpp"
#include "spqs/constraints_local.hpp"
#include "spqs/types.hpp"

namespace spqs {

struct StructureValidationResult {
  bool structure_valid = false;
  RunMode mode = RunMode::BRUTEFORCE_ONLY;
  std::string reason;
};

StructureValidationResult validate_structure_from_config(const Config& cfg);
StructureValidationResult validate_structure_contract(const ConstraintsLocal& local,
                                                      const ConstraintsGlobal& global);

}  // namespace spqs
