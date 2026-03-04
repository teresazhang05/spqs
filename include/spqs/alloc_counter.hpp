#pragma once

#include <cstddef>
#include <cstdint>

namespace spqs {

struct AllocCounters {
  std::uint64_t malloc_calls = 0;
  std::uint64_t calloc_calls = 0;
  std::uint64_t realloc_calls = 0;
  std::uint64_t free_calls = 0;
  std::uint64_t new_calls = 0;
  std::uint64_t delete_calls = 0;
  std::uint64_t bytes_allocated = 0;

  std::uint64_t alloc_calls_total() const {
    return malloc_calls + calloc_calls + realloc_calls + new_calls;
  }
};

void alloc_counter_reset();
void alloc_counter_set_enabled(bool enabled);
bool alloc_counter_enabled();
AllocCounters alloc_counter_snapshot();

void alloc_counter_note_malloc(std::size_t nbytes);
void alloc_counter_note_calloc(std::size_t nbytes);
void alloc_counter_note_realloc(std::size_t nbytes);
void alloc_counter_note_free();
void alloc_counter_note_new(std::size_t nbytes);
void alloc_counter_note_delete();

}  // namespace spqs
