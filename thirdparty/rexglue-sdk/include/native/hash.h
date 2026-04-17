#pragma once

#include <cstddef>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif

#include <xxhash.h>

namespace rex {

template <typename Key>
struct IdentityHasher {
  size_t operator()(const Key& key) const {
    return static_cast<size_t>(key);
  }
};

template <typename Key>
struct XXHasher {
  size_t operator()(const Key& key) const {
    return static_cast<size_t>(XXH3_64bits(&key, sizeof(key)));
  }
};

}  // namespace rex
