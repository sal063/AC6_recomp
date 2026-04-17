#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC
#define REX_PLATFORM_MAC 1
#elif defined(_WIN32)
#define REX_PLATFORM_WIN32 1
#elif defined(__ANDROID__)
#define REX_PLATFORM_ANDROID 1
#define REX_PLATFORM_LINUX 1
#elif defined(__linux__)
#define REX_PLATFORM_GNU_LINUX 1
#define REX_PLATFORM_LINUX 1
#else
#error Unsupported target OS.
#endif

#ifndef REX_PLATFORM_MAC
#define REX_PLATFORM_MAC 0
#endif
#ifndef REX_PLATFORM_WIN32
#define REX_PLATFORM_WIN32 0
#endif
#ifndef REX_PLATFORM_ANDROID
#define REX_PLATFORM_ANDROID 0
#endif
#ifndef REX_PLATFORM_GNU_LINUX
#define REX_PLATFORM_GNU_LINUX 0
#endif
#ifndef REX_PLATFORM_LINUX
#define REX_PLATFORM_LINUX 0
#endif

#if defined(__clang__)
#define REX_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#define REX_COMPILER_MSVC 1
#elif defined(__GNUC__)
#define REX_COMPILER_GNUC 1
#elif defined(__MINGW32__)
#define REX_COMPILER_MINGW32 1
#elif defined(__INTEL_COMPILER)
#define REX_COMPILER_INTEL 1
#else
#define REX_COMPILER_UNKNOWN 1
#endif

#ifndef REX_COMPILER_CLANG
#define REX_COMPILER_CLANG 0
#endif
#ifndef REX_COMPILER_MSVC
#define REX_COMPILER_MSVC 0
#endif
#ifndef REX_COMPILER_GNUC
#define REX_COMPILER_GNUC 0
#endif
#ifndef REX_COMPILER_MINGW32
#define REX_COMPILER_MINGW32 0
#endif
#ifndef REX_COMPILER_INTEL
#define REX_COMPILER_INTEL 0
#endif
#ifndef REX_COMPILER_UNKNOWN
#define REX_COMPILER_UNKNOWN 0
#endif

#if defined(_M_AMD64) || defined(__amd64__) || defined(__x86_64__)
#define REX_ARCH_AMD64 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define REX_ARCH_ARM64 1
#elif defined(_M_IX86) || defined(__i386__) || defined(_M_ARM) || defined(__arm__)
#error Rex is not supported on 32-bit platforms.
#elif defined(_M_PPC) || defined(__powerpc__) || defined(__powerpc64__)
#define REX_ARCH_PPC 1
#endif

#ifndef REX_ARCH_AMD64
#define REX_ARCH_AMD64 0
#endif
#ifndef REX_ARCH_ARM64
#define REX_ARCH_ARM64 0
#endif
#ifndef REX_ARCH_PPC
#define REX_ARCH_PPC 0
#endif

#if REX_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#if REX_PLATFORM_WIN32
#include <intrin.h>
#elif REX_ARCH_AMD64
#include <x86intrin.h>
#endif

#if REX_PLATFORM_MAC
#include <libkern/OSByteOrder.h>
#endif

#if REX_COMPILER_MSVC
#define _REXPACKEDSCOPE(body) __pragma(pack(push, 1)) body __pragma(pack(pop));
#else
#define _REXPACKEDSCOPE(body) \
  _Pragma("pack(push, 1)") body; \
  _Pragma("pack(pop)");
#endif

#define REXPACKEDSTRUCT(name, value) _REXPACKEDSCOPE(struct name value)
#define REXPACKEDSTRUCTANONYMOUS(value) _REXPACKEDSCOPE(struct value)
#define REXPACKEDUNION(name, value) _REXPACKEDSCOPE(union name value)

#if REX_COMPILER_CLANG || REX_COMPILER_GNUC
#define REX_HAS_BUILTIN_STRLEN 1
#define REX_LACKS_FLOAT_FROM_CHARS 1
#else
#define REX_HAS_BUILTIN_STRLEN 0
#define REX_LACKS_FLOAT_FROM_CHARS 0
#endif

namespace rex {

inline constexpr char kPathSeparator = REX_PLATFORM_WIN32 ? '\\' : '/';
inline constexpr char kGuestPathSeparator = '\\';

}  // namespace rex
