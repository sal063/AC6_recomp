// Native UI runtime - Win32 surface implementation
// Part of the AC6 Recompilation native presenter/window layer

#include <native/ui/surface_win.h>

namespace rex {
namespace ui {

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_GAMES)
bool Win32HwndSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  RECT client_rect;
  if (!GetClientRect(hwnd(), &client_rect)) {
    return false;
  }
  // GetClientRect returns a rectangle with 0 origin.
  width_out = uint32_t(client_rect.right);
  height_out = uint32_t(client_rect.bottom);
  return true;
}
#endif

}  // namespace ui
}  // namespace rex
