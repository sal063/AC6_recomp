/**
 * ReXGlue runtime - AC6 Recompilation project
 * Copyright (c) 2026 Tom Clay. All rights reserved.
 */

#include <rex/system/kernel_state.h>
#include <rex/system/xam/app_manager.h>

namespace rex {
namespace system {
namespace xam {

App::App(KernelState* kernel_state, uint32_t app_id)
    : kernel_state_(kernel_state), memory_(kernel_state->memory()), app_id_(app_id) {}

void AppManager::RegisterApp(std::unique_ptr<App> app) {
  assert_zero(app_lookup_.count(app->app_id()));
  app_lookup_.insert({app->app_id(), app.get()});
  apps_.push_back(std::move(app));
}

X_HRESULT AppManager::DispatchMessageSync(uint32_t app_id, uint32_t message, uint32_t buffer_ptr,
                                          uint32_t buffer_length) {
  const auto& it = app_lookup_.find(app_id);
  if (it == app_lookup_.end()) {
    return X_E_NOTFOUND;
  }
  return it->second->DispatchMessageSync(message, buffer_ptr, buffer_length);
}

X_HRESULT AppManager::DispatchMessageAsync(uint32_t app_id, uint32_t message, uint32_t buffer_ptr,
                                           uint32_t buffer_length) {
  const auto& it = app_lookup_.find(app_id);
  if (it == app_lookup_.end()) {
    return X_E_NOTFOUND;
  }
  return it->second->DispatchMessageSync(message, buffer_ptr, buffer_length);
}

}  // namespace xam
}  // namespace system
}  // namespace rex
