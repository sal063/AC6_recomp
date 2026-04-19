#pragma once

#include "ac6_backend_capture_bridge.h"

namespace ac6::backend {

SignatureClass ClassifySignature(const RenderEventSignature& signature);
const char* ToString(SignatureClass signature_class);

}  // namespace ac6::backend
