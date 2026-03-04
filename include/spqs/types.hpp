#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>

#include "spqs/assert.hpp"

namespace spqs {

enum class RunMode {
  STRUCTURED_FAST,
  BRUTEFORCE_ONLY,
};

struct AlignedDoubleDeleter {
  void operator()(double* ptr) const noexcept {
    if (ptr != nullptr) {
      ::operator delete[](ptr, std::align_val_t(64));
    }
  }
};

class AlignedDoubles {
 public:
  AlignedDoubles() = default;
  explicit AlignedDoubles(std::size_t count) { resize(count); }

  AlignedDoubles(const AlignedDoubles& other) { *this = other; }

  AlignedDoubles& operator=(const AlignedDoubles& other) {
    if (this == &other) {
      return *this;
    }
    resize(other.size_);
    if (size_ > 0U) {
      std::copy(other.data_.get(), other.data_.get() + static_cast<std::ptrdiff_t>(size_), data_.get());
    }
    return *this;
  }

  AlignedDoubles(AlignedDoubles&&) noexcept = default;
  AlignedDoubles& operator=(AlignedDoubles&&) noexcept = default;

  void resize(std::size_t count) {
    size_ = count;
    if (count == 0U) {
      data_.reset();
      return;
    }
    double* raw = static_cast<double*>(::operator new[](count * sizeof(double), std::align_val_t(64)));
    data_.reset(raw);
    std::fill_n(raw, count, 0.0);
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] double* data() noexcept { return data_.get(); }
  [[nodiscard]] const double* data() const noexcept { return data_.get(); }

  double& operator[](std::size_t idx) {
    SPQS_CHECK(idx < size_, "AlignedDoubles index out of range");
    return data_[idx];
  }

  const double& operator[](std::size_t idx) const {
    SPQS_CHECK(idx < size_, "AlignedDoubles index out of range");
    return data_[idx];
  }

 private:
  std::size_t size_ = 0;
  std::unique_ptr<double[], AlignedDoubleDeleter> data_;
};

}  // namespace spqs
