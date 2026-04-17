<div align="center">

# O(1) Heap

_The Fastest Heap in the West <sup>[*](https://x.com/PavelKirienko/status/1922316725195882928)</sup>_

[![Main Workflow](https://github.com/pavel-kirienko/o1heap/actions/workflows/main.yml/badge.svg)](https://github.com/pavel-kirienko/o1heap/actions/workflows/main.yml)

</div>

-----

O1Heap is a highly deterministic constant-complexity memory allocator designed for
hard real-time high-integrity embedded systems.

The allocator offers a constant worst-case execution time (WCET) that is also extremely low,
and a well-characterized and easily predictable worst-case memory fragmentation (consumption) (WCMC) bound.
It allows the designer to statically prove its temporal and spatial properties for a given application,
which makes it suitable for high-integrity embedded systems.

O1Heap is implemented in C99/C11 following MISRA C:2012; it is extremely compact (<500 LoC) and is trivial to
understand and validate.
It is designed to be usable across all conventional architectures out of the box, from 8-bit to 64-bit systems.

⚡ As a reference, on an RP2350 CM33, allocation takes ≤125 cycles and deallocation takes ≤115 cycles —
*always*, irrespective of the size, preceding (de)allocation sequence, fragmentation, or memory usage —
making it not only suitable for hard real-time but also one of the fastest allocators out there.
Similar results have been observed on a Cortex M4 MCU.

## 📚 Usage

### Integration

Copy the files `o1heap.c` and `o1heap.h` (find them under `o1heap/`) into your project tree.
Either keep them in the same directory, or make sure that the directory that contains the header
is added to the set of include look-up paths.
No special compiler options are needed to compile the source file (if you find this to be untrue, please open a ticket).

Dedicate a memory arena for the heap, and pass a pointer to it along with its size to the initialization function
`o1heapInit(..)`.

Allocate and deallocate memory using `o1heapAllocate(..)` and `o1heapFree(..)`.
Their semantics are compatible with `malloc(..)` and `free(..)` plus additional behavioral guarantees
(constant timing, bounded fragmentation).

If necessary, periodically invoke `o1heapDoInvariantsHold(..)` to ensure that the heap is functioning correctly
and its internal data structures are not damaged.

Avoid concurrent access to the heap. Use locking if necessary.

### Example

```c++
#include "o1heap.h"
#include <stdalign.h>

static alignas(O1HEAP_ALIGNMENT) unsigned char heap_arena[32768];

int main(void)
{
    O1HeapInstance* heap = o1heapInit(heap_arena, sizeof(heap_arena));
    if (heap == NULL) {
        return 1;  // Initialization failed -- arena not aligned or too small
    }
    void* ptr = o1heapAllocate(heap, 200);  // Always takes the same time to complete.
    if (ptr != NULL) {
        // Use the allocated memory...
        o1heapFree(heap, ptr);  // Also constant-time!
    }
    return 0;
}
```

### Build configuration options

The preprocessor options given below can be overridden to fine-tune the implementation.
None of them are mandatory to use.

#### O1HEAP_CONFIG_HEADER

Define this optional macro like `O1HEAP_CONFIG_HEADER="path/to/my_o1heap_config.h"` to pass build configuration macros.
This is useful because some build systems do not allow passing function-like macros via command line flags.

#### O1HEAP_ASSERT(x)

The macro `O1HEAP_ASSERT(x)` can be defined to customize the assertion handling or to disable it.
To disable assertion checks, the macro should expand to `(void)(x)`.
If not specified, the macro expands to the standard assertion check macro `assert(x)` as defined in `<assert.h>`.

#### O1HEAP_LIKELY(x) & O1HEAP_UNLIKELY(x)

Some of the conditional branching statements are equipped with this annotation to hint the compiler that
the generated code should be optimized for the case where the corresponding branch is (not) taken.
This is done to reduce the worst-case execution time.

The macro should expand to a compiler-specific branch weighting intrinsic,
or to the original expression `(x)` if no such hinting is desired.
Defaults are provided for well-known compilers.

#### O1HEAP_CLZ(x)

The count leading zeros (CLZ) function is used for fast binary logarithm computation (which has to be done
multiple times per allocation, so its performance is critical).
Most of the modern processors implement dedicated hardware support for fast CLZ computation,
which is available via compiler intrinsics.

If not overridden by the user, for some compilers `O1HEAP_CLZ(x)` will expand to the appropriate intrinsic
(e.g., `__builtin_clzl(x)` for GCC/Clang).
For other compilers it will default to a slow software implementation,
which is likely to significantly degrade the performance of the library.

## ⚙️ Design

### Objectives

- Memory allocation and deallocation routines are constant-time and fast.

- For a given peak memory requirement $M$, the worst-case memory consumption $H$ is predictable
  (i.e., the worst-case heap fragmentation is well-characterized).
  The application designer shall be able to easily predict the amount of memory that needs to be provided to the
  memory allocator to ensure that out-of-memory (OOM) failures provably cannot occur at runtime
  under any circumstances.

- The implementation shall be simple and conform to relevant high-integrity coding guidelines.

### Theory

This implementation derives from or is based on the ideas presented in:

- "Timing-Predictable Memory Allocation In Hard Real-Time Systems" -- J. Herter, 2014.
- "An algorithm with constant execution time for dynamic storage allocation" -- T. Ogasawara, 1995.
- "Worst case fragmentation of first fit and best fit storage allocation strategies" -- J. M. Robson, 1975.

This implementation does not make any non-trivial assumptions about the behavioral properties of the application.
While it has been shown to be possible to construct a constant-complexity allocator that demonstrates better
worst- and average-case memory requirements by making assumptions about the memory (de-)allocation patterns,
this implementation makes no assumptions about the application.

The library implements a modified half-fit algorithm -- a constant-complexity strategy originally proposed by Ogasawara.
In this implementation, memory is allocated in fragments whose size is rounded up to the next integer power of two.
The worst-case memory consumption (WCMC) $H$ of this allocation strategy has been shown to be:

$$
H(M,n) = 2 \ M \ (1 + \lceil{} \log_2 n \rceil{})
$$

Where $M$ is the peak total memory requirement of the application
(i.e., sum of sizes of all allocated fragments when the heap memory utilization is at its peak)
and $n$ is the maximum contiguous fragment that can be requested by the application.
The provided equation is a generalized case that does not explicitly take into account
a possible per-fragment metadata allocation overhead.

Memory allocators used in general-purpose (non-real-time and/or non-high-integrity)
applications often leverage a different class of algorithms
which may feature poorer worst-case performance metrics but perform (much) better on average.
For a hard real-time system, the average case performance is generally less relevant,
so it can be excluded from analysis.

The ***two-level segregated fit*** (TLSF) algorithm is a more complex $O(1)$ algorithm similar to half-fit
that is often found in real-time applications.
While TLSF provides a better average-case memory fragmentation due to its more sophisticated approach to
heap segmentation,
its worst-case fragmentation bound is often higher than that of half-fit (for practically useful heap parameters)
and is the same as for an ordinary best-fit allocator,
being approximately $H = M (n-2)$.
Due to its increased internal complexity, TLSF offers a somewhat higher (albeit still constant) WCET
and requires a larger number of lines of code to implement.
From this it is possible to conclude that half-fit can be considered a superior choice for high-integrity applications
compared to TLSF.

| Allocation strategy | WCMC                                         |
|---------------------|----------------------------------------------|
| First-fit           | $H =   M \ (1 + \lceil{} \log_2 n \rceil{})$ |
| Half-fit            | $H = 2 M \ (1 + \lceil{} \log_2 n \rceil{})$ |
| Best-fit            | $H =   M \ (n - 2)$                          |
| TLSF                | (see best-fit)                               |

The state of *catastrophic fragmentation* is a state where the allocator is unable to serve
a memory allocation request even if there is enough free memory due to its suboptimal arrangement.
By definition, if the amount of memory available to the allocator is not less than $H$, then the state of
catastrophic fragmentation cannot occur.

The above-defined theoretical worst-case upper bound H may be prohibitively high for some
memory-constrained applications.
It has been shown \[Robson 1975] that under a typical workload,
for a sufficiently high amount of memory available to the allocator which is less than $H$,
the probability of a (de-)allocation sequence that results in catastrophic fragmentation is low.
When combined with an acceptable failure probability and a set of adequate assumptions about the behaviors of
the application, this property may allow the designer to reduce the amount of memory dedicated to
the heap while ensuring a sufficient degree of predictability and reliability.
The methods of such optimization are outside the scope of this document;
interested readers are advised to consult with the referred publications.

Following some of the ideas from \[Herter 2014], this implementation takes caching-related issues into consideration
by choosing the most recently used memory fragments to minimize cache misses in the application.

### Implementation

The implemented variant of half-fit allocates memory in fragments of size:

$$
F(r) = 2^{\lceil{} log_2 (r+a) \rceil{}}
$$

Where $r>0$ is the requested allocation size and $a$ is the fixed per-allocation metadata overhead
implicitly introduced by the allocator for memory management needs.
The size of the overhead $a$ is represented in the codebase as `O1HEAP_ALIGNMENT`,
because it also dictates the allocated memory pointer alignment.
Due to the overhead, the maximum amount of memory available to the application per allocation is
$F^\prime{}(r) = F(r) - a$.
The amount of the overhead per allocation and, therefore, pointer alignment is 2×(pointer width);
e.g., for a 32-bit platform, the overhead/alignment is 8 bytes (64 bits).

From the above follows that $F(r) \ge 2 a$.
Remember that $r>0$ -- following the semantics of `malloc(..)`,
the allocator returns a null pointer if a zero-sized allocation is requested.

It has been mentioned that the abstract definition of $H$ does not take into account the
implementation-specific overheads.
Said overheads should be considered when calculating the amount memory needed for a specific application.
We define a refined worst-case memory consumption (WCMC) model below.

$$
n_f = \lceil{} \frac{n}{l} \rceil{}
$$

$$
M_f = \lceil{} \frac{M}{l} \rceil{}
$$

$$
k = M_f - n_f + 1
$$

Where $l$ -- the smallest amount of memory that may be requested by the application;
$n_f$ -- the size of the largest allocation expressed as the number of min-size fragments;
$M_f$ -- the total amount of heap space that may be requested by the application in min-size fragments;
$k$ -- the maximum number of fragments.
The worst case number of min-size memory fragments required is $H_f(M_f,n_f) = H(M_f,n_f)$.
The total amount of space needed to accommodate the per-fragment overhead is $k\times{}a$.
Then, the total WCMC, expressed in bytes, is:

$$
H_b(M,n,l,a) = a \ k + \frac{ 2 \ l \ n \ M_f \ (\lceil{} log_2 \ n_f \rceil{} + 1) }{ l+n }
$$

**The above equation should be used for sizing the heap space.**
Observe that the case of $l=n$ degenerates to the standard fixed-size block allocator.

The following illustration shows the worst-case memory consumption (WCMC) for some common memory sizes;
as explained above, $l$ is chosen by the application designer freely,
and $a$ is the value of `O1HEAP_ALIGNMENT` which is platform-dependent:

![WCMC](docs/H.png "Total worst-case memory consumption (H) as a function of max fragment size (n) and total memory need (M)")

## 🛠️ Development

Please refer to the continuous integration configuration to see how to build and test the library.
The code must be `clang-format`ted.

To release a new version, update the version number macro in the header file and create a new semver git tag like
`2.3.0`. Publish a new release on GitHub as well.

MISRA compliance is enforced with the help of Clang-Tidy.
Every intentional deviation shall be documented and justified in-place using the following notation,
followed by the appropriate static analyser warning suppression statement:

```c
// Intentional violation of MISRA: <valid reason here>
// NOLINT(*-specific-rule)
```

## 📖 Further reading

- [Timing-Predictable Memory Allocation In Hard Real-Time Systems](https://publikationen.sulb.uni-saarland.de/bitstream/20.500.11880/26614/1/diss.pdf), J. Herter, 2014.
- [Worst case fragmentation of first fit and best fit storage allocation strategies](https://academic.oup.com/comjnl/article/20/3/242/751782), J. M. Robson, 1975.
- [Dynamic Memory Allocation In SQLite](https://sqlite.org/malloc.html) -- on Robson proof and deterministic fragmentation.
- [Динамическая память в системах жёсткого реального времени](https://habr.com/ru/post/486650/) -- issues with dynamic memory allocation in modern embedded RTOS and related popular misconceptions.

## 📆 Changelog

### v3.0

Version 3.0 reduces the per-fragment overhead from 4×(pointer width) to 2×(pointer width) by packing the fragment header.
On 32-bit platforms, `O1HEAP_ALIGNMENT` and the per-fragment overhead are now 8 bytes instead of 16.
This change saves memory but has some performance cost; e.g., on RP2350 (Cortex M33) allocation is ≈19% slower
(allocation mean 119 cycles vs. 100 cycles, deallocation mean 79 vs. 74 cycles;
see the on-target benchmark in `perftest/`).
The trade-off is believed to be justifiable for virtually all applications.

`o1heapReallocate` is added, which is constant-complexity except for the case when the old fragment cannot be
expanded in-place. See the API docs for details.

The trace events introduced in v2.2 have been removed due to unclean integration and relative lack of use.
They may reappear in a future version, perhaps designed differently; please open an issue to discuss.

### v2.2

- Add optional trace events, enabled via the `O1HEAP_TRACE` build-time option.
- Add [`o1heapMinArenaSize`](https://github.com/pavel-kirienko/o1heap/issues/17#issuecomment-1518245157).
- Add [`o1heapGetMaxAllocationSize`](https://github.com/pavel-kirienko/o1heap/issues/18).

### v2.1

- Significantly accelerate (de-)allocation by replacing the naïve log2 implementation with fast CLZ intrinsics;
  see `O1HEAP_CLZ(x)`.
- Do not require char to be 8-bit wide: replace `uint8_t` with `uint_fast8_t`.
  This is to enhance compatibility with odd embedded platforms where `CHAR_BIT!=8` (e.g., ADSP TS-201, TMS320C2804).

### v2.0

- Remove critical section hooks to enhance MISRA conformance [#4](https://github.com/pavel-kirienko/o1heap/issues/4)
- Add support for config header via `O1HEAP_CONFIG_HEADER` [#5](https://github.com/pavel-kirienko/o1heap/issues/5)

### v1.0

The first release.

## ⚖️ License

The library is available under the terms of the MIT License.
Please find it in the file `LICENSE`.
