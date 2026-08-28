/**
 * @file        system/diag_crash_handler.cpp
 * @brief       Crash reporting implementation (Windows; POSIX stubs below).
 *
 * Everything on the crash path follows the broken-process rules: all buffers
 * are preallocated at Install, formatting uses fmt::format_to_n into those
 * buffers (no heap), files are written with raw Win32 APIs, no locks are
 * taken (the log tail uses try_lock and is skipped if held), a re-entry
 * guard turns a fault inside the handler into an immediate TerminateProcess,
 * and the file is written incrementally most-valuable-first so a handler
 * that dies partway still leaves the useful part on disk.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/diag/crash_handler.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#include <fmt/format.h>

#include <rex/logging/sink.h>
#include <rex/platform.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>

#if REX_PLATFORM_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>

namespace rex::diag::crash {

namespace {

constexpr size_t kScratchSize = 256 * 1024;
constexpr size_t kLogTailLines = 200;
constexpr int kMaxGuestFrames = 32;
constexpr int kMaxHostFrames = 48;

struct HostFunctionEntry {
  uintptr_t host;
  uint32_t guest;
};

struct CrashState {
  bool installed = false;
  std::atomic<bool> in_handler{false};
  std::atomic<bool> report_written{false};
  std::atomic<bool> orderly_shutdown{false};

  // Fixed at install; used verbatim by the handler.
  wchar_t directory[MAX_PATH] = {};
  char app_name[64] = {};
  char build_title[128] = {};
  char build_commit[64] = {};
  char build_timestamp[64] = {};
  char session_id[64] = {};

  // Guest memory bounds for classification + safe backchain reads.
  std::atomic<uintptr_t> guest_base{0};
  std::atomic<uint64_t> guest_extent{0};

  // Guest function table sorted by host address (built at registration,
  // never touched again). Lookup at crash time is a binary search.
  std::vector<HostFunctionEntry> functions_by_host;

  rex::LogCaptureSink* log_tail_sink = nullptr;
  ContextProvider context_provider = nullptr;

  uintptr_t exe_base = 0;
  uintptr_t exe_size = 0;

  // The crash path's only working memory.
  char scratch[kScratchSize];
  char log_tail[64 * 1024];

};

CrashState& S() {
  static CrashState state;
  return state;
}

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

// ---------------------------------------------------------------------------
// Crash-safe append formatting into the scratch buffer.

struct Writer {
  char* data;
  size_t capacity;
  size_t size = 0;

  void Append(const char* text, size_t len) {
    if (size + len >= capacity)
      len = capacity - size - 1;
    std::memcpy(data + size, text, len);
    size += len;
  }

  void Append(const char* text) { Append(text, std::strlen(text)); }

  template <typename... Args>
  void Format(fmt::format_string<Args...> format, Args&&... args) {
    auto result = fmt::format_to_n(data + size, capacity - size - 1, format,
                                   std::forward<Args>(args)...);
    size += (std::min)(result.size, capacity - size - 1);
  }

  void Clear() { size = 0; }
};

// ---------------------------------------------------------------------------
// File output: raw Win32, incremental, flushed after every section.

HANDLE OpenCrashFile() {
  auto& s = S();
  SYSTEMTIME time;
  GetLocalTime(&time);
  wchar_t path[MAX_PATH + 64];
  int dir_length = 0;
  while (s.directory[dir_length] && dir_length < MAX_PATH)
    ++dir_length;
  std::memcpy(path, s.directory, dir_length * sizeof(wchar_t));
  wchar_t name[64];
  int name_length =
      _snwprintf_s(name, _countof(name), _TRUNCATE, L"\\crash-%04u%02u%02u-%02u%02u%02u.txt",
                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
  if (name_length <= 0)
    return INVALID_HANDLE_VALUE;
  std::memcpy(path + dir_length, name, (name_length + 1) * sizeof(wchar_t));
  return CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, nullptr);
}

void WriteAndFlush(HANDLE file, Writer& writer) {
  if (file == INVALID_HANDLE_VALUE || writer.size == 0)
    return;
  DWORD written = 0;
  WriteFile(file, writer.data, static_cast<DWORD>(writer.size), &written, nullptr);
  FlushFileBuffers(file);
  writer.Clear();
}

// ---------------------------------------------------------------------------
// Guest-side: safe reads, function naming, backchain walk.

bool GuestAddressInRange(uintptr_t host_address) {
  auto& s = S();
  uintptr_t base = s.guest_base.load(std::memory_order_relaxed);
  uint64_t extent = s.guest_extent.load(std::memory_order_relaxed);
  return base && host_address >= base && host_address < base + extent;
}

// Read one big-endian u32 from guest memory without faulting: the guest map
// is reserved but only partially committed, so every read is probed first.
bool SafeReadGuestU32(uint32_t guest_address, uint32_t* out) {
  auto& s = S();
  uintptr_t base = s.guest_base.load(std::memory_order_relaxed);
  uint64_t extent = s.guest_extent.load(std::memory_order_relaxed);
  if (!base || guest_address + 4ull > extent)
    return false;
  const void* host = reinterpret_cast<const void*>(base + guest_address);
  MEMORY_BASIC_INFORMATION info;
  if (!VirtualQuery(host, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  uint32_t value;
  std::memcpy(&value, host, 4);
  *out = _byteswap_ulong(value);
  return true;
}

// Host code address -> the generated function containing it, via the
// host-sorted table. Returns nullptr when the address is not inside
// generated code. The caller needs both the guest address and the function
// start (for the +offset), so hand back the entry rather than looking it up
// twice.
const HostFunctionEntry* GuestFunctionForHostAddress(uintptr_t host_address) {
  const auto& table = S().functions_by_host;
  if (table.empty())
    return nullptr;
  size_t lo = 0, hi = table.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (table[mid].host <= host_address)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == 0)
    return nullptr;
  const HostFunctionEntry* entry = &table[lo - 1];
  // Generated functions are well under 1 MB; a hit further away than that
  // means the address is not inside generated code at all.
  if (host_address - entry->host > (1u << 20))
    return nullptr;
  return entry;
}

void AppendHostFrame(Writer& writer, uintptr_t pc) {
  auto& s = S();
  if (!s.exe_base || pc < s.exe_base || (s.exe_size && pc >= s.exe_base + s.exe_size)) {
    writer.Format("  0x{:016X}  (system module)\n", pc);
    return;
  }
  if (const HostFunctionEntry* entry = GuestFunctionForHostAddress(pc)) {
    writer.Format("  exe+0x{:08X}  rex_sub_{:08X}+0x{:X}\n", pc - s.exe_base,
                  entry->guest, pc - entry->host);
  } else {
    writer.Format("  exe+0x{:08X}  (host code)\n", pc - s.exe_base);
  }
}

// The guest call stack straight from guest memory: 0(r1) is the caller's
// stack pointer, and the current function's saved LR sits 8 bytes below the
// caller's frame base (generated prologues do `stw r12,-8(r1)` before
// `stwu r1,-N(r1)`). Needs no symbols and no unwind tables.
void AppendGuestBackchain(Writer& writer, const PPCContext* context) {
  uint32_t stack_pointer = context->r1.u32;
  writer.Format("  lr  0x{:08X}  (return address of the innermost frame)\n",
                static_cast<uint32_t>(context->lr));
  for (int frame = 0; frame < kMaxGuestFrames; ++frame) {
    uint32_t caller_sp = 0;
    if (!SafeReadGuestU32(stack_pointer, &caller_sp) || caller_sp == 0 ||
        caller_sp <= stack_pointer) {
      writer.Format("  (backchain ends at sp 0x{:08X})\n", stack_pointer);
      break;
    }
    uint32_t saved_lr = 0;
    if (SafeReadGuestU32(caller_sp - 8, &saved_lr) && saved_lr) {
      writer.Format("  sp  0x{:08X}  return 0x{:08X}\n", caller_sp, saved_lr);
    } else {
      writer.Format("  sp  0x{:08X}  (no readable saved lr)\n", caller_sp);
    }
    stack_pointer = caller_sp;
  }
}

void AppendGuestRegisters(Writer& writer, const PPCContext* context) {
  const PPCRegister* gprs[32] = {
      &context->r0,  &context->r1,  &context->r2,  &context->r3,  &context->r4,
      &context->r5,  &context->r6,  &context->r7,  &context->r8,  &context->r9,
      &context->r10, &context->r11, &context->r12, &context->r13, &context->r14,
      &context->r15, &context->r16, &context->r17, &context->r18, &context->r19,
      &context->r20, &context->r21, &context->r22, &context->r23, &context->r24,
      &context->r25, &context->r26, &context->r27, &context->r28, &context->r29,
      &context->r30, &context->r31};
  for (int i = 0; i < 32; i += 4) {
    writer.Format("  r{:<2} {:016X}  r{:<2} {:016X}  r{:<2} {:016X}  r{:<2} {:016X}\n", i,
                  gprs[i]->u64, i + 1, gprs[i + 1]->u64, i + 2, gprs[i + 2]->u64, i + 3,
                  gprs[i + 3]->u64);
  }
  uint32_t cr = (context->cr0.raw() << 28) | (context->cr1.raw() << 24) |
                (context->cr2.raw() << 20) | (context->cr3.raw() << 16) |
                (context->cr4.raw() << 12) | (context->cr5.raw() << 8) |
                (context->cr6.raw() << 4) | context->cr7.raw();
  writer.Format("  lr  {:016X}  ctr {:016X}  cr {:08X}  xer so={} ov={} ca={}\n", context->lr,
                context->ctr.u64, cr, context->xer.so, context->xer.ov, context->xer.ca);
}

// ---------------------------------------------------------------------------
// Host-side stack walk from a CONTEXT, no dbghelp: RtlLookupFunctionEntry +
// RtlVirtualUnwind are exported by ntdll/kernel32 and safe here.

void AppendHostStack(Writer& writer, CONTEXT* context) {
  CONTEXT unwind_context = *context;
  for (int frame = 0; frame < kMaxHostFrames && unwind_context.Rip; ++frame) {
    AppendHostFrame(writer, static_cast<uintptr_t>(unwind_context.Rip));
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function_entry =
        RtlLookupFunctionEntry(unwind_context.Rip, &image_base, nullptr);
    if (!function_entry) {
      // Leaf function: return address is at RSP.
      unwind_context.Rip = *reinterpret_cast<DWORD64*>(unwind_context.Rsp);
      unwind_context.Rsp += 8;
      continue;
    }
    PVOID handler_data = nullptr;
    DWORD64 establisher_frame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, unwind_context.Rip, function_entry,
                     &unwind_context, &handler_data, &establisher_frame, nullptr);
  }
}

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
      return "STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:
      return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
      return "IN_PAGE_ERROR";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
      return "FLT_INVALID_OPERATION";
    default:
      return "EXCEPTION";
  }
}

// ---------------------------------------------------------------------------
// The report builder. `kind` names the trigger; `pointers` is null for
// non-exception triggers (abort/terminate/...), in which case the current
// context is captured for the host walk.

void WriteCrashReport(const char* kind, EXCEPTION_POINTERS* pointers, const char* extra) {
  auto& s = S();
  if (s.report_written.exchange(true, std::memory_order_acq_rel))
    return;

  HANDLE file = OpenCrashFile();
  Writer writer{s.scratch, kScratchSize};

  // --- Header: crash nature + build identity. The single most important
  // block - everything below it is best-effort.
  writer.Format("=== {} CRASH REPORT ===\n", s.app_name);
  writer.Format("Session {}\n", s.session_id);
  writer.Format("Build {} commit {} built {}\n", s.build_title, s.build_commit,
                s.build_timestamp);
  writer.Format("Trigger {}\n", kind);
  if (extra && extra[0])
    writer.Format("Detail {}\n", extra);

  uintptr_t fault_address = 0;
  bool is_access_violation = false;
  const char* access_kind = "";
  if (pointers) {
    DWORD code = pointers->ExceptionRecord->ExceptionCode;
    uintptr_t exception_address =
        reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
    if (s.exe_base && exception_address >= s.exe_base &&
        (!s.exe_size || exception_address < s.exe_base + s.exe_size)) {
      writer.Format("Exception {} (0x{:08X}) at exe+0x{:X}\n", ExceptionName(code), code,
                    exception_address - s.exe_base);
    } else {
      writer.Format("Exception {} (0x{:08X}) at 0x{:016X} (outside exe)\n",
                    ExceptionName(code), code, exception_address);
    }
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
        pointers->ExceptionRecord->NumberParameters >= 2) {
      is_access_violation = true;
      fault_address = static_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionInformation[1]);
      switch (pointers->ExceptionRecord->ExceptionInformation[0]) {
        case 0:
          access_kind = "reading";
          break;
        case 1:
          access_kind = "writing";
          break;
        case 8:
          access_kind = "executing";
          break;
        default:
          access_kind = "accessing";
          break;
      }
      writer.Format("Fault {} address 0x{:016X}\n", access_kind, fault_address);
    }
  }

  // --- Fault classification: the one line that routes a report.
  if (is_access_violation) {
    uintptr_t base = s.guest_base.load(std::memory_order_relaxed);
    if (GuestAddressInRange(fault_address)) {
      writer.Format(
          "Classification: GUEST pointer - the faulting address is guest 0x{:08X} inside "
          "the guest memory map (a guest-side pointer bug or corrupted guest state)\n",
          static_cast<uint32_t>(fault_address - base));
    } else if (base && fault_address < 0x10000) {
      writer.Append(
          "Classification: null/near-null HOST pointer - our code, not guest data\n");
    } else {
      writer.Append("Classification: HOST address - our code, not a guest pointer\n");
    }
  }

  // --- Thread identity.
  writer.Format("Thread id {}", GetCurrentThreadId());
  {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto get_description = kernel32 ? reinterpret_cast<GetThreadDescriptionFn>(
                                          GetProcAddress(kernel32, "GetThreadDescription"))
                                    : nullptr;
    PWSTR description = nullptr;
    if (get_description && SUCCEEDED(get_description(GetCurrentThread(), &description)) &&
        description) {
      char narrow[128];
      int i = 0;
      for (; i < 127 && description[i]; ++i)
        narrow[i] = description[i] < 128 ? static_cast<char>(description[i]) : '?';
      narrow[i] = 0;
      writer.Format(" \"{}\"", narrow);
      LocalFree(description);
    }
  }
  writer.Append("\n");
  WriteAndFlush(file, writer);

  // --- Guest context: registers + backchain, straight from the thread-local
  // PPCContext. No symbols involved.
  auto* thread_state = rex::runtime::ThreadState::Get();
  PPCContext* guest_context = thread_state ? thread_state->context() : nullptr;
  if (guest_context) {
    writer.Append("\n[GUEST STACK]\n");
    writer.Format("  Innermost guest function: walk the host stack below; ctx.lr 0x{:08X} "
                  "is the last guest return address taken\n",
                  static_cast<uint32_t>(guest_context->lr));
    AppendGuestBackchain(writer, guest_context);
    writer.Append("\n[GUEST REGISTERS]\n");
    AppendGuestRegisters(writer, guest_context);
  } else {
    writer.Append("\n[GUEST STACK]\nno guest context bound to this thread (host-only "
                  "thread)\n");
  }
  WriteAndFlush(file, writer);

  // --- Host stack: raw RVAs for the offline symbolizer, with guest function
  // names attached where frames land inside generated code.
  writer.Append("\n[HOST STACK] (exe+RVA; decode offline: tools/symbolize_crash.py)\n");
  if (pointers) {
    CONTEXT context_copy = *pointers->ContextRecord;
    AppendHostStack(writer, &context_copy);
  } else {
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&context);
    AppendHostStack(writer, &context);
  }
  WriteAndFlush(file, writer);

  // --- Memory state.
  writer.Append("\n[MEMORY]\n");
  {
    MEMORYSTATUSEX memory_status = {};
    memory_status.dwLength = sizeof(memory_status);
    if (GlobalMemoryStatusEx(&memory_status)) {
      writer.Format("  System: {} MB physical, {} MB available, load {}%\n",
                    memory_status.ullTotalPhys >> 20, memory_status.ullAvailPhys >> 20,
                    memory_status.dwMemoryLoad);
    }
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
      writer.Format("  Process: working set {} MB (peak {} MB), pagefile {} MB\n",
                    counters.WorkingSetSize >> 20, counters.PeakWorkingSetSize >> 20,
                    counters.PagefileUsage >> 20);
    }
    uintptr_t base = s.guest_base.load(std::memory_order_relaxed);
    if (base) {
      writer.Format("  Guest map: host base 0x{:016X}, extent {} MB\n", base,
                    s.guest_extent.load(std::memory_order_relaxed) >> 20);
    }
  }

  // --- Loaded modules: injected overlays/capture layers are a real crash
  // source (renderdoc.dll has form here).
  writer.Append("\n[MODULES]\n");
  {
    HMODULE modules[128];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
      DWORD count = (std::min)(needed / static_cast<DWORD>(sizeof(HMODULE)),
                               static_cast<DWORD>(_countof(modules)));
      for (DWORD i = 0; i < count; ++i) {
        char module_name[MAX_PATH];
        if (K32GetModuleFileNameExA(GetCurrentProcess(), modules[i], module_name,
                                    sizeof(module_name))) {
          const char* base_name = std::strrchr(module_name, '\\');
          writer.Format("  0x{:016X} {}\n", reinterpret_cast<uintptr_t>(modules[i]),
                        base_name ? base_name + 1 : module_name);
        }
      }
    }
  }
  WriteAndFlush(file, writer);

  // --- Optional embedded context. The crash file must answer as much as it
  // can ALONE, because the logs are replaced on the next launch. Anything
  // that registered a provider (a session report, say) is copied in verbatim
  // here; with no provider the section is simply absent.
  if (s.context_provider) {
    const char* context_data = nullptr;
    size_t context_size = 0;
    s.context_provider(&context_data, &context_size);
    if (context_data && context_size) {
      writer.Append("\n[CONTEXT]\n");
      WriteAndFlush(file, writer);
      DWORD written = 0;
      WriteFile(file, context_data, static_cast<DWORD>(context_size), &written, nullptr);
    }
  }

  // --- Log tail (best effort - never blocks on a held lock).
  writer.Append("\n[LOG TAIL]\n");
  WriteAndFlush(file, writer);
  if (s.log_tail_sink) {
    size_t tail_size =
        s.log_tail_sink->CopyTailForCrash(s.log_tail, sizeof(s.log_tail), kLogTailLines);
    if (tail_size) {
      DWORD written = 0;
      WriteFile(file, s.log_tail, static_cast<DWORD>(tail_size), &written, nullptr);
    } else {
      writer.Append("(log tail unavailable - its lock was held at crash time)\n");
      WriteAndFlush(file, writer);
    }
  } else {
    writer.Append("(no log capture sink registered)\n");
    WriteAndFlush(file, writer);
  }

  writer.Append("\n=== END OF CRASH REPORT ===\n");
  WriteAndFlush(file, writer);
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
}

// Guarded entry: any fault inside the crash path terminates immediately
// instead of looping.
void HandleFatalEvent(const char* kind, EXCEPTION_POINTERS* pointers, const char* extra) {
  auto& s = S();
  if (s.in_handler.exchange(true, std::memory_order_acq_rel)) {
    TerminateProcess(GetCurrentProcess(), 0xC0DEDEAD);
  }
  __try {
    WriteCrashReport(kind, pointers, extra);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // The handler itself died; the incremental flushes preserved whatever
    // was written before this point.
  }
  s.in_handler.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// The hooks.

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers) {
  HandleFatalEvent("unhandled exception", pointers, nullptr);
  return EXCEPTION_EXECUTE_HANDLER;
}

void AbortSignalHandler(int) {
  HandleFatalEvent("abort (assert_always / rex_assert_fail / CRT abort)", nullptr, nullptr);
  _exit(3);
}

void TerminateHandler() {
  HandleFatalEvent("std::terminate (unhandled C++ exception or noexcept violation)", nullptr,
                   nullptr);
  _exit(3);
}

void PurecallHandler() {
  HandleFatalEvent("pure virtual call", nullptr, nullptr);
  _exit(3);
}

void InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                             uintptr_t) {
  // The CRT passes details only in debug builds; the stacks tell the story.
  HandleFatalEvent("invalid CRT parameter", nullptr, nullptr);
  _exit(3);
}

void AtExitHook() {
  auto& s = S();
  if (s.orderly_shutdown.load(std::memory_order_acquire) ||
      s.report_written.load(std::memory_order_acquire)) {
    return;
  }
  // exit()/ExitProcess without the shutdown path: no fault context, but the
  // exiting thread's stack usually names the caller.
  HandleFatalEvent("unexpected exit (exit()/ExitProcess without the shutdown path)", nullptr,
                   nullptr);
}

}  // namespace

void Install(const InstallOptions& options) {
  auto& s = S();
  if (s.installed)
    return;

  auto copy_string = [](char* dst, size_t cap, const std::string& src) {
    size_t n = (std::min)(src.size(), cap - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = 0;
  };
  copy_string(s.app_name, sizeof(s.app_name), options.app_name);
  copy_string(s.build_title, sizeof(s.build_title), options.build_title);
  copy_string(s.build_commit, sizeof(s.build_commit), options.build_commit);
  copy_string(s.build_timestamp, sizeof(s.build_timestamp), options.build_timestamp);
  // A session id ties a crash file to the run that produced it. A caller that
  // already mints one passes it in; otherwise generate the same shape here so
  // the field is never empty.
  if (!options.session_id.empty()) {
    copy_string(s.session_id, sizeof(s.session_id), options.session_id);
  } else {
    SYSTEMTIME now;
    GetLocalTime(&now);
    _snprintf_s(s.session_id, sizeof(s.session_id), _TRUNCATE, "%04u%02u%02u-%02u%02u%02u-%lu",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                GetCurrentProcessId());
  }
  {
    auto wide = options.directory.wstring();
    size_t n = (std::min)(wide.size(), static_cast<size_t>(MAX_PATH - 1));
    std::memcpy(s.directory, wide.data(), n * sizeof(wchar_t));
    s.directory[n] = 0;
  }
  s.exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  {
    MODULEINFO module_info = {};
    if (K32GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(s.exe_base), &module_info,
                                sizeof(module_info))) {
      s.exe_size = module_info.SizeOfImage;
    }
  }

  SetUnhandledExceptionFilter(UnhandledFilter);
  std::signal(SIGABRT, AbortSignalHandler);
  std::set_terminate(TerminateHandler);
  _set_purecall_handler(PurecallHandler);
  _set_invalid_parameter_handler(InvalidParameterHandler);
  std::atexit(AtExitHook);
  PrepareCurrentThread();

  s.installed = true;
}

void SetGuestMemoryBounds(const void* host_base, uint64_t extent_bytes) {
  auto& s = S();
  s.guest_base.store(reinterpret_cast<uintptr_t>(host_base), std::memory_order_relaxed);
  s.guest_extent.store(extent_bytes, std::memory_order_relaxed);
}

void SetGuestFunctionTable(const PPCFuncMapping* mappings) {
  auto& s = S();
  if (!mappings)
    return;
  std::vector<HostFunctionEntry> table;
  for (const PPCFuncMapping* m = mappings; m->guest; ++m) {
    if (m->host) {
      table.push_back({reinterpret_cast<uintptr_t>(m->host),
                       static_cast<uint32_t>(m->guest)});
    }
  }
  std::sort(table.begin(), table.end(),
            [](const HostFunctionEntry& a, const HostFunctionEntry& b) { return a.host < b.host; });
  s.functions_by_host = std::move(table);
}

void SetLogTailSink(rex::LogCaptureSink* sink) {
  S().log_tail_sink = sink;
}

void SetContextProvider(ContextProvider provider) {
  S().context_provider = provider;
}

void PrepareCurrentThread() {
  // Reserve stack for the handler so a stack-overflow report can be written.
  ULONG stack_size = 64 * 1024;
  SetThreadStackGuarantee(&stack_size);
}

void NotifyOrderlyShutdown() {
  S().orderly_shutdown.store(true, std::memory_order_release);
}

}  // namespace rex::diag::crash

#else  // !REX_PLATFORM_WIN32

// POSIX: reports the guest side of a fault.
//
// ExceptionHandler owns SIGSEGV here (it drives the MMIO and write-watch
// paths), so rather than installing a competing signal handler this registers
// an unhandled-fault reporter with it. What it prints is the part a host stack
// trace cannot give: which guest function faulted, and the guest call chain
// that led there.
//
// Everything below runs on a thread that is about to die, so it takes no locks
// and probes every guest read before performing it.

#include <sys/uio.h>
#include <unistd.h>

#include <rex/exception_handler.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>

namespace rex::diag::crash {

namespace {

constexpr uint32_t kGuestVirtualBase = 0x100000000u;
constexpr int kMaxGuestFrames = 32;

std::atomic<uintptr_t> g_guest_base{0};
std::atomic<uint64_t> g_guest_extent{0};

// Reads one big-endian guest u32 without faulting.
//
// Checking the page protection first is not sufficient: guest memory is a shm
// mapping, so a page can be readable and still have no backing behind it, and
// touching one of those raises SIGBUS - for which no handler is installed, so
// it kills the process. That turns this reporter into the thing that destroys
// the crash it was invoked to describe.
//
// process_vm_readv performs the access in the kernel and reports EFAULT rather
// than signalling, which is exactly the semantics a crash-time reader needs.
bool SafeReadGuestU32(uint32_t guest_address, uint32_t* out) {
  uintptr_t base = g_guest_base.load(std::memory_order_relaxed);
  uint64_t extent = g_guest_extent.load(std::memory_order_relaxed);
  if (!base || uint64_t(guest_address) + 4ull > extent) {
    return false;
  }
  uint32_t value = 0;
  iovec local{&value, sizeof(value)};
  iovec remote{reinterpret_cast<void*>(base + guest_address), sizeof(value)};
  if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != ssize_t(sizeof(value))) {
    return false;
  }
  *out = __builtin_bswap32(value);
  return true;
}

void ReportGuestState(uint64_t fault_address, uint64_t host_pc, bool is_write) {
  auto* thread_state = rex::runtime::ThreadState::Get();
  PPCContext* ctx = thread_state ? thread_state->context() : nullptr;
  if (!ctx) {
    REXLOG_ERROR("[FAULT] no guest context on this thread - the fault is in host code");
    return;
  }

  if (fault_address >= kGuestVirtualBase) {
    REXLOG_ERROR("[FAULT] guest {} of {:#010x}; host pc {:#x}", is_write ? "write" : "read",
                 uint32_t(fault_address - kGuestVirtualBase), host_pc);
  }

  // The guest call chain. Each frame's saved lr is the return address into its
  // caller, so these are the guest functions that led to the fault.
  REXLOG_ERROR("[FAULT] guest call chain (lr {:08X}, sp {:08X}):", uint32_t(ctx->lr),
               uint32_t(ctx->r1.u32));
  uint32_t sp = ctx->r1.u32;
  for (int frame = 0; frame < kMaxGuestFrames; ++frame) {
    uint32_t caller_sp = 0;
    if (!SafeReadGuestU32(sp, &caller_sp) || caller_sp <= sp) {
      REXLOG_ERROR("[FAULT]   (chain ends at sp {:08X})", sp);
      break;
    }
    uint32_t saved_lr = 0;
    if (SafeReadGuestU32(caller_sp - 8, &saved_lr) && saved_lr) {
      REXLOG_ERROR("[FAULT]   sp {:08X}  return {:08X}", caller_sp, saved_lr);
    }
    sp = caller_sp;
  }

  // Registers, so the faulting pointer can be traced back to what produced it.
  const PPCRegister* gprs[32] = {
      &ctx->r0,  &ctx->r1,  &ctx->r2,  &ctx->r3,  &ctx->r4,  &ctx->r5,  &ctx->r6,  &ctx->r7,
      &ctx->r8,  &ctx->r9,  &ctx->r10, &ctx->r11, &ctx->r12, &ctx->r13, &ctx->r14, &ctx->r15,
      &ctx->r16, &ctx->r17, &ctx->r18, &ctx->r19, &ctx->r20, &ctx->r21, &ctx->r22, &ctx->r23,
      &ctx->r24, &ctx->r25, &ctx->r26, &ctx->r27, &ctx->r28, &ctx->r29, &ctx->r30, &ctx->r31};
  for (int i = 0; i < 32; i += 4) {
    REXLOG_ERROR("[FAULT]   r{:<2}={:08X}  r{:<2}={:08X}  r{:<2}={:08X}  r{:<2}={:08X}", i,
                 uint32_t(gprs[i]->u32), i + 1, uint32_t(gprs[i + 1]->u32), i + 2,
                 uint32_t(gprs[i + 2]->u32), i + 3, uint32_t(gprs[i + 3]->u32));
  }

  // Memory behind every register that looks like a live guest pointer. A fault
  // like "walked a list shorter than its own count" is only explicable from the
  // structures involved, and recovering those one address at a time over gdb
  // costs a restart per question.
  REXLOG_ERROR("[FAULT] memory behind pointer-like registers:");
  uint32_t already_dumped[32] = {};
  int dumped_count = 0;
  for (int i = 0; i < 32; ++i) {
    const uint32_t value = gprs[i]->u32;
    // Guest heap/data lives well above the null guard; anything tiny is a
    // count or a flag, not an address.
    if (value < 0x10000u) {
      continue;
    }
    uint32_t probe = 0;
    if (!SafeReadGuestU32(value, &probe)) {
      continue;
    }
    // Registers frequently alias (a struct and a field 4 bytes into it); one
    // dump per 32-byte neighbourhood is enough.
    bool seen = false;
    for (int j = 0; j < dumped_count; ++j) {
      if ((already_dumped[j] & ~31u) == (value & ~31u)) {
        seen = true;
        break;
      }
    }
    if (seen) {
      continue;
    }
    already_dumped[dumped_count++] = value;

    uint32_t words[8] = {};
    int read = 0;
    for (; read < 8; ++read) {
      if (!SafeReadGuestU32(value + uint32_t(read * 4), &words[read])) {
        break;
      }
    }
    REXLOG_ERROR("[FAULT]   r{:<2} -> [{:08X}] {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                 i, value, words[0], words[1], words[2], words[3], words[4], words[5], words[6],
                 words[7]);
  }
}

}  // namespace

void Install(const InstallOptions&) {
  rex::arch::ExceptionHandler::SetUnhandledReporter(&ReportGuestState);
}

void SetGuestMemoryBounds(const void* host_base, uint64_t extent_bytes) {
  g_guest_base.store(reinterpret_cast<uintptr_t>(host_base), std::memory_order_relaxed);
  g_guest_extent.store(extent_bytes, std::memory_order_relaxed);
}

void SetGuestFunctionTable(const PPCFuncMapping*) {}
void SetLogTailSink(rex::LogCaptureSink*) {}
void SetContextProvider(ContextProvider) {}
void PrepareCurrentThread() {}
void NotifyOrderlyShutdown() {}

}  // namespace rex::diag::crash

#endif  // REX_PLATFORM_WIN32
