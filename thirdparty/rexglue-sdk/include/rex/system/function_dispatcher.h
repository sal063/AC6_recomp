/**
 * @file        system/function_dispatcher.h
 * @brief       Guest function dispatch coordinator for recompiled code
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Derived from Xenia's runtime::Processor (Ben Vanik, 2020).
 *              Stripped of emulation-era dead code and renamed to reflect its
 *              role as a function dispatch table rather than a CPU emulator.
 */

#pragma once

#include <unordered_map>

#include <rex/memory.h>
#include <rex/memory/mapped_memory.h>
#include <rex/ppc/context.h>
#include <rex/system/export_resolver.h>
#include <rex/system/thread_state.h>
#include <rex/thread/mutex.h>

namespace rex::runtime {

// Forward declarations
class ExportResolver;
class ThreadState;

class FunctionDispatcher {
 public:
  FunctionDispatcher(memory::Memory* memory, ExportResolver* export_resolver);
  ~FunctionDispatcher();

  memory::Memory* memory() const { return memory_; }
  ExportResolver* export_resolver() const { return export_resolver_; }

  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[], size_t arg_count);
  uint64_t ExecuteInterrupt(ThreadState* thread_state, uint32_t address, uint64_t args[],
                            size_t arg_count);

  // rexglue function table management
  bool InitializeFunctionTable(uint32_t code_base, uint32_t code_size, uint32_t image_base,
                               uint32_t image_size);
  void SetFunction(uint32_t guest_address, ::PPCFunc* func);
  ::PPCFunc* GetFunction(uint32_t guest_address);
  bool HasFunctionTable() const { return function_table_initialized_; }
  uint32_t AllocateThunk(::PPCFunc* func);

 private:
  bool Execute(ThreadState* thread_state, uint32_t address);

  memory::Memory* memory_ = nullptr;
  ExportResolver* export_resolver_ = nullptr;

  rex::thread::global_critical_region global_critical_region_;

  // rexglue function table
  std::unordered_map<uint32_t, ::PPCFunc*> function_table_;
  uint32_t code_base_ = 0;
  uint32_t code_size_ = 0;
  uint32_t image_base_ = 0;
  uint32_t image_size_ = 0;
  bool function_table_initialized_ = false;

  // Runtime thunk allocation (for XexGetProcedureAddress)
  uint32_t next_thunk_address_ = 0;
  uint32_t thunk_limit_ = 0;
};

}  // namespace rex::runtime
