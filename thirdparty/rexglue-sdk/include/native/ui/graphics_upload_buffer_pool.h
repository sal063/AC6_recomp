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

#include <cstddef>
#include <cstdint>

#include <rex/literals.h>

namespace rex {
namespace ui {

using namespace rex::literals;

class GraphicsUploadBufferPool {
 public:
  static constexpr size_t kDefaultPageSize = 2_MiB;

  virtual ~GraphicsUploadBufferPool();

  void Reclaim(uint64_t completed_submission_index);
  void ChangeSubmissionTimeline();
  void ClearCache();
  void FlushWrites();

 protected:
  struct Page {
    virtual ~Page();
    uint64_t last_submission_index_;
    Page* next_;
  };

  GraphicsUploadBufferPool(size_t page_size) : page_size_(page_size) {}

  Page* Request(uint64_t submission_index, size_t size, size_t alignment, size_t& offset_out);
  Page* RequestPartial(uint64_t submission_index, size_t size, size_t alignment, size_t& offset_out,
                       size_t& size_out);

  virtual Page* CreatePageImplementation() = 0;
  virtual void FlushPageWrites(Page* page, size_t offset, size_t size);

  size_t page_size_;
  Page* writable_first_ = nullptr;
  Page* writable_last_ = nullptr;
  Page* submitted_first_ = nullptr;
  Page* submitted_last_ = nullptr;

  size_t current_page_used_ = 0;
  size_t current_page_flushed_ = 0;
};

}  // namespace ui
}  // namespace rex
