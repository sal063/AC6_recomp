#include "ac6_backend_pass_classifier.h"

namespace ac6::backend {

SignatureClass ClassifySignature(const RenderEventSignature& signature) {
  if (signature.ui_like) {
    return SignatureClass::kUiComposite;
  }
  if (signature.particle_like && signature.additive_like &&
      signature.viewport_width && signature.viewport_height &&
      signature.viewport_width >= signature.viewport_height * 2) {
    return SignatureClass::kMissileTrails;
  }
  if (signature.half_res_like && signature.post_process_like &&
      signature.sampler_count >= 4 && signature.fetch_constant_count >= 2) {
    return SignatureClass::kClouds;
  }
  if (signature.half_res_like && signature.particle_like && signature.additive_like) {
    return SignatureClass::kExplosions;
  }
  if (signature.half_res_like && signature.post_process_like) {
    return SignatureClass::kSmoke;
  }
  if (signature.particle_like) {
    return SignatureClass::kParticles;
  }
  if (signature.post_process_like) {
    return SignatureClass::kPostProcess;
  }
  if (signature.has_depth_stencil) {
    return SignatureClass::kScene;
  }
  return SignatureClass::kUnknown;
}

const char* ToString(const SignatureClass signature_class) {
  switch (signature_class) {
    case SignatureClass::kScene:
      return "scene";
    case SignatureClass::kPostProcess:
      return "post_process";
    case SignatureClass::kUiComposite:
      return "ui_composite";
    case SignatureClass::kParticles:
      return "particles";
    case SignatureClass::kClouds:
      return "clouds";
    case SignatureClass::kSmoke:
      return "smoke";
    case SignatureClass::kExplosions:
      return "explosions";
    case SignatureClass::kMissileTrails:
      return "missile_trails";
    case SignatureClass::kUnknown:
    default:
      return "unknown";
  }
}

}  // namespace ac6::backend
