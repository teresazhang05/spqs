#pragma once

#include <string>
#include <vector>

namespace spqs {

struct BlockLayout {
  int n = 0;
  int B = 0;
  std::vector<int> block_sizes;
  std::vector<int> block_offsets;
  std::vector<int> var_to_block;

  [[nodiscard]] bool invariant_ok(std::string* reason = nullptr) const;
};

bool make_block_layout(int n,
                       const std::vector<int>& block_sizes,
                       BlockLayout* out,
                       std::string* reason = nullptr);

}  // namespace spqs
