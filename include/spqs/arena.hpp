#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spqs {

class Arena {
 public:
  explicit Arena(std::size_t bytes = 0) : buffer_(bytes, 0U), cursor_(0) {}

  void reset() noexcept { cursor_ = 0; }

  [[nodiscard]] std::size_t bytes() const noexcept { return buffer_.size(); }

  void* allocate(std::size_t nbytes) {
    const std::size_t next = cursor_ + nbytes;
    if (next > buffer_.size()) {
      return nullptr;
    }
    void* ptr = buffer_.data() + cursor_;
    cursor_ = next;
    return ptr;
  }

 private:
  std::vector<std::uint8_t> buffer_;
  std::size_t cursor_;
};

}  // namespace spqs
