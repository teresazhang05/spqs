#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "spqs/block_layout.hpp"
#include "spqs/config.hpp"

namespace spqs {

enum class StreamMode {
  IID_DENSE = 0,
  CORRELATED_BLOCK_SPARSE = 1,
  CORRELATED_BLOCK_SPARSE_BURST = 2,
};

struct StreamTickInfo {
  bool in_burst = false;
  int changed_blocks_count = 0;
  double delta_l2 = 0.0;
};

StreamMode parse_stream_mode_or_default(const std::string& mode);

class CorrelatedBlockSparseStream {
 public:
  CorrelatedBlockSparseStream(const StreamConfig& cfg,
                              const BlockLayout& layout);

  void next(std::uint64_t tick,
            double* q_prop_n,
            std::vector<int>* changed_blocks_out,
            StreamTickInfo* info_out);

 private:
  bool in_burst_window(std::uint64_t tick) const;
  void choose_churn_params(std::uint64_t tick, int* k_out, double* delta_out, bool* in_burst_out);
  void sample_unique_blocks(int k, std::vector<int>* blocks_out);

  StreamConfig cfg_;
  const BlockLayout* layout_;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> unif01_;
  std::vector<double> q_prev_;
  std::vector<int> all_blocks_scratch_;
};

}  // namespace spqs
