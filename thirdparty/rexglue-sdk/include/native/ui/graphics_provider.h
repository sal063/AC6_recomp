#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>

#include <native/ui/immediate_drawer.h>
#include <native/ui/presenter.h>

namespace rex {
namespace ui {

class Window;

class GraphicsProvider {
 public:
  enum class GpuVendorID {
    kAMD = 0x1002,
    kApple = 0x106B,
    kArm = 0x13B5,
    kImagination = 0x1010,
    kIntel = 0x8086,
    kMicrosoft = 0x1414,
    kNvidia = 0x10DE,
    kQualcomm = 0x5143,
  };

  GraphicsProvider(const GraphicsProvider&) = delete;
  GraphicsProvider& operator=(const GraphicsProvider&) = delete;
  GraphicsProvider(GraphicsProvider&&) = delete;
  GraphicsProvider& operator=(GraphicsProvider&&) = delete;

  virtual ~GraphicsProvider() = default;

  virtual std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) = 0;

  virtual std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() = 0;

 protected:
  GraphicsProvider() = default;
};

}  // namespace ui
}  // namespace rex
