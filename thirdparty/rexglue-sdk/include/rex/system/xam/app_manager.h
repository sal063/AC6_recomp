/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include <rex/memory.h>
#include <rex/system/xtypes.h>

namespace rex {
class Memory;
namespace system {
class KernelState;
}  // namespace system
}  // namespace rex

namespace rex {
namespace system {
namespace xam {

class App {
 public:
  uint32_t app_id() const { return app_id_; }

  virtual X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                        uint32_t buffer_length) = 0;

  virtual ~App() = default;

 protected:
  App(KernelState* kernel_state, uint32_t app_id);

  KernelState* kernel_state_;
  memory::Memory* memory_;
  uint32_t app_id_;
};

class AppManager {
 public:
  void RegisterApp(std::unique_ptr<App> app);

  X_HRESULT DispatchMessageSync(uint32_t app_id, uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length);
  X_HRESULT DispatchMessageAsync(uint32_t app_id, uint32_t message, uint32_t buffer_ptr,
                                 uint32_t buffer_length);

 private:
  std::vector<std::unique_ptr<App>> apps_;
  std::unordered_map<uint32_t, App*> app_lookup_;
};

}  // namespace xam
}  // namespace system
}  // namespace rex
