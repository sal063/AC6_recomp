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

/// This test file aims to exhaustively cover the state space of the allocator to empirically prove correctness.
/// It uses a combination of systematic enumeration (for small heaps) and randomized testing (for larger heaps).

#include "catch.hpp"
#include "o1heap.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <unordered_set>
#include <vector>

namespace
{

// =====================================================================================================================
// HELPERS
// =====================================================================================================================

constexpr std::size_t KiB = 1024U;

/// Tracks a single allocation with content verification support.
struct AllocationTracker
{
    void*         ptr            = nullptr;
    std::size_t   requested_size = 0;
    std::uint64_t pattern        = 0;  ///< Used to detect memory corruption.

    /// Fill the allocated memory with a deterministic pattern based on the pointer and a seed.
    void fillPattern(const std::uint64_t seed)
    {
        pattern = seed ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr));
        if (ptr != nullptr && requested_size > 0)
        {
            auto* const bytes = static_cast<std::uint8_t*>(ptr);
            for (std::size_t i = 0; i < requested_size; i++)
            {
                // Simple deterministic pattern: mix of pattern, index, and some primes.
                const auto shift = static_cast<std::uint64_t>((i % 8U) * 8U);
                const auto mult  = static_cast<std::uint64_t>(i) * 251ULL;
                bytes[i]         = static_cast<std::uint8_t>((pattern >> shift) ^ mult ^ 0xA5ULL);
            }
        }
    }

    /// Verify the pattern is still intact. Returns true if valid.
    [[nodiscard]] auto verifyPattern() const -> bool
    {
        if (ptr == nullptr || requested_size == 0)
        {
            return true;
        }
        const auto* const bytes = static_cast<const std::uint8_t*>(ptr);
        for (std::size_t i = 0; i < requested_size; i++)
        {
            const auto shift    = static_cast<std::uint64_t>((i % 8U) * 8U);
            const auto mult     = static_cast<std::uint64_t>(i) * 251ULL;
            const auto expected = static_cast<std::uint8_t>((pattern >> shift) ^ mult ^ 0xA5ULL);
            if (bytes[i] != expected)
            {
                return false;
            }
        }
        return true;
    }
};

/// Compute the expected fragment size for a given allocation request.
[[nodiscard]] auto computeFragmentSize(const std::size_t requested) -> std::size_t
{
    if (requested == 0)
    {
        return 0;
    }
    const std::size_t min_frag = O1HEAP_ALIGNMENT * 2U;
    const std::size_t size     = requested + O1HEAP_ALIGNMENT;
    // Round up to power of 2.
    std::size_t result = min_frag;
    while (result < size)
    {
        result *= 2U;
    }
    return result;
}

/// Initialize a heap in the given arena, returns nullptr on failure.
[[nodiscard]] auto initHeap(void* const arena, const std::size_t size) -> O1HeapInstance*
{
    // Fill with garbage first to ensure the allocator doesn't rely on zeroed memory.
    std::memset(arena, 0xCD, size);
    return o1heapInit(arena, size);
}

/// Verify all tracked allocations have intact patterns.
void verifyAllPatterns(const std::vector<AllocationTracker>& allocations)
{
    for (const auto& alloc : allocations)
    {
        REQUIRE(alloc.verifyPattern());
    }
}

/// Random number generator wrapper for consistent seeding.
class Rng
{
public:
    explicit Rng(const std::uint64_t seed) : gen_(seed) {}

    [[nodiscard]] auto next(const std::size_t min_val, const std::size_t max_val) -> std::size_t
    {
        std::uniform_int_distribution<std::size_t> dist(min_val, max_val);
        return dist(gen_);
    }

    [[nodiscard]] auto nextBool(const double probability = 0.5) -> bool
    {
        std::bernoulli_distribution dist(probability);
        return dist(gen_);
    }

    [[nodiscard]] auto nextU64() -> std::uint64_t { return gen_(); }

    template <typename T>
    void shuffle(std::vector<T>& vec)
    {
        std::shuffle(vec.begin(), vec.end(), gen_);
    }

private:
    std::mt19937_64 gen_;
};

// =====================================================================================================================
// TEST CASES
// =====================================================================================================================

TEST_CASE("Exhaustive: edge cases")
{
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 2 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto diag = o1heapGetDiagnostics(heap);
    REQUIRE(diag.capacity > 0);
    REQUIRE(diag.allocated == 0);

    SECTION("Zero-size allocation returns NULL without OOM")
    {
        const auto before = o1heapGetDiagnostics(heap);
        void*      ptr    = o1heapAllocate(heap, 0);
        REQUIRE(ptr == nullptr);
        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.oom_count == before.oom_count);  // Not an OOM!
        REQUIRE(after.allocated == 0);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Free of NULL is a no-op")
    {
        const auto before = o1heapGetDiagnostics(heap);
        o1heapFree(heap, nullptr);
        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == before.allocated);
        REQUIRE(after.oom_count == before.oom_count);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Allocation of maximum possible size")
    {
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        REQUIRE(max_alloc > 0);
        // max_alloc is pow2(log2Floor(capacity)) - O1HEAP_ALIGNMENT, which may be less than capacity - overhead.
        REQUIRE(max_alloc <= diag.capacity - O1HEAP_ALIGNMENT);
        REQUIRE(max_alloc >= diag.capacity / 2);  // At least half capacity is allocatable.

        void* ptr = o1heapAllocate(heap, max_alloc);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

        const auto after_alloc = o1heapGetDiagnostics(heap);
        // Allocated size is the fragment size (power of 2), which may be less than capacity.
        REQUIRE(after_alloc.allocated > 0);
        REQUIRE(after_alloc.allocated <= diag.capacity);
        REQUIRE(o1heapDoInvariantsHold(heap));

        o1heapFree(heap, ptr);
        const auto after_free = o1heapGetDiagnostics(heap);
        REQUIRE(after_free.allocated == 0);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Allocation just over maximum fails")
    {
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        const auto before    = o1heapGetDiagnostics(heap);

        void* ptr = o1heapAllocate(heap, max_alloc + 1);
        REQUIRE(ptr == nullptr);

        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.oom_count == before.oom_count + 1);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Allocation of capacity (without overhead space) fails")
    {
        const auto before = o1heapGetDiagnostics(heap);
        void*      ptr    = o1heapAllocate(heap, diag.capacity);
        REQUIRE(ptr == nullptr);
        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.oom_count == before.oom_count + 1);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Single byte allocation")
    {
        void* ptr = o1heapAllocate(heap, 1);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

        // Should allocate minimum fragment size.
        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == O1HEAP_ALIGNMENT * 2U);
        REQUIRE(o1heapDoInvariantsHold(heap));

        o1heapFree(heap, ptr);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Repeated allocation and free of same size")
    {
        for (std::size_t i = 0; i < 1000; i++)
        {
            void* ptr = o1heapAllocate(heap, 64);
            REQUIRE(ptr != nullptr);
            std::memset(ptr, static_cast<int>(i & 0xFFU), 64);
            o1heapFree(heap, ptr);
            REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
            REQUIRE(o1heapDoInvariantsHold(heap));
        }
    }
}

TEST_CASE("Exhaustive: realloc edge cases")
{
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 4 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto min_frag = O1HEAP_ALIGNMENT * 2U;
    (void) o1heapGetDiagnostics(heap);  // Verify heap is valid.

    SECTION("NULL pointer acts as allocate")
    {
        const auto before = o1heapGetDiagnostics(heap);
        void*      ptr    = o1heapReallocate(heap, nullptr, 64);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == computeFragmentSize(64));
        REQUIRE(after.oom_count == before.oom_count);
        REQUIRE(o1heapDoInvariantsHold(heap));

        o1heapFree(heap, ptr);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Zero size acts as free")
    {
        void* ptr = o1heapAllocate(heap, 64);
        REQUIRE(ptr != nullptr);
        std::memset(ptr, 0xAB, 64);

        const auto before = o1heapGetDiagnostics(heap);
        void*      result = o1heapReallocate(heap, ptr, 0);
        REQUIRE(result == nullptr);

        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == 0);
        REQUIRE(after.oom_count == before.oom_count);  // Not an OOM!
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    SECTION("Same size returns same pointer")
    {
        void* ptr = o1heapAllocate(heap, 64);
        REQUIRE(ptr != nullptr);
        std::memset(ptr, 0xAB, 64);

        const auto before = o1heapGetDiagnostics(heap);
        void*      result = o1heapReallocate(heap, ptr, 64);
        REQUIRE(result == ptr);  // Same pointer!

        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == before.allocated);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify content preserved.
        const auto* bytes = static_cast<const std::uint8_t*>(result);
        for (std::size_t i = 0; i < 64; i++)
        {
            REQUIRE(bytes[i] == 0xAB);
        }

        o1heapFree(heap, result);
    }

    SECTION("Shrink in place")
    {
        void* ptr = o1heapAllocate(heap, 200);  // Gets 256-byte fragment.
        REQUIRE(ptr != nullptr);
        const auto frag_size = computeFragmentSize(200);
        REQUIRE(frag_size == 256);

        // Fill with pattern.
        auto* bytes = static_cast<std::uint8_t*>(ptr);
        for (std::size_t i = 0; i < 200; i++)
        {
            bytes[i] = static_cast<std::uint8_t>(i & 0xFFU);
        }

        // Shrink to need a smaller fragment.
        // Use 32 bytes to get 64-byte fragment on both x64 (32+16=48->64) and x32 (32+8=40->64).
        void* result = o1heapReallocate(heap, ptr, 32);
        REQUIRE(result == ptr);  // Same pointer for shrink!

        const auto after = o1heapGetDiagnostics(heap);
        REQUIRE(after.allocated == computeFragmentSize(32));  // Shrunk from 256 to 64.
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify first 32 bytes preserved.
        bytes = static_cast<std::uint8_t*>(result);
        for (std::size_t i = 0; i < 32; i++)
        {
            REQUIRE(bytes[i] == static_cast<std::uint8_t>(i & 0xFFU));
        }

        o1heapFree(heap, result);
    }

    SECTION("Expand forward into free neighbor")
    {
        // Allocate two blocks, free the second, then expand the first.
        void* a = o1heapAllocate(heap, 1);  // 32-byte fragment.
        void* b = o1heapAllocate(heap, 1);  // 32-byte fragment.
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);

        const auto a_addr = reinterpret_cast<std::uintptr_t>(a);
        const auto b_addr = reinterpret_cast<std::uintptr_t>(b);
        REQUIRE(b_addr == a_addr + min_frag);

        // Fill a with pattern.
        auto* bytes = static_cast<std::uint8_t*>(a);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            bytes[i] = static_cast<std::uint8_t>(0xAA ^ i);
        }

        // Free b to create free space after a.
        o1heapFree(heap, b);

        // Expand a to need 64-byte fragment (a=32, b=32 free -> merged to 64).
        void* result = o1heapReallocate(heap, a, 32);  // needs 64-byte fragment.
        REQUIRE(result == a);                          // Same pointer for forward expand!
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify content preserved.
        bytes = static_cast<std::uint8_t*>(result);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            REQUIRE(bytes[i] == static_cast<std::uint8_t>(0xAA ^ i));
        }

        o1heapFree(heap, result);
    }

    SECTION("Expand backward into free neighbor")
    {
        // Allocate two blocks, free the first, then expand the second.
        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);  // Blocker to prevent forward expand.
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);

        // Fill b with pattern.
        auto* bytes = static_cast<std::uint8_t*>(b);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            bytes[i] = static_cast<std::uint8_t>(0xBB ^ i);
        }

        // Free a to create free space before b.
        o1heapFree(heap, a);

        // Expand b to need 64-byte fragment.
        void* result = o1heapReallocate(heap, b, 32);
        REQUIRE(result != nullptr);
        REQUIRE(result != b);  // Different pointer for backward expand (data moved)!
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify content preserved.
        bytes = static_cast<std::uint8_t*>(result);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            REQUIRE(bytes[i] == static_cast<std::uint8_t>(0xBB ^ i));
        }

        o1heapFree(heap, result);
        o1heapFree(heap, c);
    }

    SECTION("Alloc-copy-free fallback")
    {
        // Create fragmentation that forces alloc-copy-free.
        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);

        // Fill b with pattern.
        auto* bytes = static_cast<std::uint8_t*>(b);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            bytes[i] = static_cast<std::uint8_t>(0xCC ^ i);
        }

        // b is sandwiched between a and c (both used). Expand b to need 256 bytes.
        void* result = o1heapReallocate(heap, b, 200);
        REQUIRE(result != nullptr);
        REQUIRE(result != b);  // Different pointer (allocated elsewhere).
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify content preserved.
        bytes = static_cast<std::uint8_t*>(result);
        for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
        {
            REQUIRE(bytes[i] == static_cast<std::uint8_t>(0xCC ^ i));
        }

        o1heapFree(heap, a);
        o1heapFree(heap, result);
        o1heapFree(heap, c);
    }

    SECTION("OOM returns NULL and preserves original")
    {
        // Fill most of the heap.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        void*      big       = o1heapAllocate(heap, max_alloc);
        REQUIRE(big != nullptr);

        // Allocate a small block.
        void* small = o1heapAllocate(heap, 1);
        // This might fail if heap is full, but let's try.
        if (small != nullptr)
        {
            // Fill with pattern.
            auto* bytes = static_cast<std::uint8_t*>(small);
            for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
            {
                bytes[i] = static_cast<std::uint8_t>(0xDD ^ i);
            }

            const auto before = o1heapGetDiagnostics(heap);

            // Try to expand small to need more than available.
            void* result = o1heapReallocate(heap, small, max_alloc);
            REQUIRE(result == nullptr);  // OOM!

            const auto after = o1heapGetDiagnostics(heap);
            REQUIRE(after.oom_count == before.oom_count + 1);
            REQUIRE(o1heapDoInvariantsHold(heap));

            // Original should still be valid and content preserved.
            bytes = static_cast<std::uint8_t*>(small);
            for (std::size_t i = 0; i < min_frag - O1HEAP_ALIGNMENT; i++)
            {
                REQUIRE(bytes[i] == static_cast<std::uint8_t>(0xDD ^ i));
            }

            o1heapFree(heap, small);
        }
        o1heapFree(heap, big);
    }

    SECTION("Realloc size sweep")
    {
        // Test reallocating to every size from 1 to max.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);

        for (std::size_t new_size = 1; new_size <= max_alloc; new_size *= 2)
        {
            void* ptr = o1heapAllocate(heap, 64);
            REQUIRE(ptr != nullptr);

            // Fill with pattern.
            auto* bytes = static_cast<std::uint8_t*>(ptr);
            for (std::size_t i = 0; i < 64; i++)
            {
                bytes[i] = static_cast<std::uint8_t>(i * 7 + 0xAB);
            }

            void* result = o1heapReallocate(heap, ptr, new_size);
            if (result != nullptr)
            {
                REQUIRE(reinterpret_cast<std::uintptr_t>(result) % O1HEAP_ALIGNMENT == 0);

                // Verify first min(64, new_size) bytes preserved.
                bytes                  = static_cast<std::uint8_t*>(result);
                const auto check_count = std::min(std::size_t{64}, new_size);
                for (std::size_t i = 0; i < check_count; i++)
                {
                    REQUIRE(bytes[i] == static_cast<std::uint8_t>(i * 7 + 0xAB));
                }

                o1heapFree(heap, result);
            }
            else
            {
                // OOM - free original.
                o1heapFree(heap, ptr);
            }

            REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
            REQUIRE(o1heapDoInvariantsHold(heap));
        }
    }
}

TEST_CASE("Exhaustive: allocation size sweep")
{
    // Test every allocation size from 1 to a reasonable limit.
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 8 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto diag      = o1heapGetDiagnostics(heap);
    const auto max_alloc = o1heapGetMaxAllocationSize(heap);

    // Test sizes from 1 up to max_alloc.
    for (std::size_t size = 1; size <= max_alloc; size++)
    {
        CAPTURE(size);

        void* ptr = o1heapAllocate(heap, size);
        REQUIRE(ptr != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

        // Verify fragment size is correct power of 2.
        const auto after_alloc   = o1heapGetDiagnostics(heap);
        const auto expected_frag = computeFragmentSize(size);
        REQUIRE(after_alloc.allocated == expected_frag);
        REQUIRE((after_alloc.allocated & (after_alloc.allocated - 1)) == 0);  // Power of 2.
        REQUIRE(after_alloc.allocated >= size + O1HEAP_ALIGNMENT);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Write to entire allocation to verify it's usable.
        std::memset(ptr, 0xAB, size);

        o1heapFree(heap, ptr);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
        REQUIRE(o1heapGetDiagnostics(heap).capacity == diag.capacity);
        REQUIRE(o1heapDoInvariantsHold(heap));
    }

    // Verify size just over max fails.
    REQUIRE(o1heapAllocate(heap, max_alloc + 1) == nullptr);
    REQUIRE(o1heapGetDiagnostics(heap).oom_count == 1);
}

TEST_CASE("Exhaustive: systematic merge scenarios")
{
    // Test all four merge scenarios with rigorous position and content verification.
    // Pre-fill arena with a pattern to detect any corruption.
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 4 * KiB> arena{};

    const auto min_frag = O1HEAP_ALIGNMENT * 2U;

    // Helper to fill a block with a deterministic pattern.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    auto fillAndVerify = [](void* ptr, const std::size_t sz, const std::uint8_t pattern) {
        auto* bytes = static_cast<std::uint8_t*>(ptr);
        for (std::size_t i = 0; i < sz; i++)
        {
            bytes[i] = static_cast<std::uint8_t>(pattern ^ (i & 0xFFU));
        }
    };
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    auto checkPattern = [](void* ptr, const std::size_t sz, const std::uint8_t pattern) -> bool {
        const auto* bytes = static_cast<const std::uint8_t*>(ptr);
        for (std::size_t i = 0; i < sz; i++)
        {
            if (bytes[i] != static_cast<std::uint8_t>(pattern ^ (i & 0xFFU)))
            {
                return false;
            }
        }
        return true;
    };

    SECTION("No merge: free middle block, verify position preserved")
    {
        // Pre-fill arena with sentinel pattern.
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        // Allocate three consecutive minimum-size blocks.
        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);

        // Verify blocks are at consecutive positions (each min_frag apart).
        const auto a_addr = reinterpret_cast<std::uintptr_t>(a);
        const auto b_addr = reinterpret_cast<std::uintptr_t>(b);
        const auto c_addr = reinterpret_cast<std::uintptr_t>(c);
        REQUIRE(b_addr == a_addr + min_frag);
        REQUIRE(c_addr == b_addr + min_frag);

        // Fill each block with a distinct pattern.
        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;
        fillAndVerify(a, usable, 0xAA);
        fillAndVerify(b, usable, 0xBB);
        fillAndVerify(c, usable, 0xCC);

        // Free B (middle) - no merge should happen since A and C are used.
        o1heapFree(heap, b);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 2 * min_frag);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify A and C patterns are intact (no corruption from free).
        REQUIRE(checkPattern(a, usable, 0xAA));
        REQUIRE(checkPattern(c, usable, 0xCC));

        // Re-allocate - should get the same slot back (most recently freed).
        void* b2 = o1heapAllocate(heap, 1);
        REQUIRE(b2 != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(b2) == b_addr);  // Same position!
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 3 * min_frag);

        // Verify A and C still intact after re-allocation.
        REQUIRE(checkPattern(a, usable, 0xAA));
        REQUIRE(checkPattern(c, usable, 0xCC));

        o1heapFree(heap, a);
        o1heapFree(heap, b2);
        o1heapFree(heap, c);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Merge left: free A then B, verify merged block position")
    {
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);

        const auto        a_addr = reinterpret_cast<std::uintptr_t>(a);
        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;

        // Verify C is at expected position.
        REQUIRE(reinterpret_cast<std::uintptr_t>(c) == a_addr + 2 * min_frag);

        // Fill C with pattern to verify it survives merge operations.
        fillAndVerify(c, usable, 0xCC);

        // Free A, then B - B should merge left into A's space.
        o1heapFree(heap, a);
        o1heapFree(heap, b);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == min_frag);  // Only C remains.
        REQUIRE(o1heapDoInvariantsHold(heap));

        // C should be intact.
        REQUIRE(checkPattern(c, usable, 0xCC));

        // Now allocate a block that requires 2*min_frag - it should fit in merged A+B space.
        // Request size that rounds up to 2*min_frag: anything > min_frag-overhead.
        void* large = o1heapAllocate(heap, min_frag);
        REQUIRE(large != nullptr);
        // The merged block starts at A's position.
        REQUIRE(reinterpret_cast<std::uintptr_t>(large) == a_addr);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 2 * min_frag + min_frag);

        // Fill the large block and verify C is still intact.
        fillAndVerify(large, min_frag, 0xDD);
        REQUIRE(checkPattern(c, usable, 0xCC));

        o1heapFree(heap, large);
        o1heapFree(heap, c);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Merge right: free C then B, verify merged block usable")
    {
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);

        const auto        b_addr = reinterpret_cast<std::uintptr_t>(b);
        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;

        // Verify consecutive allocation.
        REQUIRE(reinterpret_cast<std::uintptr_t>(a) + min_frag == b_addr);

        fillAndVerify(a, usable, 0xAA);

        // Free C, then B - B should merge right into C's space.
        o1heapFree(heap, c);
        o1heapFree(heap, b);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == min_frag);  // Only A remains.
        REQUIRE(o1heapDoInvariantsHold(heap));

        // A should be intact.
        REQUIRE(checkPattern(a, usable, 0xAA));

        // Allocate into merged B+C space.
        void* large = o1heapAllocate(heap, min_frag);
        REQUIRE(large != nullptr);
        // Merged block starts at B's position.
        REQUIRE(reinterpret_cast<std::uintptr_t>(large) == b_addr);

        // Verify A still intact.
        REQUIRE(checkPattern(a, usable, 0xAA));

        o1heapFree(heap, a);
        o1heapFree(heap, large);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Merge both: free B and D, then C merges all three")
    {
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        void* a = o1heapAllocate(heap, 1);
        void* b = o1heapAllocate(heap, 1);
        void* c = o1heapAllocate(heap, 1);
        void* d = o1heapAllocate(heap, 1);
        void* e = o1heapAllocate(heap, 1);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);
        REQUIRE(d != nullptr);
        REQUIRE(e != nullptr);

        const auto        a_addr = reinterpret_cast<std::uintptr_t>(a);
        const auto        b_addr = reinterpret_cast<std::uintptr_t>(b);
        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;

        // Verify consecutive positioning.
        REQUIRE(b_addr == a_addr + min_frag);
        REQUIRE(reinterpret_cast<std::uintptr_t>(c) == a_addr + 2 * min_frag);
        REQUIRE(reinterpret_cast<std::uintptr_t>(d) == a_addr + 3 * min_frag);
        REQUIRE(reinterpret_cast<std::uintptr_t>(e) == a_addr + 4 * min_frag);

        fillAndVerify(a, usable, 0xAA);
        fillAndVerify(e, usable, 0xEE);

        // Free B and D, leaving: [A used][B free][C used][D free][E used]
        o1heapFree(heap, b);
        o1heapFree(heap, d);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 3 * min_frag);

        // Verify A and E intact.
        REQUIRE(checkPattern(a, usable, 0xAA));
        REQUIRE(checkPattern(e, usable, 0xEE));

        // Free C - should merge B+C+D into one 3*min_frag block.
        o1heapFree(heap, c);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 2 * min_frag);  // A and E only.
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Verify A and E still intact after three-way merge.
        REQUIRE(checkPattern(a, usable, 0xAA));
        REQUIRE(checkPattern(e, usable, 0xEE));

        // The merged block (B+C+D = 3*min_frag) can hold allocations up to 2*min_frag.
        // Request exactly 2*min_frag-overhead to get a 2*min_frag fragment.
        void* merged = o1heapAllocate(heap, min_frag);
        REQUIRE(merged != nullptr);
        // Should be placed at B's original position.
        REQUIRE(reinterpret_cast<std::uintptr_t>(merged) == b_addr);

        // Fill and verify no corruption.
        fillAndVerify(merged, min_frag, 0xBD);
        REQUIRE(checkPattern(a, usable, 0xAA));
        REQUIRE(checkPattern(e, usable, 0xEE));

        o1heapFree(heap, a);
        o1heapFree(heap, merged);
        o1heapFree(heap, e);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    }

    SECTION("Boundary merge: first and last fragments")
    {
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        // Allocate first block.
        void* first = o1heapAllocate(heap, 1);
        REQUIRE(first != nullptr);
        const auto first_addr = reinterpret_cast<std::uintptr_t>(first);

        // Allocate second block.
        void* second = o1heapAllocate(heap, 1);
        REQUIRE(second != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(second) == first_addr + min_frag);

        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;
        fillAndVerify(second, usable, 0x22);

        // Free first - it's at heap boundary (no left neighbor).
        o1heapFree(heap, first);
        REQUIRE(o1heapDoInvariantsHold(heap));
        REQUIRE(checkPattern(second, usable, 0x22));

        // Re-allocate first - should get same position.
        void* first2 = o1heapAllocate(heap, 1);
        REQUIRE(first2 != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(first2) == first_addr);
        REQUIRE(checkPattern(second, usable, 0x22));

        // Now free both in order - they should merge.
        o1heapFree(heap, first2);
        o1heapFree(heap, second);
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);

        // Heap should be fully defragmented - can allocate max.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        void*      full      = o1heapAllocate(heap, max_alloc);
        REQUIRE(full != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(full) == first_addr);
        o1heapFree(heap, full);
    }

    SECTION("Complex interleaved merge pattern")
    {
        std::memset(arena.data(), 0xDE, arena.size());

        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        // Allocate 8 consecutive blocks.
        std::array<void*, 8> blocks{};
        for (auto& blk : blocks)
        {
            blk = o1heapAllocate(heap, 1);
            REQUIRE(blk != nullptr);
        }

        // Verify consecutive positioning.
        const auto base = reinterpret_cast<std::uintptr_t>(blocks.at(0));
        for (std::size_t i = 1; i < blocks.size(); i++)
        {
            REQUIRE(reinterpret_cast<std::uintptr_t>(blocks.at(i)) == base + i * min_frag);
        }

        // Fill all blocks with patterns.
        const std::size_t usable = min_frag - O1HEAP_ALIGNMENT;
        for (std::size_t i = 0; i < blocks.size(); i++)
        {
            fillAndVerify(blocks.at(i), usable, static_cast<std::uint8_t>(0x10U + i));
        }

        // Free even-indexed blocks: 0, 2, 4, 6 - creating fragmentation.
        for (std::size_t i = 0; i < blocks.size(); i += 2)
        {
            o1heapFree(heap, blocks.at(i));
            blocks.at(i) = nullptr;
        }
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 4 * min_frag);

        // Verify odd blocks still intact.
        for (std::size_t i = 1; i < blocks.size(); i += 2)
        {
            REQUIRE(checkPattern(blocks.at(i), usable, static_cast<std::uint8_t>(0x10U + i)));
        }

        // Free odd blocks in reverse order: 7, 5, 3, 1 - each should merge with neighbors.
        o1heapFree(heap, blocks[7]);  // Merges with 6.
        REQUIRE(o1heapDoInvariantsHold(heap));
        o1heapFree(heap, blocks[5]);  // Merges with 4 and 6+7.
        REQUIRE(o1heapDoInvariantsHold(heap));
        o1heapFree(heap, blocks[3]);  // Merges with 2 and 4+5+6+7.
        REQUIRE(o1heapDoInvariantsHold(heap));
        o1heapFree(heap, blocks[1]);  // Merges everything.
        REQUIRE(o1heapDoInvariantsHold(heap));

        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);

        // Should be able to allocate max now.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        void*      full      = o1heapAllocate(heap, max_alloc);
        REQUIRE(full != nullptr);
        o1heapFree(heap, full);
    }
}

TEST_CASE("Exhaustive: deallocation permutations")
{
    // For N allocations, test all N! orderings of deallocation.
    // N=6 gives 720 permutations, N=7 gives 5040, N=8 gives 40320.
    constexpr std::size_t N = 7;

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 4 * KiB> arena{};

    // Generate all permutations of indices 0..N-1.
    std::vector<std::size_t> indices(N);
    std::iota(indices.begin(), indices.end(), 0);

    std::size_t permutation_count = 0;
    do
    {
        auto* const heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        const auto initial_capacity = o1heapGetDiagnostics(heap).capacity;

        // Allocate N blocks with different sizes and fill with patterns.
        std::array<AllocationTracker, N> allocations{};
        std::uint64_t                    seed = 12345;
        for (std::size_t i = 0; i < N; i++)
        {
            allocations.at(i).requested_size = (i + 1) * 10;  // 10, 20, 30, ...
            allocations.at(i).ptr            = o1heapAllocate(heap, allocations.at(i).requested_size);
            REQUIRE(allocations.at(i).ptr != nullptr);
            allocations.at(i).fillPattern(seed++);
        }

        // Verify all patterns before freeing.
        verifyAllPatterns({allocations.begin(), allocations.end()});
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Free in the order specified by current permutation.
        for (const auto idx : indices)
        {
            // Verify remaining patterns are intact.
            for (std::size_t j = 0; j < N; j++)
            {
                if (allocations.at(j).ptr != nullptr)
                {
                    REQUIRE(allocations.at(j).verifyPattern());
                }
            }

            o1heapFree(heap, allocations.at(idx).ptr);
            allocations.at(idx).ptr = nullptr;
            REQUIRE(o1heapDoInvariantsHold(heap));
        }

        // After all frees, heap should be back to initial state.
        const auto final_diag = o1heapGetDiagnostics(heap);
        REQUIRE(final_diag.allocated == 0);
        REQUIRE(final_diag.capacity == initial_capacity);

        // Verify we can allocate the maximum again.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        void*      full      = o1heapAllocate(heap, max_alloc);
        REQUIRE(full != nullptr);
        o1heapFree(heap, full);

        permutation_count++;
    } while (std::next_permutation(indices.begin(), indices.end()));

    // Verify we tested all permutations.
    std::size_t expected_perms = 1;
    for (std::size_t i = 2; i <= N; i++)
    {
        expected_perms *= i;
    }
    REQUIRE(permutation_count == expected_perms);
    std::cout << "Tested " << permutation_count << " deallocation permutations for N=" << N << std::endl;
}

TEST_CASE("Exhaustive: fragmentation stress")
{
    // Create worst-case fragmentation and verify correct behavior.
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 8 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto min_frag = O1HEAP_ALIGNMENT * 2U;

    // Allocate as many minimum-size blocks as possible.
    std::vector<void*> blocks;
    while (true)
    {
        void* ptr = o1heapAllocate(heap, 1);
        if (ptr == nullptr)
        {
            break;
        }
        blocks.push_back(ptr);
    }

    REQUIRE(!blocks.empty());
    const auto num_blocks = blocks.size();
    std::cout << "Allocated " << num_blocks << " minimum-size blocks" << std::endl;

    REQUIRE(o1heapDoInvariantsHold(heap));

    // Free every other block to create maximum fragmentation.
    std::vector<void*> freed_slots;
    for (std::size_t i = 0; i < blocks.size(); i += 2)
    {
        o1heapFree(heap, blocks[i]);
        freed_slots.push_back(blocks[i]);
        blocks[i] = nullptr;
    }

    REQUIRE(o1heapDoInvariantsHold(heap));

    // Now try to allocate something larger than min_frag - should fail due to fragmentation.
    const auto before_oom = o1heapGetDiagnostics(heap);
    void*      large      = o1heapAllocate(heap, min_frag);  // Needs 2*min_frag.
    if (large == nullptr)
    {
        // Expected: fragmentation prevents large allocation.
        const auto after_oom = o1heapGetDiagnostics(heap);
        REQUIRE(after_oom.oom_count == before_oom.oom_count + 1);
    }
    else
    {
        // If it succeeded, free it.
        o1heapFree(heap, large);
    }

    REQUIRE(o1heapDoInvariantsHold(heap));

    // Small allocations should still work - they fit in the freed slots.
    std::vector<void*> refilled;
    for (std::size_t i = 0; i < freed_slots.size(); i++)
    {
        void* ptr = o1heapAllocate(heap, 1);
        REQUIRE(ptr != nullptr);
        refilled.push_back(ptr);
    }

    REQUIRE(o1heapDoInvariantsHold(heap));

    // Free everything.
    for (auto* ptr : blocks)
    {
        if (ptr != nullptr)
        {
            o1heapFree(heap, ptr);
        }
    }
    for (auto* ptr : refilled)
    {
        o1heapFree(heap, ptr);
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    REQUIRE(o1heapDoInvariantsHold(heap));

    // Heap should be defragmented now - can allocate max.
    const auto max_alloc = o1heapGetMaxAllocationSize(heap);
    void*      full      = o1heapAllocate(heap, max_alloc);
    REQUIRE(full != nullptr);
    o1heapFree(heap, full);
}

TEST_CASE("Exhaustive: random walk with content verification", "[long]")
{
    // Long-running random test with content verification and stats tracking.
    // This test aims to run for several minutes to thoroughly explore the state space.
    // Tag [long] allows filtering if needed.

    constexpr std::size_t ArenaSize   = 256 * KiB;
    constexpr std::size_t NumOps      = 5'000'000;  // 5 million operations for thorough coverage.
    constexpr std::size_t MaxAllocReq = 8 * KiB;

    alignas(O1HEAP_ALIGNMENT) static std::array<std::uint8_t, ArenaSize> arena{};

    // Pre-fill arena with sentinel pattern to detect any out-of-bounds writes.
    std::memset(arena.data(), 0xCD, arena.size());

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    Rng                            rng(42);  // Fixed seed for reproducibility.
    std::vector<AllocationTracker> allocations;
    std::size_t                    total_allocs      = 0;
    std::size_t                    total_frees       = 0;
    std::size_t                    oom_events        = 0;
    std::size_t                    tracked_allocated = 0;  // Track allocated for verification.

    // Track allocation positions to verify no overlaps.
    auto checkNoOverlap = [](const std::vector<AllocationTracker>& allocs) {
        for (std::size_t i = 0; i < allocs.size(); i++)
        {
            const auto a_start = reinterpret_cast<std::uintptr_t>(allocs[i].ptr);
            const auto a_end   = a_start + allocs[i].requested_size;
            for (std::size_t j = i + 1; j < allocs.size(); j++)
            {
                const auto b_start = reinterpret_cast<std::uintptr_t>(allocs[j].ptr);
                const auto b_end   = b_start + allocs[j].requested_size;
                // Check for overlap (DeMorgan simplification of: !(a_end <= b_start || b_end <= a_start)).
                if (a_end > b_start && b_end > a_start)
                {
                    return false;
                }
            }
        }
        return true;
    };

    std::size_t total_reallocs = 0;

    for (std::size_t op = 0; op < NumOps; op++)
    {
        // Choose operation: 40% alloc, 35% free, 25% realloc (when allocations exist).
        const auto op_choice  = rng.next(0, 99);
        const bool do_alloc   = allocations.empty() || (op_choice < 40 && allocations.size() < 10000);
        const bool do_realloc = !do_alloc && !allocations.empty() && op_choice < 65;

        if (do_alloc)
        {
            // Allocate with random size - use different size distributions.
            std::size_t       req_size    = 0;
            const std::size_t size_choice = rng.next(0, 100);
            if (size_choice < 50)
            {
                req_size = rng.next(1, 64);  // 50%: small allocations.
            }
            else if (size_choice < 80)
            {
                req_size = rng.next(64, 512);  // 30%: medium allocations.
            }
            else if (size_choice < 95)
            {
                req_size = rng.next(512, 2 * KiB);  // 15%: large allocations.
            }
            else
            {
                req_size = rng.next(2 * KiB, MaxAllocReq);  // 5%: very large.
            }

            void* ptr = o1heapAllocate(heap, req_size);

            if (ptr != nullptr)
            {
                REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

                AllocationTracker tracker;
                tracker.ptr            = ptr;
                tracker.requested_size = req_size;
                tracker.fillPattern(rng.nextU64());
                allocations.push_back(tracker);
                total_allocs++;
                tracked_allocated += computeFragmentSize(req_size);
            }
            else
            {
                oom_events++;
            }
        }
        else if (do_realloc)
        {
            // Realloc a random allocation to a random new size.
            REQUIRE(!allocations.empty());
            const std::size_t idx = rng.next(0, allocations.size() - 1);

            // Verify pattern before reallocating.
            REQUIRE(allocations[idx].verifyPattern());

            const std::size_t old_frag_size = computeFragmentSize(allocations[idx].requested_size);

            // Choose new size with various distributions.
            std::size_t       new_size    = 0;
            const std::size_t size_choice = rng.next(0, 100);
            if (size_choice < 30)
            {
                // Shrink or same size.
                new_size = rng.next(1, std::max(std::size_t{1}, allocations[idx].requested_size));
            }
            else if (size_choice < 60)
            {
                // Modest grow.
                new_size = rng.next(allocations[idx].requested_size, allocations[idx].requested_size * 2 + 64);
            }
            else if (size_choice < 90)
            {
                // Random size.
                new_size = rng.next(1, MaxAllocReq);
            }
            else
            {
                // Large grow.
                new_size = rng.next(MaxAllocReq / 2, MaxAllocReq);
            }

            const auto old_size = allocations[idx].requested_size;
            void*      new_ptr  = o1heapReallocate(heap, allocations[idx].ptr, new_size);

            if (new_ptr != nullptr)
            {
                REQUIRE(reinterpret_cast<std::uintptr_t>(new_ptr) % O1HEAP_ALIGNMENT == 0);

                // Verify content preserved (first min(old, new) bytes).
                const auto*       bytes       = static_cast<const std::uint8_t*>(new_ptr);
                const std::size_t check_count = std::min(old_size, new_size);
                // Regenerate expected pattern for verification.
                const auto expected_pattern = allocations[idx].pattern;
                for (std::size_t i = 0; i < check_count; i++)
                {
                    const auto shift    = static_cast<std::uint64_t>((i % 8U) * 8U);
                    const auto mult     = static_cast<std::uint64_t>(i) * 251ULL;
                    const auto expected = static_cast<std::uint8_t>((expected_pattern >> shift) ^ mult ^ 0xA5ULL);
                    REQUIRE(bytes[i] == expected);
                }

                // Update tracked allocated.
                const std::size_t new_frag_size = computeFragmentSize(new_size);
                tracked_allocated -= old_frag_size;
                tracked_allocated += new_frag_size;

                // Update tracker.
                allocations[idx].ptr            = new_ptr;
                allocations[idx].requested_size = new_size;
                allocations[idx].fillPattern(allocations[idx].pattern);  // Re-fill with same pattern seed.
                total_reallocs++;
            }
            else
            {
                // OOM on realloc - original should still be valid.
                REQUIRE(allocations[idx].verifyPattern());
                oom_events++;
            }
        }
        else
        {
            // Free a random allocation.
            REQUIRE(!allocations.empty());
            const std::size_t idx = rng.next(0, allocations.size() - 1);

            // Verify pattern before freeing.
            REQUIRE(allocations[idx].verifyPattern());

            tracked_allocated -= computeFragmentSize(allocations[idx].requested_size);
            o1heapFree(heap, allocations[idx].ptr);
            allocations.erase(allocations.begin() + static_cast<std::ptrdiff_t>(idx));
            total_frees++;
        }

        // Periodic full verification including allocated tracking.
        if (op % 50000 == 0)
        {
            verifyAllPatterns(allocations);
            REQUIRE(o1heapDoInvariantsHold(heap));
            REQUIRE(checkNoOverlap(allocations));

            // Verify allocated matches tracked value.
            const auto diag = o1heapGetDiagnostics(heap);
            REQUIRE(diag.allocated == tracked_allocated);

            // Progress report.
            if (op % 500000 == 0)
            {
                std::cout << "Random walk: " << op << "/" << NumOps << " ops, " << allocations.size()
                          << " live allocations, " << total_reallocs << " reallocs, " << oom_events << " OOMs"
                          << ", allocated=" << diag.allocated << std::endl;
            }
        }
    }

    // Final verification.
    verifyAllPatterns(allocations);
    REQUIRE(o1heapDoInvariantsHold(heap));
    REQUIRE(checkNoOverlap(allocations));
    REQUIRE(o1heapGetDiagnostics(heap).allocated == tracked_allocated);

    // Free all remaining allocations.
    for (auto& alloc : allocations)
    {
        REQUIRE(alloc.verifyPattern());
        tracked_allocated -= computeFragmentSize(alloc.requested_size);
        o1heapFree(heap, alloc.ptr);
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    REQUIRE(tracked_allocated == 0);
    REQUIRE(o1heapDoInvariantsHold(heap));

    std::cout << "Random walk complete: " << total_allocs << " allocations, " << total_frees << " frees, "
              << total_reallocs << " reallocs, " << oom_events << " OOMs" << std::endl;
}

TEST_CASE("Exhaustive: state space coverage for tiny heap", "[long]")
{
    // For a tiny heap (few fragments), systematically explore reachable states.
    // We use BFS to enumerate all reachable states from the initial state.

    // Heap with capacity for ~8 minimum fragments.
    constexpr std::size_t NumFragments = 8;
    constexpr std::size_t MinFrag      = O1HEAP_ALIGNMENT * 2U;
    constexpr std::size_t ArenaSize    = NumFragments * MinFrag + 1 * KiB;  // Extra for O1HeapInstance.

    alignas(O1HEAP_ALIGNMENT) static std::array<std::uint8_t, ArenaSize> arena{};

    // State is represented as a sorted list of (offset, size) for allocated blocks.
    // We use string serialization for easy hashing.
    using State = std::string;

    auto serializeState = [](const std::vector<AllocationTracker>& allocs) -> State {
        std::vector<std::uintptr_t> offsets;
        for (const auto& a : allocs)
        {
            if (a.ptr != nullptr)
            {
                offsets.push_back(reinterpret_cast<std::uintptr_t>(a.ptr));
            }
        }
        std::sort(offsets.begin(), offsets.end());

        std::string result;
        for (const auto off : offsets)
        {
            result += std::to_string(off) + ",";
        }
        return result;
    };

    std::unordered_set<State> visited;
    std::size_t               max_live_allocs = 0;
    std::size_t               transitions     = 0;

    // Use explicit stack for DFS (to avoid deep recursion).
    struct StackEntry
    {
        std::vector<AllocationTracker> allocs;
    };

    std::vector<StackEntry> stack;
    stack.emplace_back();  // Start with empty allocation list.

    Rng rng(12345);

    while (!stack.empty())
    {
        auto entry = std::move(stack.back());
        stack.pop_back();

        // Reinitialize heap and replay allocations to reach this state.
        auto* heap = initHeap(arena.data(), arena.size());
        REQUIRE(heap != nullptr);

        std::vector<AllocationTracker> current_allocs;
        bool                           replay_ok = true;
        for (const auto& orig : entry.allocs)
        {
            void* ptr = o1heapAllocate(heap, orig.requested_size);
            if (ptr == nullptr)
            {
                replay_ok = false;
                break;
            }
            AllocationTracker t;
            t.ptr            = ptr;
            t.requested_size = orig.requested_size;
            t.fillPattern(rng.nextU64());
            current_allocs.push_back(t);
        }

        if (!replay_ok)
        {
            continue;  // State not reachable with current heap layout.
        }

        const State state = serializeState(current_allocs);
        if (visited.count(state) > 0)
        {
            // Already visited this state - free and continue.
            for (auto& a : current_allocs)
            {
                o1heapFree(heap, a.ptr);
            }
            continue;
        }
        visited.insert(state);
        max_live_allocs = std::max(max_live_allocs, current_allocs.size());

        // Verify invariants in this state.
        verifyAllPatterns(current_allocs);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Generate successor states by trying allocations and frees.

        // Try allocating minimum size.
        {
            void* ptr = o1heapAllocate(heap, 1);
            if (ptr != nullptr)
            {
                // New state with this allocation.
                StackEntry next_entry;
                for (const auto& a : entry.allocs)
                {
                    next_entry.allocs.push_back(a);
                }
                AllocationTracker t;
                t.requested_size = 1;
                next_entry.allocs.push_back(t);

                stack.push_back(std::move(next_entry));
                transitions++;

                o1heapFree(heap, ptr);  // Undo for next exploration.
            }
        }

        // Try freeing each existing allocation.
        for (std::size_t i = 0; i < current_allocs.size(); i++)
        {
            StackEntry next_entry;
            for (std::size_t j = 0; j < entry.allocs.size(); j++)
            {
                if (j != i)
                {
                    next_entry.allocs.push_back(entry.allocs[j]);
                }
            }
            stack.push_back(std::move(next_entry));
            transitions++;
        }

        // Clean up.
        for (auto& a : current_allocs)
        {
            o1heapFree(heap, a.ptr);
        }

        // Limit exploration to avoid combinatorial explosion.
        if (visited.size() > 50000)
        {
            break;
        }
    }

    std::cout << "State space exploration: " << visited.size() << " unique states, " << transitions << " transitions, "
              << "max " << max_live_allocs << " live allocations" << std::endl;

    REQUIRE(visited.size() > 10);  // Sanity check that we explored something.
}

TEST_CASE("Exhaustive: LIFO allocation pattern", "[long]")
{
    // Test stack-like allocation pattern (last allocated, first freed).
    // Run for many cycles to explore the state space thoroughly.
    constexpr int NumCycles = 10000;

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 64 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    Rng                            rng(99);
    std::vector<AllocationTracker> stack_allocs;

    // Perform many push/pop cycles.
    for (int cycle = 0; cycle < NumCycles; cycle++)
    {
        // Push phase: allocate random number of blocks.
        const std::size_t num_push = rng.next(5, 20);
        for (std::size_t i = 0; i < num_push; i++)
        {
            const std::size_t size = rng.next(1, 500);
            void*             ptr  = o1heapAllocate(heap, size);
            if (ptr != nullptr)
            {
                AllocationTracker t;
                t.ptr            = ptr;
                t.requested_size = size;
                t.fillPattern(rng.nextU64());
                stack_allocs.push_back(t);
            }
        }

        // Verify patterns.
        verifyAllPatterns(stack_allocs);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Pop phase: free in reverse order (LIFO).
        const std::size_t num_pop = rng.next(1, stack_allocs.size());
        for (std::size_t i = 0; i < num_pop && !stack_allocs.empty(); i++)
        {
            auto& top = stack_allocs.back();
            REQUIRE(top.verifyPattern());
            o1heapFree(heap, top.ptr);
            stack_allocs.pop_back();
        }

        REQUIRE(o1heapDoInvariantsHold(heap));

        if (cycle % 1000 == 0)
        {
            std::cout << "LIFO cycle " << cycle << "/" << NumCycles << ", " << stack_allocs.size()
                      << " allocations on stack" << std::endl;
        }
    }

    // Clean up.
    while (!stack_allocs.empty())
    {
        o1heapFree(heap, stack_allocs.back().ptr);
        stack_allocs.pop_back();
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    std::cout << "LIFO test completed " << NumCycles << " cycles" << std::endl;
}

TEST_CASE("Exhaustive: FIFO allocation pattern", "[long]")
{
    // Test queue-like allocation pattern (first allocated, first freed).
    // Run for many cycles to explore the state space thoroughly.
    constexpr int NumCycles = 10000;

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 64 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    Rng                            rng(77);
    std::vector<AllocationTracker> queue_allocs;

    // Perform many enqueue/dequeue cycles.
    for (int cycle = 0; cycle < NumCycles; cycle++)
    {
        // Enqueue phase: allocate random number of blocks.
        const std::size_t num_enqueue = rng.next(5, 20);
        for (std::size_t i = 0; i < num_enqueue; i++)
        {
            const std::size_t size = rng.next(1, 500);
            void*             ptr  = o1heapAllocate(heap, size);
            if (ptr != nullptr)
            {
                AllocationTracker t;
                t.ptr            = ptr;
                t.requested_size = size;
                t.fillPattern(rng.nextU64());
                queue_allocs.push_back(t);
            }
        }

        // Verify patterns.
        verifyAllPatterns(queue_allocs);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Dequeue phase: free from front (FIFO).
        const std::size_t num_dequeue = rng.next(1, std::min(queue_allocs.size(), static_cast<std::size_t>(15)));
        for (std::size_t i = 0; i < num_dequeue && !queue_allocs.empty(); i++)
        {
            auto& front = queue_allocs.front();
            REQUIRE(front.verifyPattern());
            o1heapFree(heap, front.ptr);
            queue_allocs.erase(queue_allocs.begin());
        }

        REQUIRE(o1heapDoInvariantsHold(heap));

        if (cycle % 1000 == 0)
        {
            std::cout << "FIFO cycle " << cycle << "/" << NumCycles << ", " << queue_allocs.size()
                      << " allocations in queue" << std::endl;
        }
    }

    // Clean up.
    for (auto& alloc : queue_allocs)
    {
        o1heapFree(heap, alloc.ptr);
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    std::cout << "FIFO test completed " << NumCycles << " cycles" << std::endl;
}

TEST_CASE("Exhaustive: alternating sizes", "[long]")
{
    // Alternate between large and small allocations to stress bin management.
    // Run for many iterations to thoroughly explore the state space.
    constexpr int NumIterations = 100000;

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 128 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto                     min_frag = O1HEAP_ALIGNMENT * 2U;
    Rng                            rng(55);
    std::vector<AllocationTracker> allocations;

    for (int i = 0; i < NumIterations; i++)
    {
        // Alternate: even iterations allocate small, odd allocate large.
        const std::size_t size = (i % 2 == 0) ? rng.next(1, min_frag / 2) : rng.next(min_frag * 4, min_frag * 16);

        void* ptr = o1heapAllocate(heap, size);
        if (ptr != nullptr)
        {
            AllocationTracker t;
            t.ptr            = ptr;
            t.requested_size = size;
            t.fillPattern(rng.nextU64());
            allocations.push_back(t);
        }

        // Occasionally free some allocations.
        if (allocations.size() > 50 && rng.nextBool(0.3))
        {
            const std::size_t idx = rng.next(0, allocations.size() - 1);
            REQUIRE(allocations[idx].verifyPattern());
            o1heapFree(heap, allocations[idx].ptr);
            allocations.erase(allocations.begin() + static_cast<std::ptrdiff_t>(idx));
        }

        if (i % 5000 == 0)
        {
            verifyAllPatterns(allocations);
            REQUIRE(o1heapDoInvariantsHold(heap));

            if (i % 10000 == 0)
            {
                std::cout << "Alternating sizes: " << i << "/" << NumIterations << ", " << allocations.size()
                          << " live allocations" << std::endl;
            }
        }
    }

    // Final cleanup.
    for (auto& alloc : allocations)
    {
        REQUIRE(alloc.verifyPattern());
        o1heapFree(heap, alloc.ptr);
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
    REQUIRE(o1heapDoInvariantsHold(heap));
    std::cout << "Alternating sizes test completed " << NumIterations << " iterations" << std::endl;
}

TEST_CASE("Exhaustive: repeated fill and drain", "[long]")
{
    // Repeatedly fill heap to capacity, then drain completely.
    // This tests the allocator's ability to fully defragment after arbitrary usage.
    constexpr std::size_t NumCycles = 500;  // Many cycles for thorough testing.

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 32 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto initial_capacity = o1heapGetDiagnostics(heap).capacity;

    Rng rng(33);

    for (std::size_t cycle = 0; cycle < NumCycles; cycle++)
    {
        std::vector<AllocationTracker> allocations;

        // Fill phase: allocate until OOM with varying sizes.
        while (true)
        {
            // Use different size distributions each cycle.
            std::size_t size = 0;
            if (cycle % 3 == 0)
            {
                size = rng.next(1, 100);  // Small allocations.
            }
            else if (cycle % 3 == 1)
            {
                size = rng.next(50, 500);  // Medium allocations.
            }
            else
            {
                size = rng.next(100, 2000);  // Large allocations.
            }

            void* ptr = o1heapAllocate(heap, size);
            if (ptr == nullptr)
            {
                break;
            }
            AllocationTracker t;
            t.ptr            = ptr;
            t.requested_size = size;
            t.fillPattern(rng.nextU64());
            allocations.push_back(t);
        }

        REQUIRE(!allocations.empty());
        verifyAllPatterns(allocations);
        REQUIRE(o1heapDoInvariantsHold(heap));

        // Drain phase: free all in random order.
        rng.shuffle(allocations);
        for (auto& alloc : allocations)
        {
            REQUIRE(alloc.verifyPattern());
            o1heapFree(heap, alloc.ptr);
            REQUIRE(o1heapDoInvariantsHold(heap));
        }

        // Verify heap is back to initial state.
        REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
        REQUIRE(o1heapGetDiagnostics(heap).capacity == initial_capacity);

        // Verify can allocate max - proves full defragmentation.
        const auto max_alloc = o1heapGetMaxAllocationSize(heap);
        void*      full      = o1heapAllocate(heap, max_alloc);
        REQUIRE(full != nullptr);
        o1heapFree(heap, full);

        if (cycle % 50 == 0)
        {
            std::cout << "Fill-drain cycle " << cycle << "/" << NumCycles << " complete, " << allocations.size()
                      << " allocations this cycle" << std::endl;
        }
    }

    std::cout << "Completed " << NumCycles << " fill-drain cycles" << std::endl;
}

TEST_CASE("Exhaustive: pointer alignment verification")
{
    // Verify all returned pointers are properly aligned for all sizes.
    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 16 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    // Test many allocation sizes.
    for (std::size_t size = 1; size <= 2000; size++)
    {
        void* ptr = o1heapAllocate(heap, size);
        if (ptr != nullptr)
        {
            // Check alignment.
            REQUIRE(reinterpret_cast<std::uintptr_t>(ptr) % O1HEAP_ALIGNMENT == 0);

            // Check pointer is within arena bounds.
            const auto ptr_addr   = reinterpret_cast<std::uintptr_t>(ptr);
            const auto arena_addr = reinterpret_cast<std::uintptr_t>(arena.data());
            REQUIRE(ptr_addr >= arena_addr);
            REQUIRE(ptr_addr < arena_addr + arena.size());

            // Verify usability by writing.
            std::memset(ptr, 0xAA, size);

            o1heapFree(heap, ptr);
        }
    }

    REQUIRE(o1heapGetDiagnostics(heap).allocated == 0);
}

TEST_CASE("Exhaustive: diagnostics consistency", "[long]")
{
    // Verify diagnostics are always consistent with actual state.
    // This is a long-running test that verifies every diagnostic field after each operation.
    // Includes alloc, free, AND realloc operations.
    constexpr std::size_t NumOps = 2'000'000;  // 2 million operations.

    alignas(O1HEAP_ALIGNMENT) std::array<std::uint8_t, 64 * KiB> arena{};

    auto* const heap = initHeap(arena.data(), arena.size());
    REQUIRE(heap != nullptr);

    const auto capacity = o1heapGetDiagnostics(heap).capacity;

    Rng                            rng(111);
    std::vector<AllocationTracker> allocations;

    std::size_t   tracked_allocated = 0;
    std::size_t   tracked_peak      = 0;
    std::size_t   tracked_peak_req  = 0;
    std::uint64_t tracked_oom       = 0;
    std::size_t   total_reallocs    = 0;

    for (std::size_t op = 0; op < NumOps; op++)
    {
        // Choose operation: 40% alloc, 35% free, 25% realloc (when allocations exist).
        const auto op_choice  = rng.next(0, 99);
        const bool do_alloc   = allocations.empty() || (op_choice < 40 && allocations.size() < 1000);
        const bool do_realloc = !do_alloc && !allocations.empty() && op_choice < 65;

        if (do_alloc)
        {
            const std::size_t req_size = rng.next(1, 4000);
            tracked_peak_req           = std::max(tracked_peak_req, req_size);

            void* ptr = o1heapAllocate(heap, req_size);
            if (ptr != nullptr)
            {
                const std::size_t frag_size = computeFragmentSize(req_size);
                tracked_allocated += frag_size;
                tracked_peak = std::max(tracked_peak, tracked_allocated);

                AllocationTracker t;
                t.ptr            = ptr;
                t.requested_size = req_size;
                t.fillPattern(rng.nextU64());
                allocations.push_back(t);
            }
            else
            {
                tracked_oom++;
            }
        }
        else if (do_realloc)
        {
            // Realloc a random allocation to a random new size.
            const std::size_t idx = rng.next(0, allocations.size() - 1);

            // Verify pattern before reallocating.
            REQUIRE(allocations[idx].verifyPattern());

            const std::size_t old_req_size  = allocations[idx].requested_size;
            const std::size_t old_frag_size = computeFragmentSize(old_req_size);

            // Choose new size with various distributions.
            std::size_t       new_req_size = 0;
            const std::size_t size_choice  = rng.next(0, 100);
            if (size_choice < 30)
            {
                // Shrink or same size.
                new_req_size = rng.next(1, std::max(std::size_t{1}, old_req_size));
            }
            else if (size_choice < 70)
            {
                // Modest grow.
                new_req_size = rng.next(old_req_size, std::min(old_req_size * 2 + 64, std::size_t{4000}));
            }
            else
            {
                // Random size.
                new_req_size = rng.next(1, 4000);
            }

            tracked_peak_req = std::max(tracked_peak_req, new_req_size);

            void* new_ptr = o1heapReallocate(heap, allocations[idx].ptr, new_req_size);

            if (new_ptr != nullptr)
            {
                // Verify content preserved (first min(old, new) bytes).
                const auto*       bytes            = static_cast<const std::uint8_t*>(new_ptr);
                const std::size_t check_count      = std::min(old_req_size, new_req_size);
                const auto        expected_pattern = allocations[idx].pattern;
                for (std::size_t i = 0; i < check_count; i++)
                {
                    const auto shift    = static_cast<std::uint64_t>((i % 8U) * 8U);
                    const auto mult     = static_cast<std::uint64_t>(i) * 251ULL;
                    const auto expected = static_cast<std::uint8_t>((expected_pattern >> shift) ^ mult ^ 0xA5ULL);
                    REQUIRE(bytes[i] == expected);
                }

                // Update tracked allocated: subtract old fragment size, add new fragment size.
                const std::size_t new_frag_size = computeFragmentSize(new_req_size);
                tracked_allocated -= old_frag_size;
                tracked_allocated += new_frag_size;
                // Peak tracking during realloc is complex because different paths (in-place, forward,
                // backward, alloc-copy-free) have different peak behaviors. We verify peak is consistent
                // by checking it never decreases and is always >= allocated after the operation.

                // Update tracker.
                allocations[idx].ptr            = new_ptr;
                allocations[idx].requested_size = new_req_size;
                allocations[idx].fillPattern(allocations[idx].pattern);  // Re-fill with same pattern seed.
                total_reallocs++;
            }
            else
            {
                // OOM on realloc - original should still be valid.
                REQUIRE(allocations[idx].verifyPattern());
                tracked_oom++;
            }
        }
        else
        {
            // Free a random allocation.
            const std::size_t idx       = rng.next(0, allocations.size() - 1);
            const std::size_t frag_size = computeFragmentSize(allocations[idx].requested_size);

            // Verify pattern before free.
            REQUIRE(allocations[idx].verifyPattern());

            o1heapFree(heap, allocations[idx].ptr);
            allocations.erase(allocations.begin() + static_cast<std::ptrdiff_t>(idx));

            tracked_allocated -= frag_size;
        }

        // Verify diagnostics match tracked values on every operation.
        const auto diag = o1heapGetDiagnostics(heap);
        REQUIRE(diag.capacity == capacity);
        REQUIRE(diag.allocated == tracked_allocated);
        // Peak must be >= allocated and never decrease. For realloc, the exact peak depends on
        // which internal path was taken (in-place vs alloc-copy-free), so we verify consistency
        // rather than exact prediction.
        REQUIRE(diag.peak_allocated >= diag.allocated);
        REQUIRE(diag.peak_allocated >= tracked_peak);
        tracked_peak = diag.peak_allocated;  // Update to actual value.
        REQUIRE(diag.peak_request_size == tracked_peak_req);
        REQUIRE(diag.oom_count == tracked_oom);

        // Periodic invariant check and progress report.
        if (op % 100000 == 0)
        {
            REQUIRE(o1heapDoInvariantsHold(heap));
            verifyAllPatterns(allocations);
            std::cout << "Diagnostics consistency: " << op << "/" << NumOps << " ops, " << allocations.size()
                      << " live allocations, " << total_reallocs << " reallocs" << std::endl;
        }
    }

    // Cleanup.
    for (auto& alloc : allocations)
    {
        REQUIRE(alloc.verifyPattern());
        o1heapFree(heap, alloc.ptr);
    }

    std::cout << "Diagnostics consistency verified over " << NumOps << " operations (" << total_reallocs << " reallocs)"
              << std::endl;
}

}  // namespace
