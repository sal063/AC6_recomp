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
//
// READ THE DOCUMENTATION IN README.md.

#ifndef O1HEAP_H_INCLUDED
#define O1HEAP_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The semantic version number of this distribution.
#define O1HEAP_VERSION_MAJOR 3

/// The guaranteed alignment depends on the platform pointer width.
#define O1HEAP_ALIGNMENT (sizeof(void*) * 2U)

/// The definition is private, so the user code can only operate on pointers. This is done to enforce encapsulation.
typedef struct O1HeapInstance O1HeapInstance;

/// Runtime diagnostic information. This information can be used to facilitate runtime self-testing,
/// as required by certain safety-critical development guidelines.
/// If assertion checks are not disabled, the library will perform automatic runtime self-diagnostics that trigger
/// an assertion failure if a heap corruption is detected.
/// Health checks and validation can be done with o1heapDoInvariantsHold().
///
/// TODO NOTICE: Maintenance of this information takes about a dozen cycles at least, which is quite significant
/// compared to the amount of computation needed to do the actual memory management. In the future, we may add a
/// preprocessor option that disables diagnostics for the benefit of the most performance-sensitive applications.
/// If you find this feature relevant for your use case, consider opening a ticket.
typedef struct
{
    /// The total amount of memory available for serving allocation requests (heap size).
    /// The maximum allocation size is (capacity - O1HEAP_ALIGNMENT).
    /// This parameter does not include the overhead used up by O1HeapInstance and arena alignment.
    /// This parameter is constant.
    size_t capacity;

    /// The amount of memory that is currently allocated, including the per-fragment overhead and size alignment.
    /// For example, if the application requested a fragment of size 1 byte, the value reported here may be 32 bytes.
    size_t allocated;

    /// The maximum value of 'allocated' seen since initialization. This parameter is never decreased.
    size_t peak_allocated;

    /// The largest amount of memory that the allocator has attempted to allocate (perhaps unsuccessfully)
    /// since initialization (not including the rounding and the allocator's own per-fragment overhead,
    /// so the total is larger). This parameter is never decreased. The initial value is zero.
    size_t peak_request_size;

    /// The number of times an allocation request could not be completed due to the lack of memory or
    /// excessive fragmentation. OOM stands for "out of memory". This parameter is never decreased.
    uint64_t oom_count;
} O1HeapDiagnostics;

/// o1heapInit() will fail unless the arena size is at least this large.
/// This value depends only on the machine architecture.
/// The other reason to fail is if the arena pointer is not aligned at O1HEAP_ALIGNMENT.
extern const size_t o1heapMinArenaSize;

/// The arena base pointer shall be aligned at O1HEAP_ALIGNMENT, otherwise NULL is returned.
///
/// The total heap capacity cannot exceed approx. (SIZE_MAX/2). If the arena size allows for a larger heap,
/// the excess will be silently truncated away (no error). This is not a realistic use case because a typical
/// application is unlikely to be able to dedicate that much of the address space for the heap.
///
/// The function initializes a new heap instance allocated in the provided arena, taking some of its space for its
/// own needs (normally about 40..600 bytes depending on the architecture, but this parameter is not characterized).
/// A pointer to the newly initialized instance is returned.
///
/// The function fails and returns NULL iff:
/// - The provided space is less than o1heapMinArenaSize.
/// - The base pointer is not aligned at O1HEAP_ALIGNMENT.
/// - The base pointer is NULL.
///
/// An initialized instance does not hold any resources except for the arena memory.
/// Therefore, if the instance is no longer needed, it can be discarded without any de-initialization procedures.
///
/// The heap is not thread-safe; external synchronization may be required.
O1HeapInstance* o1heapInit(void* const base, const size_t size);

/// The semantics follows malloc() with additional guarantees the full list of which is provided below.
///
/// If the allocation request is served successfully, a pointer to the newly allocated memory fragment is returned.
/// The returned pointer is guaranteed to be aligned at O1HEAP_ALIGNMENT.
///
/// If the allocation request cannot be served due to the lack of memory or its excessive fragmentation,
/// a NULL pointer is returned.
///
/// The function is executed in constant time.
/// The allocated memory is NOT zero-filled (because zero-filling is a variable-complexity operation).
void* o1heapAllocate(O1HeapInstance* const handle, const size_t amount);

/// The semantics follows free() with additional guarantees the full list of which is provided below.
///
/// If the pointer does not point to a previously allocated block and is not NULL, the behavior is undefined.
/// Builds where assertion checks are enabled may trigger an assertion failure for some invalid inputs.
///
/// The function is executed in constant time.
void o1heapFree(O1HeapInstance* const handle, void* const pointer);

/// Similar to the standard realloc() with a few improvements. Given a previously allocated fragment and a new size,
/// attempts to resize the fragment.
///
/// - If the pointer is NULL, acts as o1heapAllocate(). The complexity is constant.
///
/// - If the new_amount is zero, acts as o1heapFree() (n.b.: in realloc() this case is implementation-defined).
///   The result should be ignored. The complexity is constant.
///
/// - If the new_amount is not greater than the old fragment size, the same memory pointer is always returned;
///   the data is not moved and the fragment is shrunk in place. The complexity is constant.
///
/// - If the new_amount is greater than the old fragment but there is enough free space after the fragment
///   to expand in-place, the fragment is expanded in place and the same pointer is returned; the data is not moved.
///   The complexity is constant. The contents of the expanded memory is undefined.
///
/// - If the new_amount is greater than the old fragment and there is not enough free space after the fragment to
///   expand in-place, but there is a suitable free space elsewhere, the data is moved and the new pointer is returned.
///   The complexity is LINEAR (sic!) of the size of the old fragment, as it needs to be memmove()d to the new location.
///   The library attempts to relocate the data in a nearby or recently used area to improve cache locality.
///   The new pointer is returned. The new fragment may overlap the original one.
///
/// - Otherwise, there is not enough memory to expand the fragment as requested. The old fragment remains valid as-is,
///   and NULL is returned to indicate failure. The complexity is constant.
///
/// To summarize, the only linear-complexity case is when the new_amount is larger and there is not enough free space
/// following this fragment, necessitating moving the data to a new place; the library avoids this if at all possible.
/// Every other case is constant-complexity as the data is not moved.
void* o1heapReallocate(O1HeapInstance* const handle, void* const pointer, const size_t new_amount);

/// Obtains the maximum theoretically possible allocation size for this heap instance.
/// This is useful when implementing std::allocator_traits<Alloc>::max_size.
size_t o1heapGetMaxAllocationSize(const O1HeapInstance* const handle);

/// Performs a basic sanity check on the heap.
/// This function can be used as a weak but fast method of heap corruption detection.
/// If the handle pointer is NULL, the behavior is undefined.
/// The time complexity is constant.
/// The return value is truth if the heap looks valid, falsity otherwise.
bool o1heapDoInvariantsHold(const O1HeapInstance* const handle);

/// Samples and returns a copy of the diagnostic information, see O1HeapDiagnostics.
/// This function merely copies the structure from an internal storage, so it is fast to return.
/// If the handle pointer is NULL, the behavior is undefined.
O1HeapDiagnostics o1heapGetDiagnostics(const O1HeapInstance* const handle);

#ifdef __cplusplus
}
#endif
#endif  // O1HEAP_H_INCLUDED
