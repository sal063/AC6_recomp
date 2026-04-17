// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Copyright (c) 2020 Pavel Kirienko
// Authors: Pavel Kirienko <pavel.kirienko@zubax.com>

#include "internal.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>

namespace
{
constexpr std::size_t KiB = 1024U;
constexpr std::size_t MiB = KiB * KiB;

template <typename T>
auto log2Floor(const T& x) -> std::enable_if_t<std::is_integral_v<T>, std::uint8_t>
{
    std::size_t  tmp = x;
    std::uint8_t y   = 0;
    while (tmp > 1U)
    {
        tmp >>= 1U;
        y++;
    }
    return y;
}

auto getRandomByte()
{
    static std::random_device                           rd;
    static std::mt19937                                 gen(rd());
    static std::uniform_int_distribution<std::uint16_t> dis(0, 255U);
    return static_cast<std::byte>(dis(gen));
}

auto init(void* const base, const std::size_t size)
{
    using internal::Fragment;

    // Fill the beginning of the arena with random bytes (the entire arena may be too slow to fill).
    std::generate_n(reinterpret_cast<std::byte*>(base), std::min<std::size_t>(1 * MiB, size), getRandomByte);

    const auto heap = reinterpret_cast<internal::O1HeapInstance*>(o1heapInit(base, size));

    if (heap != nullptr)
    {
        REQUIRE(reinterpret_cast<std::size_t>(heap) % O1HEAP_ALIGNMENT == 0U);

        heap->validate();

        REQUIRE(heap->nonempty_bin_mask > 0U);
        REQUIRE((heap->nonempty_bin_mask & (heap->nonempty_bin_mask - 1U)) == 0);
        for (auto i = 0U; i < std::size(heap->bins); i++)
        {
            const std::size_t min = Fragment::SizeMin << i;
            const std::size_t max = (Fragment::SizeMin << i) * 2U - 1U;
            if ((heap->nonempty_bin_mask & (1ULL << i)) == 0U)
            {
                REQUIRE(heap->bins.at(i) == nullptr);
            }
            else
            {
                REQUIRE(heap->bins.at(i) != nullptr);
                REQUIRE(heap->bins.at(i)->header.getSize(heap) >= min);
                REQUIRE(heap->bins.at(i)->header.getSize(heap) <= max);
            }
        }

        REQUIRE(heap->diagnostics.capacity < size);
        REQUIRE(heap->diagnostics.capacity <= Fragment::SizeMax);
        REQUIRE(heap->diagnostics.capacity >= Fragment::SizeMin);
        REQUIRE(heap->diagnostics.allocated == 0);
        REQUIRE(heap->diagnostics.oom_count == 0);
        REQUIRE(heap->diagnostics.peak_allocated == 0);
        REQUIRE(heap->diagnostics.peak_request_size == 0);

        const auto root_fragment = heap->bins.at(log2Floor(heap->nonempty_bin_mask));
        REQUIRE(root_fragment != nullptr);
        REQUIRE(root_fragment->next_free == nullptr);
        REQUIRE(root_fragment->prev_free == nullptr);
        REQUIRE(!root_fragment->header.isUsed());
        REQUIRE(root_fragment->header.getSize(heap) == heap->diagnostics.capacity);
        REQUIRE(root_fragment->header.getNext() == nullptr);
        REQUIRE(root_fragment->header.getPrev() == nullptr);
    }
    return heap;
}

}  // namespace

TEST_CASE("General: init")
{
    using internal::Fragment;

    std::cout << "sizeof(void*)=" << sizeof(void*) << "; sizeof(O1HeapInstance)=" << sizeof(internal::O1HeapInstance)
              << std::endl;

    alignas(128) std::array<std::byte, 10'000U> arena{};

    REQUIRE(nullptr == init(nullptr, 0U));
    REQUIRE(nullptr == init(arena.data(), 0U));
    REQUIRE(nullptr == init(arena.data(), 99U));  // Too small.

    // Check various offsets and sizes to make sure the initialization is done correctly in all cases.
    for (auto offset = 0U; offset < 7U; offset++)
    {
        for (auto size = 99U; size < 5100U; size += 111U)
        {
            REQUIRE(arena.size() >= size);
            auto heap = init(arena.data() + offset, size - offset);
            if (heap != nullptr)
            {
                REQUIRE(size >= sizeof(internal::O1HeapInstance) + Fragment::SizeMin);
                REQUIRE(reinterpret_cast<std::size_t>(heap) >= reinterpret_cast<std::size_t>(arena.data()));
                REQUIRE(reinterpret_cast<std::size_t>(heap) % O1HEAP_ALIGNMENT == 0U);
                REQUIRE(heap->doInvariantsHold());
            }
        }
    }
}

TEST_CASE("General: allocate: OOM")
{
    constexpr auto                   MiB256    = MiB * 256U;
    constexpr auto                   ArenaSize = MiB256 + MiB;
    const std::shared_ptr<std::byte> arena(static_cast<std::byte*>(std::aligned_alloc(64U, ArenaSize)), &std::free);

    auto heap = init(arena.get(), ArenaSize);
    REQUIRE(heap != nullptr);
    REQUIRE(heap->getDiagnostics().capacity > ArenaSize - 1024U);
    REQUIRE(heap->getDiagnostics().capacity < ArenaSize);
    REQUIRE(heap->getDiagnostics().oom_count == 0);

    REQUIRE(nullptr == heap->allocate(ArenaSize));  // Too large
    REQUIRE(heap->getDiagnostics().oom_count == 1);

    REQUIRE(nullptr == heap->allocate(ArenaSize - O1HEAP_ALIGNMENT));  // Too large
    REQUIRE(heap->getDiagnostics().oom_count == 2);

    REQUIRE(nullptr == heap->allocate(heap->diagnostics.capacity - O1HEAP_ALIGNMENT + 1U));  // Too large
    REQUIRE(heap->getDiagnostics().oom_count == 3);

    REQUIRE(nullptr == heap->allocate(ArenaSize * 10U));  // Too large
    REQUIRE(heap->getDiagnostics().oom_count == 4);

    REQUIRE(nullptr == heap->allocate(0));           // Nothing to allocate
    REQUIRE(heap->getDiagnostics().oom_count == 4);  // Not incremented! Zero allocation is not an OOM.

    REQUIRE(heap->getDiagnostics().peak_allocated == 0);
    REQUIRE(heap->getDiagnostics().allocated == 0);
    REQUIRE(heap->getDiagnostics().peak_request_size == ArenaSize * 10U);

    REQUIRE(nullptr != heap->allocate(MiB256 - O1HEAP_ALIGNMENT));  // Maximum possible allocation.
    REQUIRE(heap->getDiagnostics().oom_count == 4);                 // OOM counter not incremented.
    REQUIRE(heap->getDiagnostics().peak_allocated == MiB256);
    REQUIRE(heap->getDiagnostics().allocated == MiB256);
    REQUIRE(heap->getDiagnostics().peak_request_size == ArenaSize * 10U);  // Same size -- that one was unsuccessful.

    REQUIRE(heap->doInvariantsHold());
}

TEST_CASE("General: allocate: smallest")
{
    using internal::Fragment;

    constexpr auto                   ArenaSize = MiB * 300U;
    const std::shared_ptr<std::byte> arena(static_cast<std::byte*>(std::aligned_alloc(64U, ArenaSize)), &std::free);

    auto heap = init(arena.get(), ArenaSize);
    REQUIRE(heap != nullptr);

    void* const mem = heap->allocate(1U);
    REQUIRE(mem != nullptr);
    REQUIRE(heap->getDiagnostics().oom_count == 0);
    REQUIRE(heap->getDiagnostics().peak_allocated == Fragment::SizeMin);
    REQUIRE(heap->getDiagnostics().allocated == Fragment::SizeMin);
    REQUIRE(heap->getDiagnostics().peak_request_size == 1);

    auto& frag = Fragment::constructFromAllocatedMemory(mem);
    REQUIRE(frag.header.getSize(heap) == (O1HEAP_ALIGNMENT * 2U));
    REQUIRE(frag.header.getNext() != nullptr);
    REQUIRE(frag.header.getPrev() == nullptr);
    REQUIRE(frag.header.isUsed());
    REQUIRE(frag.header.getNext()->header.getSize(heap) == (heap->diagnostics.capacity - frag.header.getSize(heap)));
    REQUIRE(!frag.header.getNext()->header.isUsed());

    heap->free(mem);
    REQUIRE(heap->doInvariantsHold());
}

TEST_CASE("General: allocate: size_t overflow")
{
    using internal::Fragment;

    constexpr auto size_max = std::numeric_limits<std::size_t>::max();

    constexpr auto                   ArenaSize = MiB * 300U;
    const std::shared_ptr<std::byte> arena(static_cast<std::byte*>(std::aligned_alloc(64U, ArenaSize)), &std::free);

    auto heap = init(arena.get(), ArenaSize);
    REQUIRE(heap != nullptr);
    REQUIRE(heap->diagnostics.capacity > (ArenaSize - 1024U));
    REQUIRE(heap->diagnostics.capacity < ArenaSize);
    for (auto i = 1U; i <= 2U; i++)
    {
        REQUIRE(nullptr == heap->allocate(size_max / i));
        REQUIRE(nullptr == heap->allocate(size_max / i + 1U));  // May overflow to 0.
        REQUIRE(nullptr == heap->allocate(size_max / i - 1U));
        REQUIRE(nullptr == heap->allocate(Fragment::SizeMax - O1HEAP_ALIGNMENT + 1U));
    }

    // Over-commit the arena -- it is SMALLER than the size we're providing; it's an UB but for a test it's acceptable.
    heap = init(arena.get(), size_max);
    REQUIRE(heap != nullptr);
    REQUIRE(heap->diagnostics.capacity == Fragment::SizeMax);
    for (auto i = 1U; i <= 2U; i++)
    {
        REQUIRE(nullptr == heap->allocate(size_max / i));
        REQUIRE(nullptr == heap->allocate(size_max / i + 1U));
        REQUIRE(nullptr == heap->allocate(size_max / i - 1U));
        REQUIRE(nullptr == heap->allocate(Fragment::SizeMax - O1HEAP_ALIGNMENT + 1U));
    }

    // Make sure the max-sized fragments are allocatable.
    void* const mem = heap->allocate(Fragment::SizeMax - O1HEAP_ALIGNMENT);
    REQUIRE(mem != nullptr);

    auto& frag = Fragment::constructFromAllocatedMemory(mem);
    REQUIRE(frag.header.getSize(heap) == Fragment::SizeMax);
    REQUIRE(frag.header.getNext() == nullptr);
    REQUIRE(frag.header.getPrev() == nullptr);
    REQUIRE(frag.header.isUsed());

    REQUIRE(heap->getDiagnostics().peak_allocated == Fragment::SizeMax);
    REQUIRE(heap->getDiagnostics().allocated == Fragment::SizeMax);

    REQUIRE(heap->nonempty_bin_mask == 0);
    REQUIRE(std::all_of(std::begin(heap->bins), std::end(heap->bins), [](auto* p) { return p == nullptr; }));

    REQUIRE(heap->doInvariantsHold());
}

TEST_CASE("General: free")
{
    using internal::Fragment;

    alignas(128U) std::array<std::byte, 4096U + sizeof(internal::O1HeapInstance) + O1HEAP_ALIGNMENT - 1U> arena{};
    auto heap = init(arena.data(), std::size(arena));
    REQUIRE(heap != nullptr);

    REQUIRE(nullptr == heap->allocate(0U));
    REQUIRE(heap->diagnostics.allocated == 0U);
    heap->free(nullptr);
    REQUIRE(heap->diagnostics.peak_allocated == 0U);
    REQUIRE(heap->diagnostics.peak_request_size == 0U);
    REQUIRE(heap->diagnostics.oom_count == 0U);

    std::size_t allocated         = 0U;
    std::size_t peak_allocated    = 0U;
    std::size_t peak_request_size = 0U;

    const auto alloc = [&](const std::size_t amount, const std::vector<std::pair<bool, std::size_t>>& reference) {
        const auto p = heap->allocate(amount);
        if (amount > 0U)
        {
            REQUIRE(p != nullptr);

            // Overwrite all to ensure that the allocator does not make implicit assumptions about the memory use.
            std::generate_n(reinterpret_cast<std::byte*>(p), amount, getRandomByte);

            const auto& frag = Fragment::constructFromAllocatedMemory(p);
            REQUIRE(frag.header.isUsed());
            const auto frag_size = frag.header.getSize(heap);
            REQUIRE((frag_size & (frag_size - 1U)) == 0U);
            REQUIRE(frag_size >= (amount + O1HEAP_ALIGNMENT));
            REQUIRE(frag_size <= Fragment::SizeMax);

            allocated += frag_size;
            peak_allocated    = std::max(peak_allocated, allocated);
            peak_request_size = std::max(peak_request_size, amount);
        }
        else
        {
            REQUIRE(p == nullptr);
        }

        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        heap->matchFragments(reference);
        REQUIRE(heap->doInvariantsHold());
        return p;
    };

    const auto dealloc = [&](void* const p, const std::vector<std::pair<bool, std::size_t>>& reference) {
        INFO(heap->visualize());
        if (p != nullptr)
        {
            // Overwrite some to ensure that the allocator does not make implicit assumptions about the memory use.
            std::generate_n(reinterpret_cast<std::byte*>(p), O1HEAP_ALIGNMENT, getRandomByte);

            const auto& frag = Fragment::constructFromAllocatedMemory(p);
            REQUIRE(frag.header.isUsed());
            REQUIRE(allocated >= frag.header.getSize(heap));
            allocated -= frag.header.getSize(heap);
            heap->free(p);
        }
        else
        {
            heap->free(p);
        }
        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        heap->matchFragments(reference);
        REQUIRE(heap->doInvariantsHold());
    };

    constexpr auto X = true;   // used
    constexpr auto O = false;  // free

    auto a = alloc(32U,
                   {
                       {X, 64},
                       {O, 4032},
                   });
    auto b = alloc(32U,
                   {
                       {X, 64},
                       {X, 64},
                       {O, 3968},
                   });
    auto c = alloc(32U,
                   {
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {O, 3904},
                   });
    auto d = alloc(32U,
                   {
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {O, 3840},
                   });
    auto e = alloc(1024U,
                   {
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {X, 2048},
                       {O, 1792},
                   });
    auto f = alloc(512U,
                   {
                       {X, 64},    // a
                       {X, 64},    // b
                       {X, 64},    // c
                       {X, 64},    // d
                       {X, 2048},  // e
                       {X, 1024},  // f
                       {O, 768},
                   });
    dealloc(b,
            {
                {X, 64},  // a
                {O, 64},
                {X, 64},    // c
                {X, 64},    // d
                {X, 2048},  // e
                {X, 1024},  // f
                {O, 768},
            });
    dealloc(a,
            {
                {O, 128},   // joined right
                {X, 64},    // c
                {X, 64},    // d
                {X, 2048},  // e
                {X, 1024},  // f
                {O, 768},
            });
    dealloc(c,
            {
                {O, 192},   // joined left
                {X, 64},    // d
                {X, 2048},  // e
                {X, 1024},  // f
                {O, 768},
            });
    dealloc(e,
            {
                {O, 192},
                {X, 64},  // d
                {O, 2048},
                {X, 1024},  // f
                {O, 768},
            });
    auto g = alloc(400U,  // The last block will be taken because it is a better fit.
                   {
                       {O, 192},
                       {X, 64},  // d
                       {O, 2048},
                       {X, 1024},  // f
                       {X, 512},   // g
                       {O, 256},
                   });
    dealloc(f,
            {
                {O, 192},
                {X, 64},    // d
                {O, 3072},  // joined left
                {X, 512},   // g
                {O, 256},
            });
    dealloc(d,
            {
                {O, 3328},  // joined left & right
                {X, 512},   // g
                {O, 256},
            });
    auto h = alloc(200U,
                   {
                       {O, 3328},
                       {X, 512},  // g
                       {X, 256},  // h
                   });
    auto i = alloc(32U,
                   {
                       {X, 64},  // i
                       {O, 3264},
                       {X, 512},  // g
                       {X, 256},  // h
                   });
    dealloc(g,
            {
                {X, 64},  // i
                {O, 3776},
                {X, 256},  // h
            });
    dealloc(h,
            {
                {X, 64},  // i
                {O, 4032},
            });
    dealloc(i,
            {
                {O, 4096},  // All heap is free.
            });

    REQUIRE(heap->diagnostics.capacity == 4096U);
    REQUIRE(heap->diagnostics.allocated == 0U);
    REQUIRE(heap->diagnostics.peak_allocated == 3328U);
    REQUIRE(heap->diagnostics.peak_request_size == 1024U);
    REQUIRE(heap->diagnostics.oom_count == 0U);
    REQUIRE(heap->doInvariantsHold());
}

TEST_CASE("General: realloc")
{
    using internal::Fragment;

    // Use a 4096-byte heap (after instance overhead) for predictable fragment sizes.
    // Fragment sizes are powers of 2: 32 (min), 64, 128, 256, 512, 1024, 2048, 4096.
    alignas(128U) std::array<std::byte, 4096U + sizeof(internal::O1HeapInstance) + O1HEAP_ALIGNMENT - 1U> arena{};
    auto heap = init(arena.data(), std::size(arena));
    REQUIRE(heap != nullptr);
    REQUIRE(heap->diagnostics.capacity == 4096U);

    std::size_t allocated         = 0U;
    std::size_t peak_allocated    = 0U;
    std::size_t peak_request_size = 0U;
    uint64_t    oom_count         = 0U;

    // Helper to allocate and track diagnostics.
    const auto alloc = [&](const std::size_t amount, const std::vector<std::pair<bool, std::size_t>>& reference) {
        const auto p = heap->allocate(amount);
        if (amount > 0U)
        {
            REQUIRE(p != nullptr);
            std::generate_n(reinterpret_cast<std::byte*>(p), amount, getRandomByte);
            const auto& frag      = Fragment::constructFromAllocatedMemory(p);
            const auto  frag_size = frag.header.getSize(heap);
            allocated += frag_size;
            peak_allocated    = std::max(peak_allocated, allocated);
            peak_request_size = std::max(peak_request_size, amount);
        }
        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        heap->matchFragments(reference);
        REQUIRE(heap->doInvariantsHold());
        return p;
    };

    // Helper to free and track diagnostics.
    const auto dealloc = [&](void* const p, const std::vector<std::pair<bool, std::size_t>>& reference) {
        if (p != nullptr)
        {
            const auto& frag = Fragment::constructFromAllocatedMemory(p);
            allocated -= frag.header.getSize(heap);
        }
        heap->free(p);
        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        heap->matchFragments(reference);
        REQUIRE(heap->doInvariantsHold());
    };

    // Helper to realloc, fill with pattern, and verify heap state.
    // old_amount is used to verify data preservation (first min(old,new) bytes should match).
    const auto realloc_check = [&](void* const                                      old_ptr,
                                   const std::size_t                                old_amount,
                                   const std::size_t                                new_amount,
                                   const std::vector<std::pair<bool, std::size_t>>& reference,
                                   const bool                                       expect_success  = true,
                                   const bool                                       expect_same_ptr = false) {
        INFO(heap->visualize());

        // Fill old allocation with a known pattern before realloc.
        if (old_ptr != nullptr && old_amount > 0U)
        {
            auto* bytes = reinterpret_cast<std::uint8_t*>(old_ptr);
            for (std::size_t i = 0; i < old_amount; i++)
            {
                bytes[i] = static_cast<std::uint8_t>((i * 7U + 0xAB) & 0xFFU);
            }
        }

        // Track old fragment size for diagnostics update.
        std::size_t old_frag_size = 0U;
        if (old_ptr != nullptr)
        {
            const auto& frag = Fragment::constructFromAllocatedMemory(old_ptr);
            old_frag_size    = frag.header.getSize(heap);
        }

        // Update peak_request_size before calling realloc.
        if (new_amount > 0U)
        {
            peak_request_size = std::max(peak_request_size, new_amount);
        }

        const auto new_ptr = heap->reallocate(old_ptr, new_amount);

        if (expect_success && new_amount > 0U)
        {
            REQUIRE(new_ptr != nullptr);

            if (expect_same_ptr)
            {
                REQUIRE(new_ptr == old_ptr);
            }

            // Verify data preservation: first min(old_amount, new_amount) bytes should match pattern.
            const auto  preserve_size = std::min(old_amount, new_amount);
            const auto* bytes         = reinterpret_cast<const std::uint8_t*>(new_ptr);
            for (std::size_t i = 0; i < preserve_size; i++)
            {
                const auto expected = static_cast<std::uint8_t>((i * 7U + 0xAB) & 0xFFU);
                REQUIRE(bytes[i] == expected);
            }

            // Update tracked allocated size.
            const auto& new_frag      = Fragment::constructFromAllocatedMemory(new_ptr);
            const auto  new_frag_size = new_frag.header.getSize(heap);
            allocated                 = allocated - old_frag_size + new_frag_size;
            // During alloc-copy-free, peak can spike temporarily (old + new both allocated).
            // We sync from heap's diagnostics since we can't easily predict which path was taken.
            peak_allocated = heap->diagnostics.peak_allocated;
        }
        else if (new_amount == 0U)
        {
            // Acts as free.
            REQUIRE(new_ptr == nullptr);
            allocated -= old_frag_size;
        }
        else
        {
            // Expect failure (OOM). Original pointer remains valid.
            REQUIRE(new_ptr == nullptr);
            oom_count++;
        }

        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        REQUIRE(heap->diagnostics.oom_count == oom_count);
        heap->matchFragments(reference);
        REQUIRE(heap->doInvariantsHold());

        // On failure, return the original pointer (still valid). On success, return new_ptr.
        return (new_ptr != nullptr) ? new_ptr : old_ptr;
    };

    constexpr auto X = true;   // used
    constexpr auto O = false;  // free

    // ==================== EDGE CASES ====================

    // Edge case 1: NULL pointer acts as allocate.
    auto a = realloc_check(nullptr,
                           0U,
                           32U,
                           {
                               {X, 64},
                               {O, 4032},
                           });

    // Edge case 2: Zero size acts as free.
    (void) realloc_check(a,
                         32U,
                         0U,
                         {
                             {O, 4096},
                         });
    a = nullptr;

    // Edge case 3: Realloc that increases peak_allocated.
    // After edge case 2, peak_allocated=64, allocated=0.
    // Allocate small (32 bytes -> 64-byte fragment), then realloc to larger size that exceeds peak.
    a = realloc_check(nullptr,
                      0U,
                      32U,
                      {
                          {X, 64},
                          {O, 4032},
                      });
    // Now allocated=64, peak=64 (same as edge case 1).
    // Realloc to need 128 bytes (request 100 -> 128-byte fragment).
    // This should increase allocated to 128, exceeding peak of 64.
    a = realloc_check(a,
                      32U,
                      100U,
                      {
                          {X, 128},
                          {O, 3968},
                      },
                      true,
                      true);  // same pointer (expand forward into free tail)
    // Now peak_allocated should have been updated to 128.
    REQUIRE(heap->diagnostics.peak_allocated == 128U);
    // Clean up.
    dealloc(a, {{O, 4096}});
    a = nullptr;

    // ==================== SHRINK SCENARIOS ====================

    // Setup: allocate a large block to shrink.
    a = alloc(200U,
              {
                  {X, 256},
                  {O, 3840},
              });

    // Shrink scenario 1: Same size - no change.
    a = realloc_check(a,
                      200U,
                      200U,
                      {
                          {X, 256},  // a unchanged
                          {O, 3840},
                      },
                      true,
                      true);  // same pointer

    // Shrink scenario 2: Shrink with leftover >= MIN, next is free (merge with next).
    // a is 256 bytes, shrink to fit in 64 bytes -> leftover = 192, merges with 3840 = 4032.
    a = realloc_check(a,
                      200U,
                      32U,
                      {
                          {X, 64},    // a (shrunk)
                          {O, 4032},  // leftover merged with tail
                      },
                      true,
                      true);  // same pointer

    // Shrink scenario 3: Same size request - no change.
    a = realloc_check(a,
                      32U,
                      32U,
                      {
                          {X, 64},
                          {O, 4032},
                      },
                      true,
                      true);  // same pointer

    // Setup for shrink with no merge (next is used).
    // First, free a and allocate fresh blocks in the right configuration.
    dealloc(a, {{O, 4096}});
    a = nullptr;

    // Allocate: large block (to shrink), then small blocker.
    a      = alloc(100U,
                   {
                  {X, 128},  // a
                  {O, 3968},
              });
    auto b = alloc(32U,
                   {
                       {X, 128},  // a
                       {X, 64},   // b (blocker)
                       {O, 3904},
                   });

    // Shrink scenario 4: Shrink with leftover >= MIN, next is used (no merge).
    // a is 128 bytes, shrink to fit in 64 bytes -> leftover = 64, becomes new free block.
    a = realloc_check(a,
                      100U,
                      32U,
                      {
                          {X, 64},  // a (shrunk)
                          {O, 64},  // leftover from a
                          {X, 64},  // b
                          {O, 3904},
                      },
                      true,
                      true);  // same pointer

    // Clean up.
    // When a (64 bytes) is freed, it merges with adjacent free block (64 bytes) -> 128 bytes.
    dealloc(a, {{O, 128}, {X, 64}, {O, 3904}});
    a = nullptr;

    dealloc(b, {{O, 4096}});
    b = nullptr;

    // ==================== EXPAND FORWARD SCENARIOS ====================

    // Setup: create a used block followed by a free block.
    a      = alloc(32U,
                   {
                  {X, 64},
                  {O, 4032},
              });
    b      = alloc(32U,
                   {
                  {X, 64},
                  {X, 64},
                  {O, 3968},
              });
    auto c = alloc(32U,
                   {
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {O, 3904},
                   });

    // Free b to create: [a used][b free][c used][free tail]
    dealloc(b,
            {
                {X, 64},  // a
                {O, 64},  // b (freed)
                {X, 64},  // c
                {O, 3904},
            });
    b = nullptr;

    // Expand forward scenario 1: a expands into b's space, with leftover.
    // a is 64, request 50 bytes -> needs 64 bytes still, but let's request 40 to need 64.
    // Actually, to expand forward, we need to request more than current frag can hold.
    // a has 64 bytes frag, user can use 64-16=48 bytes. Request 60 -> needs 128 byte frag.
    // Next free (b) is 64 bytes. 64 + 64 = 128 >= 128. Expand forward, no leftover.
    a = realloc_check(a,
                      32U,
                      60U,
                      {
                          {X, 128},  // a expanded into b's space (64+64=128, no leftover)
                          {X, 64},   // c
                          {O, 3904},
                      },
                      true,
                      true);  // same pointer

    // Clean up and set up for split scenario.
    dealloc(a, {{O, 128}, {X, 64}, {O, 3904}});
    dealloc(c, {{O, 4096}});
    a = c = nullptr;

    // Setup for expand forward with split.
    a = alloc(32U,
              {
                  {X, 64},
                  {O, 4032},
              });
    b = alloc(200U,
              {
                  {X, 64},
                  {X, 256},
                  {O, 3776},
              });
    c = alloc(32U,
              {
                  {X, 64},   // a
                  {X, 256},  // b
                  {X, 64},   // c
                  {O, 3712},
              });

    // Free b: [a used 64][b free 256][c used 64][free 3712]
    dealloc(b,
            {
                {X, 64},   // a
                {O, 256},  // b freed
                {X, 64},   // c
                {O, 3712},
            });
    b = nullptr;

    // Expand forward scenario 2: a expands into b's space, with leftover (split).
    // a is 64, request 60 -> needs 128. Free next is 256. 64 + 256 = 320 >= 128.
    // Take 128, leftover = 320 - 128 = 192 >= 32, so split.
    a = realloc_check(a,
                      32U,
                      60U,
                      {
                          {X, 128},  // a expanded
                          {O, 192},  // leftover
                          {X, 64},   // c
                          {O, 3712},
                      },
                      true,
                      true);  // same pointer

    // Clean up for backward expand tests.
    dealloc(a, {{O, 320}, {X, 64}, {O, 3712}});
    dealloc(c, {{O, 4096}});
    a = c = nullptr;

    // ==================== EXPAND BACKWARD SCENARIOS ====================

    // Setup: create [free][used][used] pattern.
    a = alloc(32U,
              {
                  {X, 64},
                  {O, 4032},
              });
    b = alloc(32U,
              {
                  {X, 64},
                  {X, 64},
                  {O, 3968},
              });
    c = alloc(32U,
              {
                  {X, 64},
                  {X, 64},
                  {X, 64},
                  {O, 3904},
              });

    // Free a to create: [a free 64][b used 64][c used 64][free 3904]
    dealloc(a,
            {
                {O, 64},  // a freed
                {X, 64},  // b
                {X, 64},  // c
                {O, 3904},
            });
    a = nullptr;

    // Expand backward scenario 1: b expands into a's space, no leftover.
    // b is 64, request 60 -> needs 128. Prev free is 64. 64 + 64 = 128 >= 128, no leftover.
    // Data must be moved backward.
    b = realloc_check(b,
                      32U,
                      60U,
                      {
                          {X, 128},  // b expanded backward (now starts at old a's position)
                          {X, 64},   // c
                          {O, 3904},
                      },
                      true,
                      false);  // different pointer (moved backward)

    // Clean up and set up for split scenario.
    dealloc(b, {{O, 128}, {X, 64}, {O, 3904}});
    dealloc(c, {{O, 4096}});
    b = c = nullptr;

    // Setup for expand backward with split (larger prev free block).
    a = alloc(200U,
              {
                  {X, 256},
                  {O, 3840},
              });
    b = alloc(32U,
              {
                  {X, 256},
                  {X, 64},
                  {O, 3776},
              });
    c = alloc(32U,
              {
                  {X, 256},  // a
                  {X, 64},   // b
                  {X, 64},   // c
                  {O, 3712},
              });

    // Free a: [a free 256][b used 64][c used 64][free 3712]
    dealloc(a,
            {
                {O, 256},  // a freed
                {X, 64},   // b
                {X, 64},   // c
                {O, 3712},
            });
    a = nullptr;

    // Expand backward scenario 2: b expands into a's space, with leftover (split).
    // b is 64, request 60 -> needs 128. Prev free is 256. 64 + 256 = 320 >= 128.
    // Take 128, leftover = 320 - 128 = 192 >= 32, so split. Leftover at end.
    b = realloc_check(b,
                      32U,
                      60U,
                      {
                          {X, 128},  // b expanded backward
                          {O, 192},  // leftover at end of combined region
                          {X, 64},   // c
                          {O, 3712},
                      },
                      true,
                      false);  // different pointer

    // Clean up.
    dealloc(b, {{O, 320}, {X, 64}, {O, 3712}});
    dealloc(c, {{O, 4096}});
    b = c = nullptr;

    // ==================== STANDARD ALLOC-COPY-FREE ====================

    // Setup: fragment the heap so neighbors can't help but there's space elsewhere.
    a      = alloc(32U,
                   {
                  {X, 64},
                  {O, 4032},
              });
    b      = alloc(32U,
                   {
                  {X, 64},
                  {X, 64},
                  {O, 3968},
              });
    c      = alloc(32U,
                   {
                  {X, 64},
                  {X, 64},
                  {X, 64},
                  {O, 3904},
              });
    auto d = alloc(32U,
                   {
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {X, 64},
                       {O, 3840},
                   });

    // All neighbors of b are used. Request larger size -> must alloc elsewhere.
    // b is 64, request 200 -> needs 256. No free neighbors. Must allocate from tail (3840).
    // After: old b freed, new b at tail.
    b = realloc_check(b,
                      32U,
                      200U,
                      {
                          {X, 64},   // a
                          {O, 64},   // old b freed
                          {X, 64},   // c
                          {X, 64},   // d
                          {X, 256},  // new b
                          {O, 3584},
                      },
                      true,
                      false);  // different pointer

    // Clean up.
    // When a (64) is freed, it merges with adjacent free (old b, 64) -> free 128.
    dealloc(a, {{O, 128}, {X, 64}, {X, 64}, {X, 256}, {O, 3584}});
    a = nullptr;

    dealloc(c, {{O, 192}, {X, 64}, {X, 256}, {O, 3584}});
    dealloc(d, {{O, 256}, {X, 256}, {O, 3584}});
    dealloc(b, {{O, 4096}});
    b = c = d = nullptr;

    // ==================== MERGE-AWARE FALLBACK (THE EDGE CASE) ====================

    // This is the tricky case: standard alloc fails, but merging prev + current + next works.
    // Setup: [free Z][used X][free tail] where Z < needed, X < needed, but Z + X >= needed.
    // And the tail must be too small or non-existent to satisfy a direct alloc.

    // Create a nearly-full heap with specific layout.
    a = alloc(1800U,
              {
                  {X, 2048},
                  {O, 2048},
              });
    b = alloc(32U,
              {
                  {X, 2048},
                  {X, 64},
                  {O, 1984},
              });
    c = alloc(900U,
              {
                  {X, 2048},
                  {X, 64},
                  {X, 1024},
                  {O, 960},
              });
    d = alloc(400U,
              {
                  {X, 2048},  // a
                  {X, 64},    // b
                  {X, 1024},  // c
                  {X, 512},   // d
                  {O, 448},
              });

    // Free a and c to create: [free 2048][b used 64][free 1024][d used 512][free 448]
    dealloc(a,
            {
                {O, 2048},
                {X, 64},
                {X, 1024},
                {X, 512},
                {O, 448},
            });
    dealloc(c,
            {
                {O, 2048},
                {X, 64},
                {O, 1024},
                {X, 512},
                {O, 448},
            });
    a = c = nullptr;

    // Now b (64 bytes) is between two free blocks (2048 before, 1024 after).
    // Request a size that:
    // - Is larger than 2048 (so direct alloc of that block fails)
    // - Is larger than 1024 (so direct alloc of that block fails)
    // - Is <= 2048 + 64 + 1024 = 3136
    // Request 2050 bytes -> needs 4096 byte fragment. 2048 + 64 + 1024 = 3136 < 4096. Won't work.
    // Let's try: request 1500 -> needs 2048. prev=2048 alone can satisfy. That's not the edge case.
    //
    // Better setup: make prev and next both smaller than needed individually.
    // Reset and try again.
    // Free d: merges with adjacent free blocks (1024 + 512 + 448 = 1984).
    dealloc(d, {{O, 2048}, {X, 64}, {O, 1984}});
    d = nullptr;
    // Free b: merges with all adjacent free blocks -> entire heap free.
    dealloc(b, {{O, 4096}});
    b = nullptr;

    // Setup for merge-aware fallback.
    // Goal: [free 512][b used 64][free 512][d used 2048][free 960]
    // Then request for b that needs 1024. prev=512 < 1024, next=512 < 1024, tail=960 < 1024.
    // Standard alloc fails. But 512 + 64 + 512 = 1088 >= 1024, so merge-aware fallback works.
    a = alloc(400U,
              {
                  {X, 512},
                  {O, 3584},
              });
    b = alloc(32U,
              {
                  {X, 512},
                  {X, 64},
                  {O, 3520},
              });
    c = alloc(400U,
              {
                  {X, 512},
                  {X, 64},
                  {X, 512},
                  {O, 3008},
              });
    // 2000 + 16 = 2016, needs 2048 fragment. 3008 >= 2048.
    d = alloc(2000U,
              {
                  {X, 512},   // a
                  {X, 64},    // b
                  {X, 512},   // c
                  {X, 2048},  // d
                  {O, 960},
              });
    // Now: [a 512][b 64][c 512][d 2048][free 960]

    // Free a and c:
    dealloc(a,
            {
                {O, 512},
                {X, 64},
                {X, 512},
                {X, 2048},
                {O, 960},
            });
    dealloc(c,
            {
                {O, 512},
                {X, 64},
                {O, 512},
                {X, 2048},
                {O, 960},
            });
    a = c = nullptr;

    // Current state: [free 512][b used 64][free 512][d used 2048][free 960]
    // b wants to grow to need 1024 byte fragment.
    // prev (512) < 1024. next (512) < 1024. tail (960) < 1024.
    // Standard alloc will fail (no single block >= 1024).
    // But: 512 + 64 + 512 = 1088 >= 1024. Merge-aware fallback should work!
    // Note: request 510 bytes needs 1024 on both x64 (510+16=526) and x32 (510+8=518).

    b = realloc_check(b,
                      32U,
                      510U,  // needs 1024 byte fragment (510+alignment > 512)
                      {
                          {X, 1024},  // b expanded via merge (512 + 64 + 512 = 1088, take 1024)
                          {O, 64},    // leftover (1088 - 1024 = 64)
                          {X, 2048},  // d
                          {O, 960},
                      },
                      true,
                      false);  // different pointer (moved)

    // Clean up.
    dealloc(b, {{O, 1088}, {X, 2048}, {O, 960}});
    dealloc(d, {{O, 4096}});
    b = d = nullptr;

    // ==================== MERGE BACKWARD NO LEFTOVER ====================

    // This tests the edge case in EXPAND BACKWARD where:
    // - prev is free, next is free
    // - prev + frag + next equals EXACTLY the needed size (leftover = 0)
    // This exercises line 649 branch 0 in o1heap.c (next_free=true in the no-leftover case).
    // Setup: [prev free 256][frag used 128][next free 128][blocker]
    // Realloc frag to need 512 bytes.
    // Forward check: 128 + 128 = 256 < 512 -> fail
    // Backward check: 256 + 128 + 128 = 512 >= 512 -> success, leftover = 0
    a = alloc(240U,
              {
                  {X, 256},
                  {O, 3840},
              });
    b = alloc(112U,
              {
                  {X, 256},
                  {X, 128},
                  {O, 3712},
              });
    c = alloc(112U,
              {
                  {X, 256},  // a (prev)
                  {X, 128},  // b (frag)
                  {X, 128},  // c (next)
                  {O, 3584},
              });
    // Allocate blocker (d) to prevent b from using the tail for direct alloc-copy-free.
    // The tail is 3584, requesting 2000 needs 2048 fragment. 3584 >= 2048.
    d = alloc(2000U,
              {
                  {X, 256},   // a
                  {X, 128},   // b
                  {X, 128},   // c
                  {X, 2048},  // d (blocker)
                  {O, 1536},
              });
    // Allocated: 256 + 128 + 128 + 2048 = 2560
    REQUIRE(allocated == 2560U);
    REQUIRE(heap->diagnostics.allocated == 2560U);

    // Free a (prev) and c (next), keeping b (frag) and d (blocker).
    // Result: [free 256][b used 128][free 128][d used 2048][free 1536]
    dealloc(a,
            {
                {O, 256},
                {X, 128},  // b
                {X, 128},  // c
                {X, 2048},
                {O, 1536},
            });
    dealloc(c,
            {
                {O, 256},   // freed a
                {X, 128},   // b
                {O, 128},   // freed c
                {X, 2048},  // d
                {O, 1536},
            });
    a = c = nullptr;
    // Allocated after frees: 2560 - 256 - 128 = 2176 (b=128 + d=2048)
    REQUIRE(allocated == 2176U);
    REQUIRE(heap->diagnostics.allocated == 2176U);

    // Now: [free 256][b used 128][free 128][d used 2048][free 1536]
    // Request size 480 -> needs 512 byte fragment on both x64 and x32.
    // (x64: 480+16=496 -> 512; x32: 480+8=488 -> 512)
    // Forward: 128 + 128 = 256 < 512 -> cannot expand forward only
    // Backward: 256 + 128 + 128 = 512 >= 512 -> success!
    // Leftover = 512 - 512 = 0 < FRAGMENT_SIZE_MIN -> no split, absorb all three
    // Allocated update: += prev_size + next_size = 256 + 128 = 384 -> 2176 + 384 = 2560
    b = realloc_check(b,
                      112U,
                      480U,  // needs 512 byte fragment (256 + 128 + 128 = 512 exactly)
                      {
                          {X, 512},   // b expanded via merge (absorbed all of prev and next)
                          {X, 2048},  // d
                          {O, 1536},
                      },
                      true,
                      false);  // different pointer (moved backward)
    // Allocated after realloc: 2560 (b=512 + d=2048)
    REQUIRE(allocated == 2560U);
    REQUIRE(heap->diagnostics.allocated == 2560U);

    // Clean up.
    dealloc(b, {{O, 512}, {X, 2048}, {O, 1536}});
    dealloc(d, {{O, 4096}});
    b = d = nullptr;

    // ==================== REALLOC LAST FRAGMENT (next == NULL) ====================

    // This tests reallocating the last fragment in the heap where next is NULL.
    // This exercises the branch at line 564 where next == NULL.
    // Setup: [blocker used 2048][frag used 2048] (frag is at the end, no next fragment)
    a = alloc(2000U,
              {
                  {X, 2048},  // a (blocker)
                  {O, 2048},
              });
    b = alloc(2000U,
              {
                  {X, 2048},  // a
                  {X, 2048},  // b (last fragment, uses all remaining space)
              });
    // b is now the last fragment, next == NULL
    // Allocated: 2048 + 2048 = 4096
    REQUIRE(allocated == 4096U);
    REQUIRE(heap->diagnostics.allocated == 4096U);

    // Shrink b: since next == NULL, next_free = false, next_size = 0
    // Shrink with leftover >= MIN and next_free = false
    // Request 496 bytes: x64: 496+16=512, x32: 496+8=504 -> both round to 512
    // Allocated update: -= leftover = -(2048 - 512) = -1536 -> 4096 - 1536 = 2560
    b = realloc_check(b,
                      2000U,
                      496U,  // shrink from 2048 to 512
                      {
                          {X, 2048},  // a
                          {X, 512},   // b shrunk
                          {O, 1536},  // leftover becomes new free fragment (no merge, next was NULL)
                      },
                      true,
                      true);  // same pointer (shrink in place)
    // Allocated after shrink: 2560 (a=2048 + b=512)
    REQUIRE(allocated == 2560U);
    REQUIRE(heap->diagnostics.allocated == 2560U);

    // Grow b: prev is used (a), next is free (the leftover), should expand forward
    // Request 1000 bytes: x64: 1000+16=1016, x32: 1000+8=1008 -> both round to 1024
    // Allocated update: += new_frag_size - frag_size = 1024 - 512 = 512 -> 2560 + 512 = 3072
    b = realloc_check(b,
                      496U,
                      1000U,  // grow from 512 to 1024
                      {
                          {X, 2048},  // a
                          {X, 1024},  // b expanded
                          {O, 1024},  // leftover after expansion
                      },
                      true,
                      true);  // same pointer (expand forward)
    // Allocated after expand: 3072 (a=2048 + b=1024)
    REQUIRE(allocated == 3072U);
    REQUIRE(heap->diagnostics.allocated == 3072U);

    // Clean up.
    dealloc(b, {{X, 2048}, {O, 2048}});
    dealloc(a, {{O, 4096}});
    a = b = nullptr;

    // ==================== BACKWARD EXPANSION INSUFFICIENT ====================

    // This tests the case where prev is free but prev + frag + next is still not enough.
    // This exercises line 626 branch 3 (condition false when prev_free is true).
    // Setup: [free 64][frag 64][blocker 2048][free 1920]
    // Try to realloc frag to need 256 bytes.
    // Forward: frag(64) + next(0, blocker is used) = 64 < 256 -> fail
    // Backward: prev(64) + frag(64) + next(0) = 128 < 256 -> fail (target branch!)
    // Falls through to alloc-copy-free: allocates 256 from the 1920-byte tail.
    a = alloc(32U,
              {
                  {X, 64},
                  {O, 4032},
              });
    b = alloc(32U,
              {
                  {X, 64},
                  {X, 64},
                  {O, 3968},
              });
    c = alloc(1900U,
              {
                  {X, 64},    // a
                  {X, 64},    // b
                  {X, 2048},  // c (blocker)
                  {O, 1920},
              });
    // Allocated: 64 + 64 + 2048 = 2176
    REQUIRE(allocated == 2176U);
    REQUIRE(heap->diagnostics.allocated == 2176U);

    // Free a to create: [free 64][b used 64][c used 2048][free 1920]
    dealloc(a,
            {
                {O, 64},
                {X, 64},
                {X, 2048},
                {O, 1920},
            });
    a = nullptr;
    // Allocated after free: 2176 - 64 = 2112 (b=64 + c=2048)
    REQUIRE(allocated == 2112U);
    REQUIRE(heap->diagnostics.allocated == 2112U);

    // Realloc b to need 256 bytes. This will use alloc-copy-free fallback.
    // Alloc-copy-free: allocate new 256-byte block, copy data, free old 64-byte block.
    // Allocated update: +256 (new alloc) -64 (free old) = +192 -> 2112 + 192 = 2304
    // After realloc: [free 128][c used 2048][b used 256][free 1664]
    // (old b merges with prev when freed)
    b = realloc_check(b,
                      32U,
                      200U,  // needs 256 byte fragment
                      {
                          {O, 128},   // merged: freed prev (64) + freed old b (64)
                          {X, 2048},  // c
                          {X, 256},   // b (new location)
                          {O, 1664},  // remaining tail
                      },
                      true,
                      false);  // different pointer (alloc-copy-free)
    // Allocated after realloc: 2304 (c=2048 + b=256)
    REQUIRE(allocated == 2304U);
    REQUIRE(heap->diagnostics.allocated == 2304U);

    // Clean up.
    dealloc(b, {{O, 128}, {X, 2048}, {O, 1920}});
    dealloc(c, {{O, 4096}});
    b = c = nullptr;

    // ==================== TRUE OOM ====================

    // Setup: heap is fragmented such that even merging can't help.
    a = alloc(32U,
              {
                  {X, 64},
                  {O, 4032},
              });
    b = alloc(1800U,
              {
                  {X, 64},
                  {X, 2048},
                  {O, 1984},
              });
    c = alloc(32U,
              {
                  {X, 64},
                  {X, 2048},
                  {X, 64},
                  {O, 1920},
              });

    // Try to grow 'a' to need 4096 bytes (larger than entire heap capacity - overhead).
    // prev = none, current = 64, next = none (b is used). Can't merge with anyone.
    // And 64 < 4096 needed. OOM.
    a = realloc_check(a,
                      32U,
                      4000U,  // needs 4096 fragment, but a is only 64 and no free neighbors
                      {
                          {X, 64},    // a unchanged
                          {X, 2048},  // b
                          {X, 64},    // c
                          {O, 1920},
                      },
                      false);  // expect failure

    // Edge case: request larger than capacity.
    a = realloc_check(a,
                      32U,
                      10000U,  // way larger than capacity
                      {
                          {X, 64},
                          {X, 2048},
                          {X, 64},
                          {O, 1920},
                      },
                      false);  // expect failure

    // Final cleanup.
    dealloc(a, {{O, 64}, {X, 2048}, {X, 64}, {O, 1920}});
    dealloc(b, {{O, 2112}, {X, 64}, {O, 1920}});
    dealloc(c, {{O, 4096}});

    REQUIRE(heap->diagnostics.capacity == 4096U);
    REQUIRE(heap->diagnostics.allocated == 0U);
    REQUIRE(heap->doInvariantsHold());
}

/// This test has been empirically tuned to expand its state space coverage.
/// If any new behaviors need to be tested, please consider writing another test instead of changing this one.
TEST_CASE("General: random A")
{
    using internal::Fragment;

    constexpr auto                   ArenaSize = MiB * 100U;
    const std::shared_ptr<std::byte> arena(static_cast<std::byte*>(std::aligned_alloc(64U, ArenaSize)), &std::free);
    std::generate_n(arena.get(), ArenaSize, getRandomByte);  // Random-fill the ENTIRE arena!
    auto heap = init(arena.get(), ArenaSize);
    REQUIRE(heap != nullptr);

    std::vector<void*> pointers;

    std::size_t   allocated         = 0U;
    std::size_t   peak_allocated    = 0U;
    std::size_t   peak_request_size = 0U;
    std::uint64_t oom_count         = 0U;

    std::random_device random_device;
    std::mt19937       random_generator(random_device());

    const auto allocate = [&]() {
        REQUIRE(heap->doInvariantsHold());
        std::uniform_int_distribution<std::size_t> dis(0, ArenaSize / 300U);

        const std::size_t amount = dis(random_generator);
        const auto        ptr    = heap->allocate(amount);
        if (ptr != nullptr)
        {
            // Overwrite all to ensure that the allocator does not make implicit assumptions about the memory use.
            std::generate_n(reinterpret_cast<std::byte*>(ptr), amount, getRandomByte);
            pointers.push_back(ptr);
            const auto& frag = Fragment::constructFromAllocatedMemory(ptr);
            allocated += frag.header.getSize(heap);
            peak_allocated = std::max(peak_allocated, allocated);
        }
        else
        {
            if (amount > 0U)
            {
                oom_count++;
            }
        }
        peak_request_size = std::max(peak_request_size, amount);
        REQUIRE(heap->doInvariantsHold());
    };

    const auto deallocate = [&]() {
        REQUIRE(heap->doInvariantsHold());
        if (!pointers.empty())
        {
            std::uniform_int_distribution<decltype(pointers)::difference_type>
                        dis(0, static_cast<decltype(pointers)::difference_type>(std::size(pointers) - 1));
            const auto  it  = std::begin(pointers) + dis(random_generator);
            void* const ptr = *it;
            (void) pointers.erase(it);
            if (ptr != nullptr)
            {
                const auto& frag = Fragment::constructFromAllocatedMemory(ptr);
                frag.validate(heap);
                REQUIRE(allocated >= frag.header.getSize(heap));
                allocated -= frag.header.getSize(heap);
            }
            heap->free(ptr);
        }
        REQUIRE(heap->doInvariantsHold());
    };

    // The memory use is growing slowly from zero.
    // We stop the test when it's been running near the max heap utilization for long enough.
    while (heap->diagnostics.oom_count < 500U)
    {
        for (auto i = 0U; i < 100U; i++)
        {
            allocate();
        }
        for (auto i = 0U; i < 40U; i++)
        {
            deallocate();
        }
        REQUIRE(heap->diagnostics.allocated == allocated);
        REQUIRE(heap->diagnostics.peak_allocated == peak_allocated);
        REQUIRE(heap->diagnostics.peak_request_size == peak_request_size);
        REQUIRE(heap->diagnostics.oom_count == oom_count);
        REQUIRE(heap->doInvariantsHold());

        std::cout << heap->visualize() << std::endl;
    }
}

TEST_CASE("General: min arena size")
{
    alignas(128U) std::array<std::byte, 1024> arena{};
    REQUIRE(nullptr == init(arena.data(), o1heapMinArenaSize - 1U));
    REQUIRE(nullptr != init(arena.data(), o1heapMinArenaSize));
}

TEST_CASE("General: max allocation size")
{
    alignas(128U) std::array<std::byte, 4096U + sizeof(internal::O1HeapInstance) + O1HEAP_ALIGNMENT - 1U> arena{};
    {
        auto heap = init(arena.data(), std::size(arena));
        REQUIRE(heap != nullptr);
        REQUIRE(heap->diagnostics.capacity == 4096);
        REQUIRE(4096 - O1HEAP_ALIGNMENT == heap->getMaxAllocationSize());
        REQUIRE(nullptr == heap->allocate(heap->getMaxAllocationSize() + 1));
        REQUIRE(nullptr != heap->allocate(heap->getMaxAllocationSize() + 0));
    }
    {
        auto heap = init(arena.data(), std::size(arena) - O1HEAP_ALIGNMENT);
        REQUIRE(heap != nullptr);
        REQUIRE(heap->diagnostics.capacity < 4095);
        REQUIRE(2048 - O1HEAP_ALIGNMENT == heap->getMaxAllocationSize());
        REQUIRE(nullptr == heap->allocate(heap->getMaxAllocationSize() + 1));
        REQUIRE(nullptr != heap->allocate(heap->getMaxAllocationSize() + 0));
    }
}

TEST_CASE("General: invariant checker")
{
    using internal::Fragment;

    alignas(128U) std::array<std::byte, 4096U + sizeof(internal::O1HeapInstance) + O1HEAP_ALIGNMENT - 1U> arena{};
    auto heap = init(arena.data(), std::size(arena));
    REQUIRE(heap != nullptr);
    REQUIRE(heap->doInvariantsHold());
    auto& dg = heap->diagnostics;

    dg.capacity++;
    REQUIRE(!heap->doInvariantsHold());
    dg.capacity--;
    REQUIRE(heap->doInvariantsHold());

    dg.allocated += Fragment::SizeMin;
    REQUIRE(!heap->doInvariantsHold());
    dg.peak_allocated += Fragment::SizeMin;
    REQUIRE(!heap->doInvariantsHold());
    dg.peak_request_size += 1;
    REQUIRE(heap->doInvariantsHold());
    dg.peak_allocated--;
    REQUIRE(!heap->doInvariantsHold());
    dg.peak_allocated++;
    dg.allocated -= Fragment::SizeMin;
    REQUIRE(heap->doInvariantsHold());
    dg.allocated++;
    REQUIRE(!heap->doInvariantsHold());
    dg.allocated--;
    REQUIRE(heap->doInvariantsHold());

    dg.peak_allocated = dg.capacity + 1U;
    REQUIRE(!heap->doInvariantsHold());
    dg.peak_allocated = dg.capacity;
    REQUIRE(heap->doInvariantsHold());

    dg.peak_request_size = dg.capacity;
    REQUIRE(!heap->doInvariantsHold());
    dg.oom_count++;
    REQUIRE(heap->doInvariantsHold());
}
