// Native UI runtime - windowed app implementation
// Part of the AC6 Recompilation native presenter/window layer

#include <string>
#include <unordered_map>

#include <native/ui/windowed_app.h>

namespace rex {
namespace ui {

#if XE_UI_WINDOWED_APPS_IN_LIBRARY
// A zero-initialized pointer to remove dependence on the initialization order
// of the map relatively to the app creator proxies.
std::unordered_map<std::string, WindowedApp::Creator>* WindowedApp::creators_;
#endif  // XE_UI_WINDOWED_APPS_IN_LIBRARY

}  // namespace ui
}  // namespace rex
