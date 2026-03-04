#include "spqs/structure_validator.hpp"

#include <numeric>

namespace spqs {

StructureValidationResult validate_structure_from_config(const Config& cfg) {
  if (cfg.structure.mode != "block_sparse") {
    return {
        false,
        RunMode::BRUTEFORCE_ONLY,
        "structure.mode != block_sparse, routing to BRUTEFORCE_ONLY",
    };
  }
  if (cfg.structure.B <= 0) {
    return {false, RunMode::BRUTEFORCE_ONLY, "B must be positive"};
  }
  if (static_cast<int>(cfg.structure.block_sizes.size()) != cfg.structure.B) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "block_sizes length must equal B; routing to BRUTEFORCE_ONLY"};
  }
  for (int size : cfg.structure.block_sizes) {
    if (size <= 0) {
      return {false, RunMode::BRUTEFORCE_ONLY, "all block sizes must be positive"};
    }
  }
  const int block_sum = std::accumulate(cfg.structure.block_sizes.begin(),
                                        cfg.structure.block_sizes.end(),
                                        0);
  if (block_sum != cfg.problem.n) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "sum(block_sizes) must equal n; routing to BRUTEFORCE_ONLY"};
  }
  if (cfg.structure.m_local_per_block <= 0) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "m_local_per_block must be positive; routing to BRUTEFORCE_ONLY"};
  }
  if (cfg.structure.m_global < 0) {
    return {false, RunMode::BRUTEFORCE_ONLY, "m_global must be >= 0"};
  }
  return {true, RunMode::STRUCTURED_FAST, "ok"};
}

StructureValidationResult validate_structure_contract(const ConstraintsLocal& local,
                                                      const ConstraintsGlobal& global) {
  std::string layout_reason;
  if (!local.layout.invariant_ok(&layout_reason)) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "layout invariant failed: " + layout_reason};
  }
  if (static_cast<int>(local.A_block.size()) != local.layout.B) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "local.A_block size must equal B"};
  }
  for (int r = 0; r < local.layout.B; ++r) {
    const auto& b = local.A_block[static_cast<std::size_t>(r)];
    if (b.rows != local.m_local_per_block) {
      return {false,
              RunMode::BRUTEFORCE_ONLY,
              "local block rows must equal m_local_per_block"};
    }
    if (b.cols != local.layout.block_sizes[r]) {
      return {false,
              RunMode::BRUTEFORCE_ONLY,
              "local block cols must equal block size"};
    }
    if (b.data.size() != static_cast<std::size_t>(b.rows) * static_cast<std::size_t>(b.cols)) {
      return {false,
              RunMode::BRUTEFORCE_ONLY,
              "local block data has invalid size"};
    }
  }
  if (global.n != local.layout.n) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "global.n must equal local.layout.n"};
  }
  if (global.m_global < 0) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "global.m_global must be >=0"};
  }
  if (global.A.size() != static_cast<std::size_t>(global.n) * static_cast<std::size_t>(global.m_global)) {
    return {false,
            RunMode::BRUTEFORCE_ONLY,
            "global.A size mismatch"};
  }
  return {true, RunMode::STRUCTURED_FAST, "ok"};
}

}  // namespace spqs
