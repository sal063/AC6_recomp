#pragma once

#include <memory>

#include <rex/ui/imgui_dialog.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace ac6::graphics {

class NativeGraphicsStatusDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit NativeGraphicsStatusDialog(rex::ui::ImGuiDrawer* imgui_drawer);
  ~NativeGraphicsStatusDialog();

  void Show() { visible_ = true; }
  void ToggleVisible() { visible_ = !visible_; }
  bool IsVisible() const { return visible_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool visible_ = false;
};

}  // namespace ac6::graphics
