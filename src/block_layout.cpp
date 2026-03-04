#include "spqs/block_layout.hpp"

#include <numeric>

namespace spqs {

bool BlockLayout::invariant_ok(std::string* reason) const {
  if (n <= 0) {
    if (reason != nullptr) {
      *reason = "n must be positive";
    }
    return false;
  }
  if (B <= 0) {
    if (reason != nullptr) {
      *reason = "B must be positive";
    }
    return false;
  }
  if (static_cast<int>(block_sizes.size()) != B) {
    if (reason != nullptr) {
      *reason = "block_sizes length must equal B";
    }
    return false;
  }
  if (static_cast<int>(block_offsets.size()) != B) {
    if (reason != nullptr) {
      *reason = "block_offsets length must equal B";
    }
    return false;
  }
  if (block_offsets.empty() || block_offsets.front() != 0) {
    if (reason != nullptr) {
      *reason = "block_offsets[0] must be 0";
    }
    return false;
  }
  for (int r = 1; r < B; ++r) {
    if (block_offsets[r] <= block_offsets[r - 1]) {
      if (reason != nullptr) {
        *reason = "block_offsets must be strictly increasing";
      }
      return false;
    }
  }
  for (int size : block_sizes) {
    if (size <= 0) {
      if (reason != nullptr) {
        *reason = "all block sizes must be positive";
      }
      return false;
    }
  }
  const int sum = std::accumulate(block_sizes.begin(), block_sizes.end(), 0);
  if (sum != n) {
    if (reason != nullptr) {
      *reason = "sum(block_sizes) must equal n";
    }
    return false;
  }
  if (static_cast<int>(var_to_block.size()) != n) {
    if (reason != nullptr) {
      *reason = "var_to_block length must equal n";
    }
    return false;
  }
  for (int j = 0; j < n; ++j) {
    if (var_to_block[j] < 0 || var_to_block[j] >= B) {
      if (reason != nullptr) {
        *reason = "var_to_block contains invalid block id";
      }
      return false;
    }
  }
  return true;
}

bool make_block_layout(int n,
                       const std::vector<int>& block_sizes,
                       BlockLayout* out,
                       std::string* reason) {
  if (out == nullptr) {
    if (reason != nullptr) {
      *reason = "output BlockLayout pointer is null";
    }
    return false;
  }
  if (n <= 0) {
    if (reason != nullptr) {
      *reason = "n must be positive";
    }
    return false;
  }
  if (block_sizes.empty()) {
    if (reason != nullptr) {
      *reason = "block_sizes must be non-empty";
    }
    return false;
  }

  BlockLayout layout;
  layout.n = n;
  layout.B = static_cast<int>(block_sizes.size());
  layout.block_sizes = block_sizes;
  layout.block_offsets.resize(layout.B, 0);
  layout.var_to_block.resize(n, -1);

  int running = 0;
  for (int r = 0; r < layout.B; ++r) {
    if (block_sizes[r] <= 0) {
      if (reason != nullptr) {
        *reason = "block sizes must all be positive";
      }
      return false;
    }
    layout.block_offsets[r] = running;
    for (int j = 0; j < block_sizes[r]; ++j) {
      if (running + j >= n) {
        if (reason != nullptr) {
          *reason = "sum(block_sizes) exceeds n";
        }
        return false;
      }
      layout.var_to_block[running + j] = r;
    }
    running += block_sizes[r];
  }

  if (running != n) {
    if (reason != nullptr) {
      *reason = "sum(block_sizes) must equal n";
    }
    return false;
  }

  std::string local_reason;
  if (!layout.invariant_ok(&local_reason)) {
    if (reason != nullptr) {
      *reason = "layout invariant failed: " + local_reason;
    }
    return false;
  }

  *out = std::move(layout);
  if (reason != nullptr) {
    *reason = "ok";
  }
  return true;
}

}  // namespace spqs
