#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spqs {

class HdrHistogram {
 public:
  void record(std::uint64_t value_ns) { samples_.push_back(value_ns); }

  [[nodiscard]] std::size_t count() const noexcept { return samples_.size(); }

  [[nodiscard]] std::uint64_t quantile(double q) {
    if (samples_.empty()) {
      return 0;
    }
    std::sort(samples_.begin(), samples_.end());
    const double idx_f = q * static_cast<double>(samples_.size() - 1);
    const std::size_t idx = static_cast<std::size_t>(idx_f);
    return samples_[idx];
  }

 private:
  std::vector<std::uint64_t> samples_;
};

}  // namespace spqs
