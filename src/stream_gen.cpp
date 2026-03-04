#include "spqs/stream_gen.hpp"

#include <algorithm>
#include <cmath>

#include "spqs/assert.hpp"

namespace spqs {

StreamMode parse_stream_mode_or_default(const std::string& mode) {
  if (mode == "iid_dense") {
    return StreamMode::IID_DENSE;
  }
  if (mode == "correlated_block_sparse") {
    return StreamMode::CORRELATED_BLOCK_SPARSE;
  }
  if (mode == "correlated_block_sparse_burst") {
    return StreamMode::CORRELATED_BLOCK_SPARSE_BURST;
  }
  return StreamMode::IID_DENSE;
}

CorrelatedBlockSparseStream::CorrelatedBlockSparseStream(const StreamConfig& cfg,
                                                         const BlockLayout& layout)
    : cfg_(cfg),
      layout_(&layout),
      rng_(cfg.seed_stream),
      unif01_(0.0, 1.0),
      q_prev_(static_cast<std::size_t>(layout.n), 0.0),
      all_blocks_scratch_(static_cast<std::size_t>(layout.B), 0) {
  SPQS_CHECK(layout_ != nullptr, "CorrelatedBlockSparseStream layout is null");
  for (int r = 0; r < layout_->B; ++r) {
    all_blocks_scratch_[static_cast<std::size_t>(r)] = r;
  }
}

bool CorrelatedBlockSparseStream::in_burst_window(std::uint64_t tick) const {
  if (cfg_.mode != "correlated_block_sparse_burst" || !cfg_.burst.enabled) {
    return false;
  }
  if (cfg_.burst.every_ticks == 0U || cfg_.burst.length_ticks == 0U) {
    return false;
  }
  return (tick % cfg_.burst.every_ticks) < cfg_.burst.length_ticks;
}

void CorrelatedBlockSparseStream::choose_churn_params(std::uint64_t tick,
                                                      int* k_out,
                                                      double* delta_out,
                                                      bool* in_burst_out) {
  SPQS_CHECK(k_out != nullptr, "choose_churn_params k_out is null");
  SPQS_CHECK(delta_out != nullptr, "choose_churn_params delta_out is null");
  SPQS_CHECK(in_burst_out != nullptr, "choose_churn_params in_burst_out is null");

  const bool in_burst = in_burst_window(tick);
  *in_burst_out = in_burst;

  int k = cfg_.K_small;
  double delta = cfg_.delta_small;
  const double u = unif01_(rng_);

  if (in_burst) {
    k = cfg_.burst.K_burst;
    delta = cfg_.burst.delta_burst;
    if (u < cfg_.burst.p_jump_in_burst) {
      k = cfg_.burst.K_jump_in_burst;
      delta = cfg_.burst.delta_jump_in_burst;
    }
  } else {
    if (u < cfg_.p_jump) {
      k = cfg_.K_jump;
      delta = cfg_.delta_jump;
    } else if (u < (cfg_.p_jump + cfg_.p_K_small_alt)) {
      k = cfg_.K_small_alt;
      delta = cfg_.delta_small;
    }
  }

  k = std::max(0, std::min(k, layout_->B));
  if (delta < 0.0) {
    delta = 0.0;
  }
  *k_out = k;
  *delta_out = delta;
}

void CorrelatedBlockSparseStream::sample_unique_blocks(int k,
                                                       std::vector<int>* blocks_out) {
  SPQS_CHECK(blocks_out != nullptr, "sample_unique_blocks blocks_out is null");
  blocks_out->clear();
  if (k <= 0) {
    return;
  }

  std::shuffle(all_blocks_scratch_.begin(), all_blocks_scratch_.end(), rng_);
  blocks_out->assign(all_blocks_scratch_.begin(),
                     all_blocks_scratch_.begin() + k);
  std::sort(blocks_out->begin(), blocks_out->end());
}

void CorrelatedBlockSparseStream::next(std::uint64_t tick,
                                       double* q_prop_n,
                                       std::vector<int>* changed_blocks_out,
                                       StreamTickInfo* info_out) {
  SPQS_CHECK(q_prop_n != nullptr, "CorrelatedBlockSparseStream next q_prop_n is null");
  SPQS_CHECK(changed_blocks_out != nullptr,
             "CorrelatedBlockSparseStream next changed_blocks_out is null");
  SPQS_CHECK(info_out != nullptr, "CorrelatedBlockSparseStream next info_out is null");

  const StreamMode mode = parse_stream_mode_or_default(cfg_.mode);
  changed_blocks_out->clear();

  if (mode == StreamMode::IID_DENSE) {
    std::bernoulli_distribution choose_big(cfg_.p_big);
    std::uniform_real_distribution<double> small(-cfg_.q_small, cfg_.q_small);
    std::uniform_real_distribution<double> big(-cfg_.q_big, cfg_.q_big);
    const bool is_big = choose_big(rng_);

    double delta_l2_sq = 0.0;
    for (int j = 0; j < layout_->n; ++j) {
      const double next = is_big ? big(rng_) : small(rng_);
      const double d = next - q_prev_[static_cast<std::size_t>(j)];
      delta_l2_sq += d * d;
      q_prop_n[j] = next;
      q_prev_[static_cast<std::size_t>(j)] = next;
    }
    changed_blocks_out->resize(static_cast<std::size_t>(layout_->B));
    for (int r = 0; r < layout_->B; ++r) {
      (*changed_blocks_out)[static_cast<std::size_t>(r)] = r;
    }
    info_out->in_burst = false;
    info_out->changed_blocks_count = layout_->B;
    info_out->delta_l2 = std::sqrt(delta_l2_sq);
    return;
  }

  int k = 0;
  double delta = 0.0;
  bool in_burst = false;
  choose_churn_params(tick, &k, &delta, &in_burst);

  // AR drift baseline.
  for (int j = 0; j < layout_->n; ++j) {
    q_prop_n[j] = cfg_.ar_rho * q_prev_[static_cast<std::size_t>(j)];
  }

  sample_unique_blocks(k, changed_blocks_out);
  std::uniform_real_distribution<double> ud(-delta, delta);

  for (int block_id : *changed_blocks_out) {
    const int offset = layout_->block_offsets[block_id];
    const int block_size = layout_->block_sizes[block_id];
    for (int j = 0; j < block_size; ++j) {
      q_prop_n[offset + j] += ud(rng_);
    }
  }

  const double clamp_inf = std::max(0.0, cfg_.clamp_inf);
  double delta_l2_sq = 0.0;
  for (int j = 0; j < layout_->n; ++j) {
    if (clamp_inf > 0.0) {
      q_prop_n[j] = std::max(-clamp_inf, std::min(clamp_inf, q_prop_n[j]));
    }
    const double d = q_prop_n[j] - q_prev_[static_cast<std::size_t>(j)];
    delta_l2_sq += d * d;
    q_prev_[static_cast<std::size_t>(j)] = q_prop_n[j];
  }

  info_out->in_burst = in_burst;
  info_out->changed_blocks_count = static_cast<int>(changed_blocks_out->size());
  info_out->delta_l2 = std::sqrt(delta_l2_sq);
}

}  // namespace spqs
