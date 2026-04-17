/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/thread.h>
#include <rex/system/thread_state.h>

namespace rex::runtime {

thread_local Thread* Thread::current_thread_ = nullptr;

Thread::Thread() {}
Thread::~Thread() {}

bool Thread::IsInThread() {
  return current_thread_ != nullptr;
}

Thread* Thread::GetCurrentThread() {
  return current_thread_;
}
uint32_t Thread::GetCurrentThreadId() {
  return Thread::GetCurrentThread()->thread_state()->thread_id();
}

}  // namespace rex::runtime