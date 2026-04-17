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

// Disable warnings about unused parameters for kernel functions
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <rex/audio/audio_system.h>
#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/ppc/function.h>
#include <rex/ppc/types.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

REXCVAR_DECLARE(bool, audio_trace_telemetry);
REXCVAR_DECLARE(bool, audio_trace_render_driver_verbose);
REXCVAR_DECLARE(bool, ac6_audio_deep_trace);

namespace rex::kernel::xboxkrnl {
using namespace rex::system;

namespace {

constexpr uint32_t kRenderDriverTagMask = 0xFFFF0000;
constexpr uint32_t kRenderDriverTagValue = 0x41550000;

size_t ResolveRenderDriverIndex(const ppc_pvoid_t driver_ptr) {
  return driver_ptr.guest_address() & 0x0000FFFF;
}

audio::AudioSystem* GetAudioSystem() {
  return static_cast<audio::AudioSystem*>(REX_KERNEL_STATE()->emulator()->audio_system());
}

bool IsDeepTraceEnabled() {
  return REXCVAR_GET(ac6_audio_deep_trace);
}

}  // namespace

ppc_u32_result_t XAudioGetSpeakerConfig_entry(ppc_pu32_t config_ptr) {
  *config_ptr = 0x00010001;
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioGetVoiceCategoryVolumeChangeMask_entry(ppc_pvoid_t driver_ptr,
                                                              ppc_pu32_t out_ptr) {
  *out_ptr = 0;
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioGetVoiceCategoryVolume_entry(ppc_u32_t unk, ppc_pf32_t out_ptr) {
  *out_ptr = 1.0f;
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioEnableDucker_entry(ppc_u32_t unk) {
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioRegisterRenderDriverClient_entry(ppc_pu32_t callback_ptr,
                                                        ppc_pu32_t driver_ptr) {
  REXKRNL_DEBUG("XAudioRegisterRenderDriverClient called callback_ptr={:08X} driver_ptr={:08X}",
                callback_ptr.guest_address(), driver_ptr.guest_address());
  if (!callback_ptr) {
    return X_E_INVALIDARG;
  }

  const uint32_t callback = callback_ptr[0];
  const uint32_t callback_arg = callback_ptr[1];
  if (!callback) {
    return X_E_INVALIDARG;
  }

  size_t index = 0;
  const auto result = GetAudioSystem()->RegisterClient(callback, callback_arg, &index);
  if (XFAILED(result)) {
    REXKRNL_WARN(
        "XAudioRegisterRenderDriverClient failed callback={:08X} callback_arg={:08X} "
        "status={:08X}",
        callback, callback_arg, static_cast<uint32_t>(result));
    return result;
  }

  const uint32_t assigned_driver =
      kRenderDriverTagValue | (static_cast<uint32_t>(index) & 0x0000FFFF);
  *driver_ptr = assigned_driver;
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    REXKRNL_DEBUG(
        "XAudioRegisterRenderDriverClient result callback={:08X} callback_arg={:08X} "
        "index={} driver={:08X}",
        callback, callback_arg, index, assigned_driver);
  }
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioUnregisterRenderDriverClient_entry(ppc_pvoid_t driver_ptr) {
  if ((driver_ptr.guest_address() & kRenderDriverTagMask) != kRenderDriverTagValue) {
    return X_E_INVALIDARG;
  }

  const size_t index = ResolveRenderDriverIndex(driver_ptr);
  auto* audio_system = GetAudioSystem();
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    REXKRNL_DEBUG("XAudioUnregisterRenderDriverClient driver={:08X} index={}",
                  driver_ptr.guest_address(), index);
  }
  if (REXCVAR_GET(audio_trace_telemetry) || IsDeepTraceEnabled()) {
    const auto telemetry = audio_system->GetClientTelemetry(index);
    REXKRNL_DEBUG(
        "XAudioUnregisterRenderDriverClient telemetry: driver={:08X} submitted={} consumed={} "
        "underruns={} silence_injections={} queued_depth={} peak={}",
        driver_ptr.guest_address(), telemetry.submitted_frames, telemetry.consumed_frames,
        telemetry.underrun_count, telemetry.silence_injections, telemetry.queued_depth,
        telemetry.peak_queued_depth);
  }
  audio_system->UnregisterClient(index);
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioSubmitRenderDriverFrame_entry(ppc_pvoid_t driver_ptr,
                                                     ppc_pvoid_t samples_ptr) {
  if ((driver_ptr.guest_address() & kRenderDriverTagMask) != kRenderDriverTagValue) {
    return X_E_INVALIDARG;
  }

  const size_t index = ResolveRenderDriverIndex(driver_ptr);
  auto* audio_system = GetAudioSystem();

  static uint32_t submit_krnl_count = 0;
  if (submit_krnl_count < 10) {
    REXKRNL_DEBUG("XAudioSubmitRenderDriverFrame: driver={:08X} samples={:08X}",
                  driver_ptr.guest_address(), samples_ptr.guest_address());
    submit_krnl_count++;
  }

  if (samples_ptr.guest_address() == 0) {
    audio_system->SubmitSilenceFrame(index);
  } else {
    audio_system->SubmitFrame(index, samples_ptr.guest_address());
  }
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    const auto telemetry = audio_system->GetClientTelemetry(index);
    if (telemetry.submitted_frames <= 24 || (telemetry.submitted_frames % 60) == 0 ||
        telemetry.queued_depth <= 1 || telemetry.underrun_count != 0) {
      REXKRNL_DEBUG(
          "XAudioSubmitRenderDriverFrame verbose driver={:08X} index={} samples={:08X} "
          "submitted={} consumed={} queued_depth={} peak={} underruns={} silence_injections={}",
          driver_ptr.guest_address(), index, samples_ptr.guest_address(),
          telemetry.submitted_frames, telemetry.consumed_frames, telemetry.queued_depth,
          telemetry.peak_queued_depth, telemetry.underrun_count,
          telemetry.silence_injections);
    }
  }
  if (REXCVAR_GET(audio_trace_telemetry) || IsDeepTraceEnabled()) {
    const auto telemetry = audio_system->GetClientTelemetry(index);
    if (telemetry.submitted_frames <= 12 || (telemetry.submitted_frames % 120) == 0) {
      REXKRNL_DEBUG(
          "XAudioSubmitRenderDriverFrame telemetry: driver={:08X} submitted={} consumed={} "
          "underruns={} queued_depth={} peak={}",
          driver_ptr.guest_address(), telemetry.submitted_frames, telemetry.consumed_frames,
          telemetry.underrun_count, telemetry.queued_depth, telemetry.peak_queued_depth);
    }
  }
  return X_ERROR_SUCCESS;
}

ppc_u32_result_t XAudioGetRenderDriverTic_entry(ppc_pvoid_t driver_ptr) {
  if ((driver_ptr.guest_address() & kRenderDriverTagMask) != kRenderDriverTagValue) {
    return 0;
  }

  const size_t index = ResolveRenderDriverIndex(driver_ptr);
  auto* audio_system = GetAudioSystem();
  const uint32_t internal_tic = audio_system->GetClientRenderDriverTic(index);
  const auto timing = audio_system->GetClientTimingSnapshot(index);
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    const auto telemetry = audio_system->GetClientTelemetry(index);
    if (telemetry.consumed_frames <= 24 || (telemetry.consumed_frames % 120) == 0 ||
        timing.render_driver_tic <= (audio::kRenderDriverTicSamplesPerFrame * 240)) {
      REXKRNL_DEBUG(
          "XAudioGetRenderDriverTic driver={:08X} index={} guest_tic=0 internal_tic={} "
          "submitted={} consumed={} queued_depth={} underruns={} callbacks={} "
          "clock_samples={} clock_frames={} callback_floor_tic={} host_elapsed_tic={}",
          driver_ptr.guest_address(), index, internal_tic, telemetry.submitted_frames,
          telemetry.consumed_frames, telemetry.queued_depth, telemetry.underrun_count,
          telemetry.callback_dispatch_count, timing.consumed_samples, timing.consumed_frames,
          timing.callback_floor_tic, timing.host_elapsed_tic);
    }
  }
  return internal_tic;
}

ppc_u32_result_t XAudioGetUnderrunCount_entry(ppc_pvoid_t driver_ptr) {
  if ((driver_ptr.guest_address() & kRenderDriverTagMask) != kRenderDriverTagValue) {
    return 0;
  }

  const size_t index = ResolveRenderDriverIndex(driver_ptr);
  auto* audio_system = GetAudioSystem();
  auto telemetry = audio_system->GetClientTelemetry(index);
  if (telemetry.underrun_count == 0 && telemetry.submitted_frames == 0 &&
      telemetry.callback_dispatch_count != 0) {
    telemetry.underrun_count = 1;
  }
  if (REXCVAR_GET(audio_trace_render_driver_verbose) || IsDeepTraceEnabled()) {
    if (telemetry.underrun_count != 0 || telemetry.consumed_frames <= 24 ||
        (telemetry.consumed_frames % 120) == 0) {
      REXKRNL_DEBUG(
          "XAudioGetUnderrunCount driver={:08X} index={} guest_underruns=0 internal_underruns={} "
          "submitted={} consumed={} queued_depth={} callbacks={}",
          driver_ptr.guest_address(), index, telemetry.underrun_count,
          telemetry.submitted_frames, telemetry.consumed_frames, telemetry.queued_depth,
          telemetry.callback_dispatch_count);
    }
  }
  return telemetry.underrun_count;
}

}  // namespace rex::kernel::xboxkrnl

XBOXKRNL_EXPORT(__imp__XAudioGetSpeakerConfig, rex::kernel::xboxkrnl::XAudioGetSpeakerConfig_entry)
XBOXKRNL_EXPORT(__imp__XAudioGetVoiceCategoryVolumeChangeMask,
                rex::kernel::xboxkrnl::XAudioGetVoiceCategoryVolumeChangeMask_entry)
XBOXKRNL_EXPORT(__imp__XAudioGetVoiceCategoryVolume,
                rex::kernel::xboxkrnl::XAudioGetVoiceCategoryVolume_entry)
XBOXKRNL_EXPORT(__imp__XAudioEnableDucker, rex::kernel::xboxkrnl::XAudioEnableDucker_entry)
XBOXKRNL_EXPORT(__imp__XAudioRegisterRenderDriverClient,
                rex::kernel::xboxkrnl::XAudioRegisterRenderDriverClient_entry)
XBOXKRNL_EXPORT(__imp__XAudioUnregisterRenderDriverClient,
                rex::kernel::xboxkrnl::XAudioUnregisterRenderDriverClient_entry)
XBOXKRNL_EXPORT(__imp__XAudioSubmitRenderDriverFrame,
                rex::kernel::xboxkrnl::XAudioSubmitRenderDriverFrame_entry)

XBOXKRNL_EXPORT_STUB(__imp__XAudioRenderDriverInitialize);
XBOXKRNL_EXPORT_STUB(__imp__XAudioRenderDriverLock);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetVoiceCategoryVolume);
XBOXKRNL_EXPORT_STUB(__imp__XAudioBeginDigitalBypassMode);
XBOXKRNL_EXPORT_STUB(__imp__XAudioEndDigitalBypassMode);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSubmitDigitalPacket);
XBOXKRNL_EXPORT_STUB(__imp__XAudioQueryDriverPerformance);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetRenderDriverThread);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetSpeakerConfig);
XBOXKRNL_EXPORT_STUB(__imp__XAudioOverrideSpeakerConfig);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSuspendRenderDriverClients);
XBOXKRNL_EXPORT_STUB(__imp__XAudioRegisterRenderDriverMECClient);
XBOXKRNL_EXPORT_STUB(__imp__XAudioUnregisterRenderDriverMECClient);
XBOXKRNL_EXPORT_STUB(__imp__XAudioCaptureRenderDriverFrame);
XBOXKRNL_EXPORT(__imp__XAudioGetRenderDriverTic,
                rex::kernel::xboxkrnl::XAudioGetRenderDriverTic_entry)
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetDuckerLevel);
XBOXKRNL_EXPORT_STUB(__imp__XAudioIsDuckerEnabled);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetDuckerLevel);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetDuckerThreshold);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetDuckerThreshold);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetDuckerAttackTime);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetDuckerAttackTime);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetDuckerReleaseTime);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetDuckerReleaseTime);
XBOXKRNL_EXPORT_STUB(__imp__XAudioGetDuckerHoldTime);
XBOXKRNL_EXPORT_STUB(__imp__XAudioSetDuckerHoldTime);
XBOXKRNL_EXPORT(__imp__XAudioGetUnderrunCount,
                rex::kernel::xboxkrnl::XAudioGetUnderrunCount_entry)
