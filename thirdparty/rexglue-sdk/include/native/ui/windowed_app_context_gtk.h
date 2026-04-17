// Native UI runtime - GTK UI thread context
// Part of the AC6 Recompilation native presenter/window layer

#pragma once

#include <mutex>

#include <native/ui/windowed_app_context.h>

#include <glib.h>

namespace rex {
namespace ui {

class GTKWindowedAppContext final : public WindowedAppContext {
 public:
  GTKWindowedAppContext() = default;
  ~GTKWindowedAppContext();

  void NotifyUILoopOfPendingFunctions() override;

  void PlatformQuitFromUIThread() override;

  void RunMainGTKLoop();

 private:
  static gboolean PendingFunctionsSourceFunc(gpointer data);

  static gboolean QuitSourceFunc(gpointer data);

  std::mutex pending_functions_idle_pending_mutex_;
  guint pending_functions_idle_pending_ = 0;

  guint quit_idle_pending_ = 0;
};

}  // namespace ui
}  // namespace rex
