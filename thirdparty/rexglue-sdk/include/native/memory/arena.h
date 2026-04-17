// Native runtime - Arena allocator
// Part of the AC6 Recompilation native foundation

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <native/literals.h>

namespace rex::memory {

using namespace rex::literals;

class Arena {
 public:
  explicit Arena(size_t chunk_size = 4_MiB);
  ~Arena();

  void Reset();
  void DebugFill();

  void* Alloc(size_t size, size_t align);
  template <typename T>
  T* Alloc() {
    return reinterpret_cast<T*>(Alloc(sizeof(T), alignof(T)));
  }
  // When rewinding aligned allocations, any padding that was applied during
  // allocation will be leaked
  void Rewind(size_t size);

  void* CloneContents();
  template <typename T>
  void CloneContents(std::vector<T>* buffer) {
    buffer->resize(CalculateSize() / sizeof(T));
    CloneContents(buffer->data(), buffer->size() * sizeof(T));
  }

 private:
  class Chunk {
   public:
    explicit Chunk(size_t chunk_size);
    ~Chunk();

    Chunk* next;

    size_t capacity;
    uint8_t* buffer;
    size_t offset;
  };

  size_t CalculateSize();
  void CloneContents(void* buffer, size_t buffer_length);

  size_t chunk_size_;
  Chunk* head_chunk_;
  Chunk* active_chunk_;
};

}  // namespace rex::memory
