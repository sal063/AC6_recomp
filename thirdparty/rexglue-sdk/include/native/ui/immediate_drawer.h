#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>

#include <native/ui/presenter.h>

namespace rex {
namespace ui {

enum class ImmediateTextureFilter {
  kNearest,
  kLinear,
};

class ImmediateTexture {
 public:
  virtual ~ImmediateTexture() = default;

  uint32_t width;
  uint32_t height;

 protected:
  ImmediateTexture(uint32_t width, uint32_t height) : width(width), height(height) {}
};

enum class ImmediatePrimitiveType {
  kLines,
  kTriangles,
};

struct ImmediateVertex {
  float x, y;
  float u, v;
  uint32_t color;
};

struct ImmediateDrawBatch {
  const ImmediateVertex* vertices = nullptr;
  int vertex_count = 0;
  const uint16_t* indices = nullptr;
  int index_count = 0;
};

struct ImmediateDraw {
  ImmediatePrimitiveType primitive_type = ImmediatePrimitiveType::kTriangles;
  int count = 0;
  int index_offset = 0;
  int base_vertex = 0;

  ImmediateTexture* texture = nullptr;

  bool scissor = false;
  float scissor_left = 0.0f;
  float scissor_top = 0.0f;
  float scissor_right = 0.0f;
  float scissor_bottom = 0.0f;
};

class ImmediateDrawer {
 public:
  ImmediateDrawer(const ImmediateDrawer& immediate_drawer) = delete;
  ImmediateDrawer& operator=(const ImmediateDrawer& immediate_drawer) = delete;

  virtual ~ImmediateDrawer() = default;

  void SetPresenter(Presenter* new_presenter);

  virtual std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                          ImmediateTextureFilter filter,
                                                          bool is_repeated,
                                                          const uint8_t* data) = 0;

  virtual void Begin(UIDrawContext& ui_draw_context, float coordinate_space_width,
                     float coordinate_space_height);
  virtual void BeginDrawBatch(const ImmediateDrawBatch& batch) = 0;
  virtual void Draw(const ImmediateDraw& draw) = 0;
  virtual void EndDrawBatch() = 0;
  virtual void End();

 protected:
  ImmediateDrawer() = default;

  Presenter* presenter() const { return presenter_; }
  virtual void OnLeavePresenter() {}
  virtual void OnEnterPresenter() {}

  UIDrawContext* ui_draw_context() const { return ui_draw_context_; }
  float coordinate_space_width() const { return coordinate_space_width_; }
  float coordinate_space_height() const { return coordinate_space_height_; }

  bool ScissorToRenderTarget(const ImmediateDraw& immediate_draw, uint32_t& out_left,
                             uint32_t& out_top, uint32_t& out_width, uint32_t& out_height);

 private:
  Presenter* presenter_ = nullptr;

  UIDrawContext* ui_draw_context_ = nullptr;
  float coordinate_space_width_;
  float coordinate_space_height_;
};

}  // namespace ui
}  // namespace rex
