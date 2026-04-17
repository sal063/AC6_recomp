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
// Copyright (c) Pavel Kirienko
// Authors: Pavel Kirienko <pavel.kirienko@zubax.com>

// ReSharper disable CppDFANullDereference CppRedundantCastExpression

#include "o1heap.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------- BUILD CONFIGURATION OPTIONS ----------------------------------------

/// Define this macro to include build configuration header. This is an alternative to the -D compiler flag.
/// Usage example with CMake: "-DO1HEAP_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/my_o1heap_config.h\""
#ifdef O1HEAP_CONFIG_HEADER
#    include O1HEAP_CONFIG_HEADER
#endif

/// The assertion macro defaults to the standard assert().
/// It can be overridden to manually suppress assertion checks or use a different error handling policy.
#ifndef O1HEAP_ASSERT
// Intentional violation of MISRA: the assertion check macro cannot be replaced with a function definition.
#    define O1HEAP_ASSERT(x) assert(x)  // NOSONAR
#endif

/// Allow usage of compiler intrinsics for branch annotation and CLZ.
#ifndef O1HEAP_USE_INTRINSICS
#    define O1HEAP_USE_INTRINSICS 1
#endif

/// Branch probability annotations are used to improve the worst case execution time (WCET). They are entirely optional.
#if O1HEAP_USE_INTRINSICS && !defined(O1HEAP_LIKELY)
#    if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
// Intentional violation of MISRA: branch hinting macro cannot be replaced with a function definition.
#        define O1HEAP_LIKELY(x) __builtin_expect((x), 1)    // NOSONAR
#        define O1HEAP_UNLIKELY(x) __builtin_expect((x), 0)  // NOSONAR
#    endif
#endif
#ifndef O1HEAP_LIKELY
#    define O1HEAP_LIKELY(x) (x)
#endif
#ifndef O1HEAP_UNLIKELY
#    define O1HEAP_UNLIKELY(x) (x)
#endif

/// This option is used for testing only. Do not use in production.
#ifndef O1HEAP_PRIVATE
#    define O1HEAP_PRIVATE static inline
#endif

/// Count leading zeros (CLZ) is used for fast computation of binary logarithm (which needs to be done very often).
/// Most of the modern processors (including the embedded ones) implement dedicated hardware support for fast CLZ
/// computation, which is available via compiler intrinsics. The default implementation will automatically use
/// the intrinsics for some of the compilers; for others it will default to the slow software emulation,
/// which can be overridden by the user via O1HEAP_CONFIG_HEADER. The library guarantees that the argument is positive.
#if O1HEAP_USE_INTRINSICS && !defined(O1HEAP_CLZ)
#    if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
#        define O1HEAP_CLZ __builtin_clzl
#    endif
#endif
#ifndef O1HEAP_CLZ
O1HEAP_PRIVATE uint_fast8_t O1HEAP_CLZ(const size_t x)
{
    O1HEAP_ASSERT(x > 0);
    size_t       t = ((size_t) 1U) << ((sizeof(size_t) * CHAR_BIT) - 1U);
    uint_fast8_t r = 0;
    while ((x & t) == 0)
    {
        t >>= 1U;
        r++;
    }
    return r;
}
#endif

// ---------------------------------------- INTERNAL DEFINITIONS ----------------------------------------

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#    error "Unsupported language: ISO C99 or a newer version is required."
#endif

#if __STDC_VERSION__ < 201112L
// Intentional violation of MISRA: static assertion macro cannot be replaced with a function definition.
#    define static_assert(x, ...) typedef char _static_assert_gl(_static_assertion_, __LINE__)[(x) ? 1 : -1]  // NOSONAR
#    define _static_assert_gl(a, b) _static_assert_gl_impl(a, b)                                              // NOSONAR
// Intentional violation of MISRA: the paste operator ## cannot be avoided in this context.
#    define _static_assert_gl_impl(a, b) a##b  // NOSONAR
#endif

/// The overhead is at most O1HEAP_ALIGNMENT bytes large,
/// then follows the user data which shall keep the next fragment aligned.
#define FRAGMENT_SIZE_MIN (O1HEAP_ALIGNMENT * 2U)

/// This is risky, handle with care: if the allocation amount plus per-fragment overhead exceeds 2**(b-1),
/// where b is the pointer bit width, then ceil(log2(amount)) yields b; then 2**b causes an integer overflow.
/// To avoid this, we put a hard limit on fragment size (which is amount + per-fragment overhead): 2**(b-1)
#define FRAGMENT_SIZE_MAX ((SIZE_MAX >> 1U) + 1U)

/// Normally we should subtract log2(FRAGMENT_SIZE_MIN) but log2 is bulky to compute using the preprocessor only.
/// We will certainly end up with unused bins this way, but it is cheap to ignore.
#define NUM_BINS_MAX (sizeof(size_t) * CHAR_BIT)

static_assert((O1HEAP_ALIGNMENT & (O1HEAP_ALIGNMENT - 1U)) == 0U, "Not a power of 2");
static_assert((FRAGMENT_SIZE_MIN & (FRAGMENT_SIZE_MIN - 1U)) == 0U, "Not a power of 2");
static_assert((FRAGMENT_SIZE_MAX & (FRAGMENT_SIZE_MAX - 1U)) == 0U, "Not a power of 2");

typedef struct Fragment Fragment;

/// Size is computed dynamically from (next - this) or (arena_end - this) for the last fragment.
typedef struct FragmentHeader
{
    Fragment* next;       ///< Next fragment in address order; NULL if this is the last fragment.
    uintptr_t prev_used;  ///< Prev pointer in upper bits, 'used' flag in bit 0.
} FragmentHeader;
static_assert(sizeof(FragmentHeader) == sizeof(void*) * 2U, "Memory layout error");
static_assert(sizeof(FragmentHeader) == O1HEAP_ALIGNMENT, "Memory layout error");

struct Fragment
{
    FragmentHeader header;
    // Everything past the header may spill over into the allocatable space. The header survives across alloc/free.
    Fragment* next_free;  // Next free fragment in the bin; NULL in the last one.
    Fragment* prev_free;  // Same but points back; NULL in the first one.
};
static_assert(sizeof(Fragment) <= FRAGMENT_SIZE_MIN, "Memory layout error");

struct O1HeapInstance
{
    Fragment* bins[NUM_BINS_MAX];  ///< Smallest fragments are in the bin at index 0.
    size_t    nonempty_bin_mask;   ///< Bit 1 represents a non-empty bin; bin at index 0 is for the smallest fragments.

    char* arena_end;  ///< Points past the last byte of the arena; used for computing size of the last fragment.

    O1HeapDiagnostics diagnostics;
};

/// The amount of space allocated for the heap instance.
/// Its size is padded up to O1HEAP_ALIGNMENT to ensure correct alignment of the allocation arena that follows.
#define INSTANCE_SIZE_PADDED ((sizeof(O1HeapInstance) + O1HEAP_ALIGNMENT - 1U) & ~(O1HEAP_ALIGNMENT - 1U))

static_assert(INSTANCE_SIZE_PADDED >= sizeof(O1HeapInstance), "Invalid instance footprint computation");
static_assert((INSTANCE_SIZE_PADDED % O1HEAP_ALIGNMENT) == 0U, "Invalid instance footprint computation");

O1HEAP_PRIVATE size_t larger(const size_t a, const size_t b)
{
    return (a > b) ? a : b;
}

/// Undefined for zero argument.
O1HEAP_PRIVATE uint_fast8_t log2Floor(const size_t x)
{
    O1HEAP_ASSERT(x > 0);
    // NOLINTNEXTLINE redundant cast to the same type.
    return (uint_fast8_t) (((sizeof(x) * CHAR_BIT) - 1U) - ((uint_fast8_t) O1HEAP_CLZ(x)));
}

/// Raise 2 into the specified power.
/// You might be tempted to do something like (1U << power). WRONG! We humans are prone to forgetting things.
/// If you forget to cast your 1U to size_t or ULL, you may end up with undefined behavior.
O1HEAP_PRIVATE size_t pow2(const uint_fast8_t power)
{
    return ((size_t) 1U) << power;
}

/// This is equivalent to pow2(log2Ceil(x)). Undefined for x<2.
O1HEAP_PRIVATE size_t roundUpToPowerOf2(const size_t x)
{
    O1HEAP_ASSERT(x >= 2U);
    // NOLINTNEXTLINE redundant cast to the same type.
    return ((size_t) 1U) << ((sizeof(x) * CHAR_BIT) - ((uint_fast8_t) O1HEAP_CLZ(x - 1U)));
}

// ---------------------------------------- FRAGMENT HEADER ACCESSORS ----------------------------------------

O1HEAP_PRIVATE Fragment* fragGetNext(const Fragment* const frag)
{
    O1HEAP_ASSERT((((size_t) frag) % sizeof(Fragment*)) == 0U);
    Fragment* const out = frag->header.next;
    O1HEAP_ASSERT((((size_t) out) % sizeof(Fragment*)) == 0U);
    return out;
}

O1HEAP_PRIVATE Fragment* fragGetPrev(const Fragment* const frag)
{
    O1HEAP_ASSERT((((size_t) frag) % sizeof(Fragment*)) == 0U);
    Fragment* const out = (Fragment*) (frag->header.prev_used & ~(uintptr_t) 1U);
    O1HEAP_ASSERT((((size_t) out) % sizeof(Fragment*)) == 0U);
    return out;
}

O1HEAP_PRIVATE bool fragIsUsed(const Fragment* const frag)
{
    O1HEAP_ASSERT((((size_t) frag) % sizeof(Fragment*)) == 0U);
    return (frag->header.prev_used & (uintptr_t) 1U) != 0U;
}

O1HEAP_PRIVATE size_t fragGetSize(const O1HeapInstance* const handle, const Fragment* const frag)
{
    O1HEAP_ASSERT((((size_t) frag) % sizeof(Fragment*)) == 0U);
    O1HEAP_ASSERT(((size_t) frag) >= (((size_t) handle) + INSTANCE_SIZE_PADDED));
    O1HEAP_ASSERT((((size_t) fragGetNext(frag)) % sizeof(Fragment*)) == 0U);
    O1HEAP_ASSERT((((size_t) fragGetPrev(frag)) % sizeof(Fragment*)) == 0U);

    const size_t sz = (frag->header.next != NULL) ? (size_t) (((const char*) frag->header.next) - ((const char*) frag))
                                                  : (size_t) (handle->arena_end - ((const char*) frag));

    O1HEAP_ASSERT(sz >= FRAGMENT_SIZE_MIN);
    O1HEAP_ASSERT(sz <= handle->diagnostics.capacity);
    O1HEAP_ASSERT((sz % FRAGMENT_SIZE_MIN) == 0U);
    return sz;
}

O1HEAP_PRIVATE void fragSetNext(Fragment* const frag, Fragment* const value)
{
    O1HEAP_ASSERT((((size_t) frag) % O1HEAP_ALIGNMENT) == 0U);
    O1HEAP_ASSERT((((size_t) value) % O1HEAP_ALIGNMENT) == 0U);
    frag->header.next = value;
}

O1HEAP_PRIVATE void fragSetPrev(Fragment* const frag, Fragment* const value)
{
    O1HEAP_ASSERT((((size_t) frag) % O1HEAP_ALIGNMENT) == 0U);
    O1HEAP_ASSERT((((size_t) value) % O1HEAP_ALIGNMENT) == 0U);
    frag->header.prev_used = (frag->header.prev_used & (uintptr_t) 1U) | (uintptr_t) value;
}

O1HEAP_PRIVATE void fragSetUsed(Fragment* const frag, const bool value)
{
    O1HEAP_ASSERT((((size_t) frag) % O1HEAP_ALIGNMENT) == 0U);
    if (value)
    {
        frag->header.prev_used |= (uintptr_t) 1U;
    }
    else
    {
        frag->header.prev_used &= ~(uintptr_t) 1U;
    }
}

// ---------------------------------------- FRAGMENT MANAGEMENT ----------------------------------------

/// Links two fragments so that their next/prev pointers point to each other; left goes before right.
O1HEAP_PRIVATE void interlink(Fragment* const left, Fragment* const right)
{
    if (O1HEAP_LIKELY(left != NULL))
    {
        fragSetNext(left, right);
    }
    if (O1HEAP_LIKELY(right != NULL))
    {
        fragSetPrev(right, left);
    }
}

/// Adds a new fragment into the appropriate bin and updates the lookup mask.
O1HEAP_PRIVATE void rebin(O1HeapInstance* const handle, Fragment* const fragment, const size_t fragment_size)
{
    O1HEAP_ASSERT(handle != NULL);
    O1HEAP_ASSERT(fragment != NULL);
    O1HEAP_ASSERT((fragment_size % FRAGMENT_SIZE_MIN) == 0U);
    const uint_fast8_t idx = log2Floor(fragment_size / FRAGMENT_SIZE_MIN);  // Round DOWN when inserting.
    O1HEAP_ASSERT(idx < NUM_BINS_MAX);
    // Add the new fragment to the beginning of the bin list.
    // I.e., each allocation will be returning the most-recently-used fragment -- good for caching.
    fragment->next_free = handle->bins[idx];
    fragment->prev_free = NULL;
    if (O1HEAP_LIKELY(handle->bins[idx] != NULL))
    {
        handle->bins[idx]->prev_free = fragment;
    }
    handle->bins[idx] = fragment;
    handle->nonempty_bin_mask |= pow2(idx);
}

/// Removes the specified fragment from its bin.
O1HEAP_PRIVATE void unbin(O1HeapInstance* const handle, const Fragment* const fragment, const size_t fragment_size)
{
    O1HEAP_ASSERT(handle != NULL);
    O1HEAP_ASSERT(fragment != NULL);
    O1HEAP_ASSERT((fragment_size % FRAGMENT_SIZE_MIN) == 0U);
    const uint_fast8_t idx = log2Floor(fragment_size / FRAGMENT_SIZE_MIN);  // Round DOWN when removing.
    O1HEAP_ASSERT(idx < NUM_BINS_MAX);
    // Remove the bin from the free fragment list.
    if (O1HEAP_LIKELY(fragment->next_free != NULL))
    {
        fragment->next_free->prev_free = fragment->prev_free;
    }
    if (O1HEAP_LIKELY(fragment->prev_free != NULL))
    {
        fragment->prev_free->next_free = fragment->next_free;
    }
    // Update the bin header.
    if (O1HEAP_LIKELY(handle->bins[idx] == fragment))
    {
        O1HEAP_ASSERT(fragment->prev_free == NULL);
        handle->bins[idx] = fragment->next_free;
        if (O1HEAP_LIKELY(handle->bins[idx] == NULL))
        {
            handle->nonempty_bin_mask &= ~pow2(idx);
        }
    }
}

// ---------------------------------------- PUBLIC API IMPLEMENTATION ----------------------------------------

const size_t o1heapMinArenaSize = INSTANCE_SIZE_PADDED + FRAGMENT_SIZE_MIN;

O1HeapInstance* o1heapInit(void* const base, const size_t size)
{
    O1HeapInstance* out = NULL;
    if ((base != NULL) && ((((size_t) base) % O1HEAP_ALIGNMENT) == 0U) && (size >= o1heapMinArenaSize))
    {
        // Allocate the core heap metadata structure in the beginning of the arena.
        O1HEAP_ASSERT(((size_t) base) % sizeof(O1HeapInstance*) == 0U);
        out                    = (O1HeapInstance*) base;
        out->nonempty_bin_mask = 0U;
        for (size_t i = 0; i < NUM_BINS_MAX; i++)
        {
            out->bins[i] = NULL;
        }

        // Limit and align the capacity.
        size_t capacity = size - INSTANCE_SIZE_PADDED;
        if (capacity > FRAGMENT_SIZE_MAX)
        {
            capacity = FRAGMENT_SIZE_MAX;
        }
        while ((capacity % FRAGMENT_SIZE_MIN) != 0)
        {
            O1HEAP_ASSERT(capacity > 0U);
            capacity--;
        }
        O1HEAP_ASSERT((capacity % FRAGMENT_SIZE_MIN) == 0);
        O1HEAP_ASSERT((capacity >= FRAGMENT_SIZE_MIN) && (capacity <= FRAGMENT_SIZE_MAX));

        // Initialize the diagnostics.
        out->diagnostics.capacity          = capacity;
        out->diagnostics.allocated         = 0U;
        out->diagnostics.peak_allocated    = 0U;
        out->diagnostics.peak_request_size = 0U;
        out->diagnostics.oom_count         = 0U;

        // Store the arena end pointer for computing size of the last fragment.
        char* const arena_start = ((char*) base) + INSTANCE_SIZE_PADDED;
        out->arena_end          = arena_start + capacity;

        // Initialize the root fragment.
        Fragment* const frag = (Fragment*) (void*) arena_start;
        O1HEAP_ASSERT((((size_t) frag) % O1HEAP_ALIGNMENT) == 0U);
        fragSetNext(frag, NULL);
        fragSetPrev(frag, NULL);
        fragSetUsed(frag, false);
        frag->next_free = NULL;
        frag->prev_free = NULL;
        O1HEAP_ASSERT(fragGetSize(out, frag) == capacity);
        rebin(out, frag, capacity);
        O1HEAP_ASSERT(out->nonempty_bin_mask != 0U);
    }

    return out;
}

void* o1heapAllocate(O1HeapInstance* const handle, const size_t amount)
{
    O1HEAP_ASSERT(handle != NULL);
    O1HEAP_ASSERT(handle->diagnostics.capacity <= FRAGMENT_SIZE_MAX);
    void* out = NULL;

    // If the amount approaches approx. SIZE_MAX/2, an undetected integer overflow may occur.
    // To avoid that, we do not attempt allocation if the amount exceeds the hard limit.
    // We perform multiple redundant checks to account for a possible unaccounted overflow.
    if (O1HEAP_LIKELY((amount > 0U) && (amount <= (handle->diagnostics.capacity - O1HEAP_ALIGNMENT))))
    {
        // Add the header size and align the allocation size to the power of 2.
        // See "Timing-Predictable Memory Allocation In Hard Real-Time Systems", Herter, page 27.
        const size_t alloc_size = roundUpToPowerOf2(amount + O1HEAP_ALIGNMENT);
        O1HEAP_ASSERT(alloc_size <= FRAGMENT_SIZE_MAX);
        O1HEAP_ASSERT(alloc_size >= FRAGMENT_SIZE_MIN);
        O1HEAP_ASSERT(alloc_size >= amount + O1HEAP_ALIGNMENT);
        O1HEAP_ASSERT((alloc_size & (alloc_size - 1U)) == 0U);  // Is power of 2.

        // Since alloc_size and FRAGMENT_SIZE_MIN are both powers of 2, the quotient is also a power of 2,
        // meaning log2Ceil == log2Floor. We use log2Floor because it is slightly faster (no x-1 before CLZ).
        const size_t alloc_bins = alloc_size / FRAGMENT_SIZE_MIN;
        O1HEAP_ASSERT((alloc_bins & (alloc_bins - 1U)) == 0U);         // Is power of 2 => log2Ceil == log2Floor.
        const uint_fast8_t optimal_bin_index = log2Floor(alloc_bins);  // Should be ceil, but in this case identical.
        O1HEAP_ASSERT(optimal_bin_index < NUM_BINS_MAX);
        const size_t candidate_bin_mask = ~(pow2(optimal_bin_index) - 1U);

        // Find the smallest non-empty bin we can use.
        const size_t suitable_bins     = handle->nonempty_bin_mask & candidate_bin_mask;
        const size_t smallest_bin_mask = suitable_bins & ~(suitable_bins - 1U);  // Clear all bits but the lowest.
        if (O1HEAP_LIKELY(smallest_bin_mask != 0))
        {
            O1HEAP_ASSERT((smallest_bin_mask & (smallest_bin_mask - 1U)) == 0U);  // Is power of 2.
            const uint_fast8_t bin_index = log2Floor(smallest_bin_mask);
            O1HEAP_ASSERT(bin_index >= optimal_bin_index);
            O1HEAP_ASSERT(bin_index < NUM_BINS_MAX);

            // The bin we found shall not be empty, otherwise it's a state divergence (memory corruption?).
            Fragment* const frag = handle->bins[bin_index];
            O1HEAP_ASSERT(frag != NULL);
            const size_t frag_size = fragGetSize(handle, frag);
            O1HEAP_ASSERT(frag_size >= alloc_size);
            O1HEAP_ASSERT((frag_size % FRAGMENT_SIZE_MIN) == 0U);
            O1HEAP_ASSERT(!fragIsUsed(frag));
            unbin(handle, frag, frag_size);

            // Split the fragment if it is too large.
            const size_t leftover = frag_size - alloc_size;
            O1HEAP_ASSERT(leftover < handle->diagnostics.capacity);  // Overflow check.
            O1HEAP_ASSERT(leftover % FRAGMENT_SIZE_MIN == 0U);       // Alignment check.
            if (O1HEAP_LIKELY(leftover >= FRAGMENT_SIZE_MIN))
            {
                Fragment* const new_frag = (Fragment*) (void*) (((char*) frag) + alloc_size);
                fragSetUsed(new_frag, false);
                interlink(new_frag, fragGetNext(frag));
                interlink(frag, new_frag);
                O1HEAP_ASSERT(leftover == fragGetSize(handle, new_frag));
                rebin(handle, new_frag, leftover);
            }

            // Update the diagnostics.
            O1HEAP_ASSERT((handle->diagnostics.allocated % FRAGMENT_SIZE_MIN) == 0U);
            handle->diagnostics.allocated += alloc_size;
            O1HEAP_ASSERT(handle->diagnostics.allocated <= handle->diagnostics.capacity);
            handle->diagnostics.peak_allocated =
                larger(handle->diagnostics.peak_allocated, handle->diagnostics.allocated);

            // Finalize the fragment we just allocated.
            O1HEAP_ASSERT(fragGetSize(handle, frag) >= amount + O1HEAP_ALIGNMENT);
            fragSetUsed(frag, true);

            out = ((char*) frag) + O1HEAP_ALIGNMENT;
        }
    }

    // Update the diagnostics.
    handle->diagnostics.peak_request_size = larger(handle->diagnostics.peak_request_size, amount);
    if (O1HEAP_LIKELY((out == NULL) && (amount > 0U)))
    {
        handle->diagnostics.oom_count++;
    }

    return out;
}

void o1heapFree(O1HeapInstance* const handle, void* const pointer)
{
    O1HEAP_ASSERT(handle != NULL);
    O1HEAP_ASSERT(handle->diagnostics.capacity <= FRAGMENT_SIZE_MAX);
    if (O1HEAP_LIKELY(pointer != NULL))  // NULL pointer is a no-op.
    {
        Fragment* const frag = (Fragment*) (void*) (((char*) pointer) - O1HEAP_ALIGNMENT);

        // Check for heap corruption in debug builds.
        O1HEAP_ASSERT(((size_t) frag) % sizeof(Fragment*) == 0U);
        O1HEAP_ASSERT(((size_t) frag) >= (((size_t) handle) + INSTANCE_SIZE_PADDED));
        O1HEAP_ASSERT(((size_t) frag) <=
                      (((size_t) handle) + INSTANCE_SIZE_PADDED + handle->diagnostics.capacity - FRAGMENT_SIZE_MIN));
        O1HEAP_ASSERT(fragIsUsed(frag));  // Catch double-free
        O1HEAP_ASSERT(((size_t) fragGetNext(frag)) % sizeof(Fragment*) == 0U);
        O1HEAP_ASSERT(((size_t) fragGetPrev(frag)) % sizeof(Fragment*) == 0U);
        const size_t frag_size = fragGetSize(handle, frag);
        O1HEAP_ASSERT(frag_size >= FRAGMENT_SIZE_MIN);
        O1HEAP_ASSERT(frag_size <= handle->diagnostics.capacity);
        O1HEAP_ASSERT((frag_size % FRAGMENT_SIZE_MIN) == 0U);

        // Even if we're going to drop the fragment later, mark it free anyway to prevent double-free.
        fragSetUsed(frag, false);

        // Update the diagnostics. It must be done before merging because it invalidates the fragment size information.
        O1HEAP_ASSERT(handle->diagnostics.allocated >= frag_size);  // Heap corruption check.
        handle->diagnostics.allocated -= frag_size;

        // Merge with siblings and insert the returned fragment into the appropriate bin and update metadata.
        Fragment* const prev       = fragGetPrev(frag);
        Fragment* const next       = fragGetNext(frag);
        const bool      join_left  = (prev != NULL) && (!fragIsUsed(prev));
        const bool      join_right = (next != NULL) && (!fragIsUsed(next));
        if (join_left && join_right)  // [ prev ][ this ][ next ] => [ ------- prev ------- ]
        {
            const size_t prev_size = fragGetSize(handle, prev);
            const size_t next_size = fragGetSize(handle, next);
            unbin(handle, prev, prev_size);
            unbin(handle, next, next_size);
            interlink(prev, fragGetNext(next));
            rebin(handle, prev, prev_size + frag_size + next_size);
        }
        else if (join_left)  // [ prev ][ this ][ next ] => [ --- prev --- ][ next ]
        {
            const size_t prev_size = fragGetSize(handle, prev);
            unbin(handle, prev, prev_size);
            interlink(prev, next);
            rebin(handle, prev, prev_size + frag_size);
        }
        else if (join_right)  // [ prev ][ this ][ next ] => [ prev ][ --- this --- ]
        {
            const size_t next_size = fragGetSize(handle, next);
            unbin(handle, next, next_size);
            interlink(frag, fragGetNext(next));
            rebin(handle, frag, frag_size + next_size);
        }
        else
        {
            rebin(handle, frag, frag_size);
        }
    }
}

void* o1heapReallocate(O1HeapInstance* const handle, void* const pointer, const size_t new_amount)
{
    O1HEAP_ASSERT(handle != NULL);
    O1HEAP_ASSERT(handle->diagnostics.capacity <= FRAGMENT_SIZE_MAX);

    // SPECIAL CASE: Allocation delegation.
    if (O1HEAP_UNLIKELY(pointer == NULL))
    {
        return o1heapAllocate(handle, new_amount);  // MISRA: Early return simplifies control flow.
    }

    // SPECIAL CASE: Free delegation. This is a common implementation-defined extension in the standard realloc().
    if (O1HEAP_UNLIKELY(new_amount == 0U))
    {
        o1heapFree(handle, pointer);
        return NULL;  // MISRA: Early return simplifies control flow.
    }

    // SPECIAL CASE: Prevent size overflow like in o1heapAllocate().
    handle->diagnostics.peak_request_size = larger(handle->diagnostics.peak_request_size, new_amount);
    if (O1HEAP_UNLIKELY(new_amount > (handle->diagnostics.capacity - O1HEAP_ALIGNMENT)))
    {
        handle->diagnostics.oom_count++;
        return NULL;  // MISRA: Early return simplifies control flow.
    }

    // NORMAL REALLOC BEHAVIORS. The edge cases have been handled above.
    Fragment* const frag          = (Fragment*) (void*) (((char*) pointer) - O1HEAP_ALIGNMENT);
    const size_t    frag_size     = fragGetSize(handle, frag);
    const size_t    old_amount    = frag_size - O1HEAP_ALIGNMENT;
    const size_t    new_frag_size = roundUpToPowerOf2(new_amount + O1HEAP_ALIGNMENT);
    O1HEAP_ASSERT((new_frag_size <= FRAGMENT_SIZE_MAX) && (new_frag_size >= FRAGMENT_SIZE_MIN));
    O1HEAP_ASSERT(new_frag_size <= handle->diagnostics.capacity);
    O1HEAP_ASSERT(fragIsUsed(frag));  // Catch use-after-free.

    Fragment*    prev      = fragGetPrev(frag);
    Fragment*    next      = fragGetNext(frag);
    const bool   prev_free = (prev != NULL) && (!fragIsUsed(prev));
    const bool   next_free = (next != NULL) && (!fragIsUsed(next));
    const size_t prev_size = prev_free ? fragGetSize(handle, prev) : 0U;
    const size_t next_size = next_free ? fragGetSize(handle, next) : 0U;

    // SHRINK OR SAME SIZE: new_frag_size <= frag_size. Data stays in place.
    if (O1HEAP_UNLIKELY(new_frag_size <= frag_size))
    {
        const size_t leftover = frag_size - new_frag_size;
        O1HEAP_ASSERT((leftover % FRAGMENT_SIZE_MIN) == 0U);
        if (O1HEAP_LIKELY(leftover >= FRAGMENT_SIZE_MIN))
        {
            O1HEAP_ASSERT(handle->diagnostics.allocated >= leftover);
            handle->diagnostics.allocated -= leftover;
            Fragment* const new_frag = (Fragment*) (void*) (((char*) frag) + new_frag_size);
            fragSetUsed(new_frag, false);
            interlink(frag, new_frag);
            if (O1HEAP_LIKELY(next_free))  // [ frag ][ new ][ next ] => [ frag ][ --- new --- ]
            {
                unbin(handle, next, next_size);
                interlink(new_frag, fragGetNext(next));
                O1HEAP_ASSERT(fragGetSize(handle, new_frag) == (leftover + next_size));
                rebin(handle, new_frag, leftover + next_size);
            }
            else  // [ frag ][ new ][ next ]
            {
                interlink(new_frag, next);
                O1HEAP_ASSERT(fragGetSize(handle, new_frag) == leftover);
                rebin(handle, new_frag, leftover);
            }
            O1HEAP_ASSERT(fragGetSize(handle, frag) == new_frag_size);
            O1HEAP_ASSERT(fragGetSize(handle, new_frag) == (next_free ? (leftover + next_size) : leftover));
        }
        return pointer;  // MISRA: Early return simplifies control flow.
    }

    // EXPAND FORWARD: next is free and current+next >= new_frag_size. Data stays in place.
    if (next_free && ((frag_size + next_size) >= new_frag_size))
    {
        unbin(handle, next, next_size);
        const size_t leftover = (frag_size + next_size) - new_frag_size;
        O1HEAP_ASSERT((leftover % FRAGMENT_SIZE_MIN) == 0U);
        if (O1HEAP_LIKELY(leftover >= FRAGMENT_SIZE_MIN))  // [ frag ][ --- next --- ] => [ --- frag --- ][ next ]
        {
            Fragment* const new_frag = (Fragment*) (void*) (((char*) frag) + new_frag_size);
            fragSetUsed(new_frag, false);
            interlink(new_frag, fragGetNext(next));
            interlink(frag, new_frag);
            rebin(handle, new_frag, leftover);
            handle->diagnostics.allocated += new_frag_size - frag_size;
        }
        else  // [ frag ][ --- next --- ] => [ --- frag --- ]
        {
            interlink(frag, fragGetNext(next));
            handle->diagnostics.allocated += next_size;
        }
        handle->diagnostics.peak_allocated = larger(handle->diagnostics.peak_allocated, handle->diagnostics.allocated);
        return pointer;  // MISRA: Early return simplifies control flow.
    }

    // EXPAND BACKWARD AND FORWARD: prev is free; next maybe free; prev+current+next >= new_frag_size.
    // Data must be moved, this is unavoidable because there is not enough space ahead.
    // Note that since the move size is not greater than the current fragment, memmove will not invalidate the next
    // fragment, but it may invalidate the current fragment.
    if (prev_free && ((prev_size + frag_size + next_size) >= new_frag_size))
    {
        unbin(handle, prev, prev_size);
        if (next_free)
        {
            unbin(handle, next, next_size);
        }
        void* const out = ((char*) prev) + O1HEAP_ALIGNMENT;  // Move all the way to the back before fragments updated.
        (void) memmove(out, pointer, old_amount);  // ATTENTION: Invalidates the old frag due to potential overwrite.
        fragSetUsed(prev, true);
        const size_t leftover = (prev_size + frag_size + next_size) - new_frag_size;
        O1HEAP_ASSERT((leftover % FRAGMENT_SIZE_MIN) == 0U);
        if (O1HEAP_LIKELY(leftover >= FRAGMENT_SIZE_MIN))
        {
            Fragment* const new_frag = (Fragment*) (void*) (((char*) prev) + new_frag_size);
            fragSetUsed(new_frag, false);
            interlink(new_frag, next_free ? fragGetNext(next) : next);
            interlink(prev, new_frag);  // NOLINT(readability-suspicious-call-argument)
            rebin(handle, new_frag, leftover);
            handle->diagnostics.allocated += new_frag_size - frag_size;
        }
        else
        {
            interlink(prev, next_free ? fragGetNext(next) : next);
            handle->diagnostics.allocated += prev_size + next_size;
        }
        handle->diagnostics.peak_allocated = larger(handle->diagnostics.peak_allocated, handle->diagnostics.allocated);
        return out;  // MISRA: Early return simplifies control flow.
    }

    // ALLOCATE NEW BLOCK: copy data, free old block. In-place or near-place expansion not possible.
    // This is the final resort. The normal allocate also handles the OOM count update.
    void* const out = o1heapAllocate(handle, new_amount);
    if (out != NULL)
    {
        (void) memcpy(out, pointer, old_amount);
        o1heapFree(handle, pointer);
    }
    return out;
}

size_t o1heapGetMaxAllocationSize(const O1HeapInstance* const handle)
{
    O1HEAP_ASSERT(handle != NULL);
    // The largest allocation is smaller (up to almost two times) than the arena capacity,
    // due to the power-of-two padding and the fragment header overhead.
    return pow2(log2Floor(handle->diagnostics.capacity)) - O1HEAP_ALIGNMENT;
}

bool o1heapDoInvariantsHold(const O1HeapInstance* const handle)
{
    O1HEAP_ASSERT(handle != NULL);
    bool valid = true;

    // Check the bin mask consistency.
    for (size_t i = 0; i < NUM_BINS_MAX; i++)  // Dear compiler, feel free to unroll this loop.
    {
        const bool mask_bit_set = (handle->nonempty_bin_mask & pow2((uint_fast8_t) i)) != 0U;
        const bool bin_nonempty = handle->bins[i] != NULL;
        valid                   = valid && (mask_bit_set == bin_nonempty);
    }

    // Create a local copy of the diagnostics struct.
    const O1HeapDiagnostics diag = handle->diagnostics;

    // Capacity check.
    valid = valid && (diag.capacity <= FRAGMENT_SIZE_MAX) && (diag.capacity >= FRAGMENT_SIZE_MIN) &&
            ((diag.capacity % FRAGMENT_SIZE_MIN) == 0U);

    // Allocation info check.
    valid = valid && (diag.allocated <= diag.capacity) && ((diag.allocated % FRAGMENT_SIZE_MIN) == 0U) &&
            (diag.peak_allocated <= diag.capacity) && (diag.peak_allocated >= diag.allocated) &&
            ((diag.peak_allocated % FRAGMENT_SIZE_MIN) == 0U);

    // Peak request check
    valid = valid && ((diag.peak_request_size < diag.capacity) || (diag.oom_count > 0U));
    if (diag.peak_request_size == 0U)
    {
        valid = valid && (diag.peak_allocated == 0U) && (diag.allocated == 0U) && (diag.oom_count == 0U);
    }
    else
    {
        valid = valid &&  // Overflow on summation is possible but safe to ignore.
                (((diag.peak_request_size + O1HEAP_ALIGNMENT) <= diag.peak_allocated) || (diag.oom_count > 0U));
    }

    return valid;
}

O1HeapDiagnostics o1heapGetDiagnostics(const O1HeapInstance* const handle)
{
    O1HEAP_ASSERT(handle != NULL);
    const O1HeapDiagnostics out = handle->diagnostics;
    return out;
}
