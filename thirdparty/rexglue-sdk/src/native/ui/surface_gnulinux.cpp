// Native UI runtime - Xcb surface implementation
// Part of the AC6 Recompilation native presenter/window layer

#include <cstdlib>

#include <native/ui/surface_gnulinux.h>

namespace rex {
namespace ui {

bool XcbWindowSurface::GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const {
  xcb_get_geometry_reply_t* reply =
      xcb_get_geometry_reply(connection_, xcb_get_geometry(connection_, window_), nullptr);
  if (!reply) {
    return false;
  }
  width_out = reply->width;
  height_out = reply->height;
  std::free(reply);
  return true;
}

}  // namespace ui
}  // namespace rex
