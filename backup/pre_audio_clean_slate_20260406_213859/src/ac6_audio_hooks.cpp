#include "generated/ac6recomp_config.h"
#include "ac6_audio_policy.h"

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc.h>

PPC_EXTERN_IMPORT(__savegprlr_28);
PPC_EXTERN_IMPORT(__savegprlr_29);
PPC_EXTERN_IMPORT(__restgprlr_28);
PPC_EXTERN_IMPORT(__restgprlr_29);
PPC_EXTERN_IMPORT(__imp__XAudioRegisterRenderDriverClient);
PPC_EXTERN_IMPORT(__imp__XAudioSubmitRenderDriverFrame);
PPC_EXTERN_IMPORT(__imp__XAudioUnregisterRenderDriverClient);
PPC_EXTERN_FUNC(rex_sub_823A8648);
PPC_EXTERN_FUNC(__imp__rex_sub_823A2C30);
PPC_EXTERN_FUNC(__imp__rex_sub_823A8910);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD0D8);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD1C0);
PPC_EXTERN_FUNC(__imp__rex_sub_823AD9C0);
PPC_EXTERN_FUNC(__imp__rex_sub_823AE3B8);
PPC_EXTERN_FUNC(__imp__rex_sub_823AED08);
PPC_EXTERN_FUNC(__imp__rex_sub_823AED88);
PPC_EXTERN_FUNC(__imp__rex_sub_823AEE50);
PPC_EXTERN_FUNC(__imp__rex_sub_823B3BA8);
PPC_EXTERN_FUNC(__imp__rex_sub_823B7C80);
PPC_EXTERN_FUNC(__imp__rex_sub_823B7EC8);
PPC_EXTERN_FUNC(__imp__rex_sub_823B9158);
PPC_EXTERN_FUNC(__imp__rex_sub_823B9700);
PPC_EXTERN_FUNC(__imp__rex_sub_823B9F10);
PPC_EXTERN_FUNC(__imp__rex_sub_823B0DB8);
PPC_EXTERN_FUNC(__imp__rex_sub_823B0DF0);
PPC_EXTERN_IMPORT(__imp__RtlEnterCriticalSection);
PPC_EXTERN_IMPORT(__imp__RtlLeaveCriticalSection);

namespace {

std::atomic<uint64_t> g_movie_audio_producer_trace_count{0};
std::atomic<uint32_t> g_movie_audio_last_head_zero_state{1};
std::atomic<uint32_t> g_last_movie_singleton_ptr{0};
std::atomic<uint64_t> g_movie_audio_gate_indirect_entry_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_indirect_result_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_driver_entry_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_ready_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_arm_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_wait_trace_count{0};
std::atomic<uint64_t> g_movie_audio_gate_poll_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_dispatch_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_prepare_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_finalize_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_post_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_flush_trace_count{0};
std::atomic<uint64_t> g_movie_audio_async_vfunc_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_publish_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_step84_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_step9f10_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_step1_trace_count{0};
std::atomic<uint64_t> g_movie_audio_worker_step2_trace_count{0};

uint32_t LoadGuestWord(uint8_t* base, const uint32_t ptr, const uint32_t byte_offset = 0) {
    return ptr != 0 ? PPC_LOAD_U32(ptr + byte_offset) : 0;
}

uint16_t LoadGuestHalf(uint8_t* base, const uint32_t ptr, const uint32_t byte_offset = 0) {
    return ptr != 0 ? PPC_LOAD_U16(ptr + byte_offset) : 0;
}

}  // namespace

PPC_FUNC_IMPL(rex_sub_823A6620) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    __savegprlr_29(ctx, base);
    ea = -128 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r29.u64 = ctx.r4.u64;
    ctx.r3.s64 = 0;
    ctx.r30.s64 = ctx.r31.s64 + 24;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);
    ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A6660;
    }

    const uint32_t old_driver_ptr = ctx.r11.u32;
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        ac6::audio_policy::IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6620 unregister-existing owner={:08X} old_driver={:08X}",
            ctx.r31.u32,
            old_driver_ptr);
    }
    ctx.r3.u64 = ctx.r11.u64;
    ctx.lr = 0x823A6650;
    __imp__XAudioUnregisterRenderDriverClient(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A6680;
    }

    ac6::audio_policy::OnMovieAudioClientUnregistered(ctx.r31.u32,
                                                      old_driver_ptr);
    ctx.r11.s64 = 0;
    PPC_STORE_U32(ctx.r30.u32 + 0, ctx.r11.u32);

loc_823A6660:
    ctx.cr6.compare<uint32_t>(ctx.r29.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A6680;
    }

    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 12);
    ctx.r4.u64 = ctx.r30.u64;
    ctx.r3.s64 = ctx.r1.s64 + 80;
    PPC_STORE_U32(ctx.r1.u32 + 80, ctx.r29.u32);
    PPC_STORE_U32(ctx.r1.u32 + 84, ctx.r11.u32);
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        ac6::audio_policy::IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6620 register owner={:08X} callback={:08X} callback_arg={:08X}",
            ctx.r31.u32,
            ctx.r29.u32,
            ctx.r11.u32);
    }
    ctx.lr = 0x823A6680;
    __imp__XAudioRegisterRenderDriverClient(ctx, base);
    if (ctx.r3.s32 >= 0) {
        const uint32_t driver_ptr = PPC_LOAD_U32(ctx.r30.u32 + 0);
        if (REXCVAR_GET(ac6_movie_audio_trace) ||
            ac6::audio_policy::IsDeepTraceEnabled()) {
            REXAPU_DEBUG(
                "AC6 hook rex_sub_823A6620 register-result owner={:08X} callback={:08X} "
                "driver={:08X} status={:08X}",
                ctx.r31.u32,
                ctx.r29.u32,
                driver_ptr,
                static_cast<uint32_t>(ctx.r3.u32));
        }
        ac6::audio_policy::OnMovieAudioClientRegistered(
            ctx.r31.u32, ctx.r29.u32, PPC_LOAD_U32(ctx.r31.u32 + 12), driver_ptr);
    }

loc_823A6680:
    ctx.r1.s64 = ctx.r1.s64 + 128;
    __restgprlr_29(ctx, base);
    return;
}

PPC_FUNC_IMPL(rex_sub_823A6778) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r11.s64 = -2113601536;
    ctx.r10.s64 = -2113601536;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r11.s64 = ctx.r11.s64 + -13968;
    ctx.r10.s64 = ctx.r10.s64 + -13996;
    ctx.r9.s64 = -2106654720;
    ctx.r30.s64 = 0;
    ctx.r9.s64 = ctx.r9.s64 + -18624;
    PPC_STORE_U32(ctx.r31.u32 + 0, ctx.r11.u32);
    ctx.r11.s64 = 6144;
    PPC_STORE_U32(ctx.r31.u32 + 4, ctx.r10.u32);
    ctx.r10.s64 = -2103050240;
    ctx.r6.u64 = ctx.r11.u64;
    PPC_STORE_U32(ctx.r10.u32 + -3580, ctx.r11.u32);

loc_823A67C4:
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.r7.u64 = PPC_CHECK_GLOBAL_LOCK();
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r13.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_ENTER_GLOBAL_LOCK();
    ea = ctx.r9.u32;
    ctx.reserved.u32 = *(uint32_t*)PPC_RAW_ADDR(ea);
    ctx.r8.u64 = __builtin_bswap32(ctx.reserved.u32);
    ctx.cr6.compare<int32_t>(ctx.r8.s32, ctx.r30.s32, ctx.xer);
    if (!ctx.cr6.eq) {
        goto loc_823A67E8;
    }

    ea = ctx.r9.u32;
    ctx.cr0.lt = 0;
    ctx.cr0.gt = 0;
    ctx.cr0.eq = __sync_bool_compare_and_swap(
        reinterpret_cast<uint32_t*>(PPC_RAW_ADDR(ea)), ctx.reserved.s32,
        __builtin_bswap32(ctx.r6.s32));
    ctx.cr0.so = ctx.xer.so;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r7.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_LEAVE_GLOBAL_LOCK();
    if (!ctx.cr0.eq) {
        goto loc_823A67C4;
    }
    goto loc_823A67F0;

loc_823A67E8:
    ea = ctx.r9.u32;
    ctx.cr0.lt = 0;
    ctx.cr0.gt = 0;
    ctx.cr0.eq = __sync_bool_compare_and_swap(
        reinterpret_cast<uint32_t*>(PPC_RAW_ADDR(ea)), ctx.reserved.s32,
        __builtin_bswap32(ctx.r8.s32));
    ctx.cr0.so = ctx.xer.so;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx.msr = (ctx.r7.u32 & 0x8020) | (ctx.msr & ~0x8020);
    PPC_LEAVE_GLOBAL_LOCK();

loc_823A67F0:
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);
    const uint32_t old_driver_ptr = ctx.r3.u32;
    ctx.cr6.compare<uint32_t>(ctx.r3.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823A680C;
    }

    ctx.lr = 0x823A6800;
    __imp__XAudioUnregisterRenderDriverClient(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A680C;
    }

    if (REXCVAR_GET(ac6_movie_audio_trace) ||
        ac6::audio_policy::IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6778 destructor-unregister owner={:08X} old_driver={:08X} "
            "status={:08X}",
            ctx.r31.u32,
            old_driver_ptr,
            static_cast<uint32_t>(ctx.r3.u32));
    }
    ac6::audio_policy::OnMovieAudioClientUnregistered(ctx.r31.u32,
                                                      old_driver_ptr);
    PPC_STORE_U32(ctx.r31.u32 + 24, ctx.r30.u32);

loc_823A680C:
    ctx.r11.s64 = -2113601536;
    ctx.r11.s64 = ctx.r11.s64 + -14044;
    PPC_STORE_U32(ctx.r31.u32 + 4, ctx.r11.u32);
    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r30.u64 = PPC_LOAD_U64(ctx.r1.u32 + -24);
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823A6878) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    const uint32_t packet_provider_ptr = ctx.r4.u32;
    const uint32_t packet_provider_base_ptr =
        packet_provider_ptr != 0 ? packet_provider_ptr - 8 : 0;
    ctx.r3.u64 = ctx.r4.u64;
    ctx.r4.s64 = ctx.r1.s64 + 80;
    ctx.lr = 0x823A6898;
    rex_sub_823A8648(ctx, base);
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823A68C4;
    }

    const uint32_t packet_word_0_before = PPC_LOAD_U32(ctx.r1.u32 + 80);
    const uint32_t packet_word_1_before = PPC_LOAD_U32(ctx.r1.u32 + 84);
    const uint32_t pre_samples_ptr = PPC_LOAD_U32(ctx.r1.u32 + 88);
    const uint32_t pre_sample_word_0 = LoadGuestWord(base, pre_samples_ptr, 0);
    const uint32_t pre_sample_word_1 = LoadGuestWord(base, pre_samples_ptr, 4);
    const uint32_t pre_sample_word_2 = LoadGuestWord(base, pre_samples_ptr, 8);
    const uint32_t pre_sample_word_3 = LoadGuestWord(base, pre_samples_ptr, 12);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
    ctx.r4.s64 = ctx.r1.s64 + 80;
    ctx.r3.u64 = ctx.r31.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 32);
    const uint32_t producer_callback_ptr = ctx.r11.u32;
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x823A68B8;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    ctx.r4.u64 = PPC_LOAD_U32(ctx.r1.u32 + 88);
    ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 24);

    const uint32_t driver_ptr = ctx.r3.u32;
    const uint32_t samples_ptr = ctx.r4.u32;
    const uint32_t packet_word_0_after = PPC_LOAD_U32(ctx.r1.u32 + 80);
    const uint32_t packet_word_1_after = PPC_LOAD_U32(ctx.r1.u32 + 84);
    const uint32_t post_sample_word_0 = LoadGuestWord(base, samples_ptr, 0);
    const uint32_t post_sample_word_1 = LoadGuestWord(base, samples_ptr, 4);
    const uint32_t post_sample_word_2 = LoadGuestWord(base, samples_ptr, 8);
    const uint32_t post_sample_word_3 = LoadGuestWord(base, samples_ptr, 12);
    const bool head_zero =
        post_sample_word_0 == 0 && post_sample_word_1 == 0 &&
        post_sample_word_2 == 0 && post_sample_word_3 == 0;
    const uint32_t previous_head_zero_state =
        g_movie_audio_last_head_zero_state.exchange(head_zero ? 1u : 0u, std::memory_order_relaxed);
    const bool zero_to_nonzero_transition = previous_head_zero_state != 0 && !head_zero;
    const bool suspicious_samples =
        samples_ptr == 0 || head_zero || samples_ptr == pre_samples_ptr;
    const uint64_t producer_trace_count =
        g_movie_audio_producer_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((REXCVAR_GET(ac6_movie_audio_trace) || ac6::audio_policy::IsDeepTraceEnabled()) &&
        (producer_trace_count <= 48 || suspicious_samples ||
         (producer_trace_count % 120) == 0)) {
        REXAPU_DEBUG(
            "AC6 producer snapshot owner={:08X} driver={:08X} producer={:08X} "
            "provider={:08X}/{:08X} packet_pre=[{:08X} {:08X} {:08X}] "
            "packet_post=[{:08X} {:08X} {:08X}] pre_head=[{:08X} {:08X} {:08X} {:08X}] "
            "post_head=[{:08X} {:08X} {:08X} {:08X}] reused_samples={} suspicious={}",
            ctx.r31.u32,
            driver_ptr,
            producer_callback_ptr,
            packet_provider_ptr,
            packet_provider_base_ptr,
            packet_word_0_before,
            packet_word_1_before,
            pre_samples_ptr,
            packet_word_0_after,
            packet_word_1_after,
            samples_ptr,
            pre_sample_word_0,
            pre_sample_word_1,
            pre_sample_word_2,
            pre_sample_word_3,
            post_sample_word_0,
            post_sample_word_1,
            post_sample_word_2,
            post_sample_word_3,
            samples_ptr == pre_samples_ptr,
            suspicious_samples);
    }
    if ((REXCVAR_GET(ac6_movie_audio_trace) || ac6::audio_policy::IsDeepTraceEnabled()) &&
        zero_to_nonzero_transition) {
        const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
        const uint32_t owner_word_0 = LoadGuestWord(base, ctx.r31.u32, 0);
        const uint32_t owner_word_1 = LoadGuestWord(base, ctx.r31.u32, 4);
        const uint32_t owner_word_2 = LoadGuestWord(base, ctx.r31.u32, 8);
        const uint32_t owner_word_3 = LoadGuestWord(base, ctx.r31.u32, 12);
        const uint32_t owner_word_4 = LoadGuestWord(base, ctx.r31.u32, 16);
        const uint32_t owner_word_5 = LoadGuestWord(base, ctx.r31.u32, 20);
        const uint32_t owner_word_6 = LoadGuestWord(base, ctx.r31.u32, 24);
        const uint32_t owner_word_7 = LoadGuestWord(base, ctx.r31.u32, 28);
        const uint32_t provider_word_0 = LoadGuestWord(base, packet_provider_base_ptr, 0);
        const uint32_t provider_word_1 = LoadGuestWord(base, packet_provider_base_ptr, 4);
        const uint32_t provider_word_2 = LoadGuestWord(base, packet_provider_base_ptr, 8);
        const uint32_t provider_word_3 = LoadGuestWord(base, packet_provider_base_ptr, 12);
        const uint32_t provider_word_4 = LoadGuestWord(base, packet_provider_base_ptr, 16);
        const uint32_t provider_word_5 = LoadGuestWord(base, packet_provider_base_ptr, 20);
        const uint32_t provider_word_6 = LoadGuestWord(base, packet_provider_base_ptr, 24);
        const uint32_t provider_word_7 = LoadGuestWord(base, packet_provider_base_ptr, 28);
        const uint32_t singleton_primary_0 = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t singleton_primary_1 = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t singleton_primary_2 = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t singleton_extra_0 = LoadGuestWord(base, singleton_ptr, 124);
        const uint32_t singleton_extra_1 = LoadGuestWord(base, singleton_ptr, 128);
        const uint32_t singleton_extra_2 = LoadGuestWord(base, singleton_ptr, 132);
        const uint32_t primary0_word_0 = LoadGuestWord(base, singleton_primary_0, 0);
        const uint32_t primary0_word_1 = LoadGuestWord(base, singleton_primary_0, 4);
        const uint32_t primary0_word_2 = LoadGuestWord(base, singleton_primary_0, 8);
        const uint32_t primary0_word_3 = LoadGuestWord(base, singleton_primary_0, 12);
        const uint32_t primary1_word_0 = LoadGuestWord(base, singleton_primary_1, 0);
        const uint32_t primary1_word_1 = LoadGuestWord(base, singleton_primary_1, 4);
        const uint32_t primary1_word_2 = LoadGuestWord(base, singleton_primary_1, 8);
        const uint32_t primary1_word_3 = LoadGuestWord(base, singleton_primary_1, 12);
        const uint32_t singleton_state_292 =
            singleton_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(singleton_ptr + 292)) : 0;
        const uint32_t singleton_worker_count = LoadGuestWord(base, singleton_ptr, 304);
        REXAPU_DEBUG(
            "AC6 producer transition owner={:08X} driver={:08X} provider={:08X}/{:08X} "
            "producer={:08X} samples={:08X} zero_to_nonzero=true "
            "owner_head=[{:08X} {:08X} {:08X} {:08X}] owner_tail=[{:08X} {:08X} {:08X} {:08X}] "
            "provider_head=[{:08X} {:08X} {:08X} {:08X}] "
            "provider_packet=[{:08X} {:08X} {:08X} {:08X}] "
            "singleton={:08X} state_292={:02X} worker_count={} "
            "primary=[{:08X} {:08X} {:08X}] extra=[{:08X} {:08X} {:08X}] "
            "primary0_head=[{:08X} {:08X} {:08X} {:08X}] "
            "primary1_head=[{:08X} {:08X} {:08X} {:08X}]",
            ctx.r31.u32,
            driver_ptr,
            packet_provider_ptr,
            packet_provider_base_ptr,
            producer_callback_ptr,
            samples_ptr,
            owner_word_0,
            owner_word_1,
            owner_word_2,
            owner_word_3,
            owner_word_4,
            owner_word_5,
            owner_word_6,
            owner_word_7,
            provider_word_0,
            provider_word_1,
            provider_word_2,
            provider_word_3,
            provider_word_4,
            provider_word_5,
            provider_word_6,
            provider_word_7,
            singleton_ptr,
            singleton_state_292,
            singleton_worker_count,
            singleton_primary_0,
            singleton_primary_1,
            singleton_primary_2,
            singleton_extra_0,
            singleton_extra_1,
            singleton_extra_2,
            primary0_word_0,
            primary0_word_1,
            primary0_word_2,
            primary0_word_3,
            primary1_word_0,
            primary1_word_1,
            primary1_word_2,
            primary1_word_3);
    }
    if (REXCVAR_GET(ac6_movie_audio_trace_verbose) ||
        ac6::audio_policy::IsDeepTraceEnabled()) {
        REXAPU_DEBUG(
            "AC6 hook rex_sub_823A6878 submit owner={:08X} driver={:08X} samples={:08X}",
            ctx.r31.u32,
            driver_ptr,
            samples_ptr);
    }

    const uint32_t startup_silence_frames =
        ac6::audio_policy::ConsumeMovieAudioStartupSilenceFrames(ctx.r31.u32, driver_ptr);
    for (uint32_t i = 0; i < startup_silence_frames; ++i) {
        ctx.r3.u64 = driver_ptr;
        ctx.r4.u64 = 0;
        ctx.lr = 0x823A68BE;
        __imp__XAudioSubmitRenderDriverFrame(ctx, base);
    }

    ctx.r3.u64 = driver_ptr;
    ctx.r4.u64 = samples_ptr;
    ctx.lr = 0x823A68C4;
    __imp__XAudioSubmitRenderDriverFrame(ctx, base);
    if (ctx.r3.s32 >= 0) {
        ac6::audio_policy::OnMovieAudioFrameSubmitted(ctx.r31.u32, driver_ptr,
                                                      samples_ptr);
    }

loc_823A68C4:
    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823AD0D8) {
    PPC_FUNC_PROLOGUE();
    const uint32_t singleton_ptr = ctx.r3.u32;
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0;
    const uint32_t state_292_before =
        singleton_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(singleton_ptr + 292)) : 0;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t extra_0_before = LoadGuestWord(base, singleton_ptr, 124);
    const uint32_t extra_1_before = LoadGuestWord(base, singleton_ptr, 128);
    const uint32_t extra_2_before = LoadGuestWord(base, singleton_ptr, 132);
    const uint32_t primary0_head_0_before = LoadGuestWord(base, primary_0_before, 0);
    const uint32_t primary0_head_1_before = LoadGuestWord(base, primary_0_before, 4);
    const uint32_t primary0_head_2_before = LoadGuestWord(base, primary_0_before, 8);
    const uint32_t primary0_head_3_before = LoadGuestWord(base, primary_0_before, 12);
    const uint32_t primary1_head_0_before = LoadGuestWord(base, primary_1_before, 0);
    const uint32_t primary1_head_1_before = LoadGuestWord(base, primary_1_before, 4);
    const uint32_t primary1_head_2_before = LoadGuestWord(base, primary_1_before, 8);
    const uint32_t primary1_head_3_before = LoadGuestWord(base, primary_1_before, 12);

    __imp__rex_sub_823AD0D8(ctx, base);

    if (trace_movie_worker) {
        const uint32_t state_292_after =
            static_cast<uint32_t>(PPC_LOAD_U8(singleton_ptr + 292));
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t extra_0_after = LoadGuestWord(base, singleton_ptr, 124);
        const uint32_t extra_1_after = LoadGuestWord(base, singleton_ptr, 128);
        const uint32_t extra_2_after = LoadGuestWord(base, singleton_ptr, 132);
        const uint32_t primary0_head_0_after = LoadGuestWord(base, primary_0_after, 0);
        const uint32_t primary0_head_1_after = LoadGuestWord(base, primary_0_after, 4);
        const uint32_t primary0_head_2_after = LoadGuestWord(base, primary_0_after, 8);
        const uint32_t primary0_head_3_after = LoadGuestWord(base, primary_0_after, 12);
        const uint32_t primary1_head_0_after = LoadGuestWord(base, primary_1_after, 0);
        const uint32_t primary1_head_1_after = LoadGuestWord(base, primary_1_after, 4);
        const uint32_t primary1_head_2_after = LoadGuestWord(base, primary_1_after, 8);
        const uint32_t primary1_head_3_after = LoadGuestWord(base, primary_1_after, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_prepare_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            state_292_before != state_292_after || primary_0_before != primary_0_after ||
            primary_1_before != primary_1_after || primary_2_before != primary_2_after ||
            extra_0_before != extra_0_after || extra_1_before != extra_1_after ||
            extra_2_before != extra_2_after || primary0_head_0_before != primary0_head_0_after ||
            primary0_head_1_before != primary0_head_1_after ||
            primary0_head_2_before != primary0_head_2_after ||
            primary0_head_3_before != primary0_head_3_after ||
            primary1_head_0_before != primary1_head_0_after ||
            primary1_head_1_before != primary1_head_1_after ||
            primary1_head_2_before != primary1_head_2_after ||
            primary1_head_3_before != primary1_head_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker prepare singleton={:08X} state_292_before={:02X} "
                "state_292_after={:02X} primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after=[{:08X} {:08X} {:08X}] extra_before=[{:08X} {:08X} {:08X}] "
                "extra_after=[{:08X} {:08X} {:08X}] primary0_head_before=[{:08X} {:08X} {:08X} "
                "{:08X}] primary0_head_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                singleton_ptr,
                state_292_before,
                state_292_after,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after,
                extra_0_before,
                extra_1_before,
                extra_2_before,
                extra_0_after,
                extra_1_after,
                extra_2_after,
                primary0_head_0_before,
                primary0_head_1_before,
                primary0_head_2_before,
                primary0_head_3_before,
                primary0_head_0_after,
                primary0_head_1_after,
                primary0_head_2_after,
                primary0_head_3_after,
                primary1_head_0_before,
                primary1_head_1_before,
                primary1_head_2_before,
                primary1_head_3_before,
                primary1_head_0_after,
                primary1_head_1_after,
                primary1_head_2_after,
                primary1_head_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823AD1C0) {
    PPC_FUNC_PROLOGUE();
    const uint32_t singleton_ptr = ctx.r3.u32;
    const uint32_t mode = ctx.r4.u32 & 0xFF;
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0;
    const uint32_t state_292_before =
        singleton_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(singleton_ptr + 292)) : 0;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t extra_0_before = LoadGuestWord(base, singleton_ptr, 124);
    const uint32_t extra_1_before = LoadGuestWord(base, singleton_ptr, 128);
    const uint32_t extra_2_before = LoadGuestWord(base, singleton_ptr, 132);
    const uint32_t primary0_head_0_before = LoadGuestWord(base, primary_0_before, 0);
    const uint32_t primary0_head_1_before = LoadGuestWord(base, primary_0_before, 4);
    const uint32_t primary0_head_2_before = LoadGuestWord(base, primary_0_before, 8);
    const uint32_t primary0_head_3_before = LoadGuestWord(base, primary_0_before, 12);
    const uint32_t primary1_head_0_before = LoadGuestWord(base, primary_1_before, 0);
    const uint32_t primary1_head_1_before = LoadGuestWord(base, primary_1_before, 4);
    const uint32_t primary1_head_2_before = LoadGuestWord(base, primary_1_before, 8);
    const uint32_t primary1_head_3_before = LoadGuestWord(base, primary_1_before, 12);

    __imp__rex_sub_823AD1C0(ctx, base);

    if (trace_movie_worker) {
        const uint32_t state_292_after =
            static_cast<uint32_t>(PPC_LOAD_U8(singleton_ptr + 292));
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t extra_0_after = LoadGuestWord(base, singleton_ptr, 124);
        const uint32_t extra_1_after = LoadGuestWord(base, singleton_ptr, 128);
        const uint32_t extra_2_after = LoadGuestWord(base, singleton_ptr, 132);
        const uint32_t primary0_head_0_after = LoadGuestWord(base, primary_0_after, 0);
        const uint32_t primary0_head_1_after = LoadGuestWord(base, primary_0_after, 4);
        const uint32_t primary0_head_2_after = LoadGuestWord(base, primary_0_after, 8);
        const uint32_t primary0_head_3_after = LoadGuestWord(base, primary_0_after, 12);
        const uint32_t primary1_head_0_after = LoadGuestWord(base, primary_1_after, 0);
        const uint32_t primary1_head_1_after = LoadGuestWord(base, primary_1_after, 4);
        const uint32_t primary1_head_2_after = LoadGuestWord(base, primary_1_after, 8);
        const uint32_t primary1_head_3_after = LoadGuestWord(base, primary_1_after, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_finalize_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            state_292_before != state_292_after || primary_0_before != primary_0_after ||
            primary_1_before != primary_1_after || primary_2_before != primary_2_after ||
            extra_0_before != extra_0_after || extra_1_before != extra_1_after ||
            extra_2_before != extra_2_after || primary0_head_0_before != primary0_head_0_after ||
            primary0_head_1_before != primary0_head_1_after ||
            primary0_head_2_before != primary0_head_2_after ||
            primary0_head_3_before != primary0_head_3_after ||
            primary1_head_0_before != primary1_head_0_after ||
            primary1_head_1_before != primary1_head_1_after ||
            primary1_head_2_before != primary1_head_2_after ||
            primary1_head_3_before != primary1_head_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker finalize singleton={:08X} mode={} state_292_before={:02X} "
                "state_292_after={:02X} primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after=[{:08X} {:08X} {:08X}] extra_before=[{:08X} {:08X} {:08X}] "
                "extra_after=[{:08X} {:08X} {:08X}] primary0_head_before=[{:08X} {:08X} {:08X} "
                "{:08X}] primary0_head_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                singleton_ptr,
                mode,
                state_292_before,
                state_292_after,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after,
                extra_0_before,
                extra_1_before,
                extra_2_before,
                extra_0_after,
                extra_1_after,
                extra_2_after,
                primary0_head_0_before,
                primary0_head_1_before,
                primary0_head_2_before,
                primary0_head_3_before,
                primary0_head_0_after,
                primary0_head_1_after,
                primary0_head_2_after,
                primary0_head_3_after,
                primary1_head_0_before,
                primary1_head_1_before,
                primary1_head_2_before,
                primary1_head_3_before,
                primary1_head_0_after,
                primary1_head_1_after,
                primary1_head_2_after,
                primary1_head_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823AD9C0) {
    PPC_FUNC_PROLOGUE();
    g_last_movie_singleton_ptr.store(ctx.r3.u32, std::memory_order_relaxed);
    __imp__rex_sub_823AD9C0(ctx, base);
    return;
}

PPC_FUNC_IMPL(rex_sub_823B0DB8) {
    PPC_FUNC_PROLOGUE();
    const uint32_t arg0 = ctx.r3.u32;
    const uint32_t arg1 = ctx.r4.u32;
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t primary0_head_0_before = LoadGuestWord(base, primary_0_before, 0);
    const uint32_t primary0_head_1_before = LoadGuestWord(base, primary_0_before, 4);
    const uint32_t primary0_head_2_before = LoadGuestWord(base, primary_0_before, 8);
    const uint32_t primary0_head_3_before = LoadGuestWord(base, primary_0_before, 12);
    const uint32_t primary1_head_0_before = LoadGuestWord(base, primary_1_before, 0);
    const uint32_t primary1_head_1_before = LoadGuestWord(base, primary_1_before, 4);
    const uint32_t primary1_head_2_before = LoadGuestWord(base, primary_1_before, 8);
    const uint32_t primary1_head_3_before = LoadGuestWord(base, primary_1_before, 12);

    __imp__rex_sub_823B0DB8(ctx, base);

    if (trace_movie_worker) {
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t primary0_head_0_after = LoadGuestWord(base, primary_0_after, 0);
        const uint32_t primary0_head_1_after = LoadGuestWord(base, primary_0_after, 4);
        const uint32_t primary0_head_2_after = LoadGuestWord(base, primary_0_after, 8);
        const uint32_t primary0_head_3_after = LoadGuestWord(base, primary_0_after, 12);
        const uint32_t primary1_head_0_after = LoadGuestWord(base, primary_1_after, 0);
        const uint32_t primary1_head_1_after = LoadGuestWord(base, primary_1_after, 4);
        const uint32_t primary1_head_2_after = LoadGuestWord(base, primary_1_after, 8);
        const uint32_t primary1_head_3_after = LoadGuestWord(base, primary_1_after, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_post_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            primary_0_before != primary_0_after || primary_1_before != primary_1_after ||
            primary_2_before != primary_2_after || primary0_head_0_before != primary0_head_0_after ||
            primary0_head_1_before != primary0_head_1_after ||
            primary0_head_2_before != primary0_head_2_after ||
            primary0_head_3_before != primary0_head_3_after ||
            primary1_head_0_before != primary1_head_0_after ||
            primary1_head_1_before != primary1_head_1_after ||
            primary1_head_2_before != primary1_head_2_after ||
            primary1_head_3_before != primary1_head_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker post arg0={:08X} arg1={:08X} singleton={:08X} "
                "primary_before=[{:08X} {:08X} {:08X}] primary_after=[{:08X} {:08X} {:08X}] "
                "primary0_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary0_head_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                arg0,
                arg1,
                singleton_ptr,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after,
                primary0_head_0_before,
                primary0_head_1_before,
                primary0_head_2_before,
                primary0_head_3_before,
                primary0_head_0_after,
                primary0_head_1_after,
                primary0_head_2_after,
                primary0_head_3_after,
                primary1_head_0_before,
                primary1_head_1_before,
                primary1_head_2_before,
                primary1_head_3_before,
                primary1_head_0_after,
                primary1_head_1_after,
                primary1_head_2_after,
                primary1_head_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823B0DF0) {
    PPC_FUNC_PROLOGUE();
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t primary0_head_0_before = LoadGuestWord(base, primary_0_before, 0);
    const uint32_t primary0_head_1_before = LoadGuestWord(base, primary_0_before, 4);
    const uint32_t primary0_head_2_before = LoadGuestWord(base, primary_0_before, 8);
    const uint32_t primary0_head_3_before = LoadGuestWord(base, primary_0_before, 12);
    const uint32_t primary1_head_0_before = LoadGuestWord(base, primary_1_before, 0);
    const uint32_t primary1_head_1_before = LoadGuestWord(base, primary_1_before, 4);
    const uint32_t primary1_head_2_before = LoadGuestWord(base, primary_1_before, 8);
    const uint32_t primary1_head_3_before = LoadGuestWord(base, primary_1_before, 12);

    __imp__rex_sub_823B0DF0(ctx, base);

    if (trace_movie_worker) {
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t primary0_head_0_after = LoadGuestWord(base, primary_0_after, 0);
        const uint32_t primary0_head_1_after = LoadGuestWord(base, primary_0_after, 4);
        const uint32_t primary0_head_2_after = LoadGuestWord(base, primary_0_after, 8);
        const uint32_t primary0_head_3_after = LoadGuestWord(base, primary_0_after, 12);
        const uint32_t primary1_head_0_after = LoadGuestWord(base, primary_1_after, 0);
        const uint32_t primary1_head_1_after = LoadGuestWord(base, primary_1_after, 4);
        const uint32_t primary1_head_2_after = LoadGuestWord(base, primary_1_after, 8);
        const uint32_t primary1_head_3_after = LoadGuestWord(base, primary_1_after, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_flush_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            primary_0_before != primary_0_after || primary_1_before != primary_1_after ||
            primary_2_before != primary_2_after || primary0_head_0_before != primary0_head_0_after ||
            primary0_head_1_before != primary0_head_1_after ||
            primary0_head_2_before != primary0_head_2_after ||
            primary0_head_3_before != primary0_head_3_after ||
            primary1_head_0_before != primary1_head_0_after ||
            primary1_head_1_before != primary1_head_1_after ||
            primary1_head_2_before != primary1_head_2_after ||
            primary1_head_3_before != primary1_head_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker flush singleton={:08X} primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after=[{:08X} {:08X} {:08X}] primary0_head_before=[{:08X} {:08X} {:08X} "
                "{:08X}] primary0_head_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                singleton_ptr,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after,
                primary0_head_0_before,
                primary0_head_1_before,
                primary0_head_2_before,
                primary0_head_3_before,
                primary0_head_0_after,
                primary0_head_1_after,
                primary0_head_2_after,
                primary0_head_3_after,
                primary1_head_0_before,
                primary1_head_1_before,
                primary1_head_2_before,
                primary1_head_3_before,
                primary1_head_0_after,
                primary1_head_1_after,
                primary1_head_2_after,
                primary1_head_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823B7EC8) {
    PPC_FUNC_PROLOGUE();
    const uint32_t obj_ptr = ctx.r3.u32;
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0;
    const uint32_t obj_word_0_before = LoadGuestWord(base, obj_ptr, 0);
    const uint32_t obj_word_1_before = LoadGuestWord(base, obj_ptr, 4);
    const uint32_t obj_word_2_before = LoadGuestWord(base, obj_ptr, 8);
    const uint32_t obj_word_3_before = LoadGuestWord(base, obj_ptr, 12);
    const uint32_t obj_word_8_before = LoadGuestWord(base, obj_ptr, 32);
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t primary0_head_0_before = LoadGuestWord(base, primary_0_before, 0);
    const uint32_t primary0_head_1_before = LoadGuestWord(base, primary_0_before, 4);
    const uint32_t primary0_head_2_before = LoadGuestWord(base, primary_0_before, 8);
    const uint32_t primary0_head_3_before = LoadGuestWord(base, primary_0_before, 12);
    const uint32_t primary1_head_0_before = LoadGuestWord(base, primary_1_before, 0);
    const uint32_t primary1_head_1_before = LoadGuestWord(base, primary_1_before, 4);
    const uint32_t primary1_head_2_before = LoadGuestWord(base, primary_1_before, 8);
    const uint32_t primary1_head_3_before = LoadGuestWord(base, primary_1_before, 12);

    __imp__rex_sub_823B7EC8(ctx, base);

    if (trace_movie_worker) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t obj_word_0_after = LoadGuestWord(base, obj_ptr, 0);
        const uint32_t obj_word_1_after = LoadGuestWord(base, obj_ptr, 4);
        const uint32_t obj_word_2_after = LoadGuestWord(base, obj_ptr, 8);
        const uint32_t obj_word_3_after = LoadGuestWord(base, obj_ptr, 12);
        const uint32_t obj_word_8_after = LoadGuestWord(base, obj_ptr, 32);
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t primary0_head_0_after = LoadGuestWord(base, primary_0_after, 0);
        const uint32_t primary0_head_1_after = LoadGuestWord(base, primary_0_after, 4);
        const uint32_t primary0_head_2_after = LoadGuestWord(base, primary_0_after, 8);
        const uint32_t primary0_head_3_after = LoadGuestWord(base, primary_0_after, 12);
        const uint32_t primary1_head_0_after = LoadGuestWord(base, primary_1_after, 0);
        const uint32_t primary1_head_1_after = LoadGuestWord(base, primary_1_after, 4);
        const uint32_t primary1_head_2_after = LoadGuestWord(base, primary_1_after, 8);
        const uint32_t primary1_head_3_after = LoadGuestWord(base, primary_1_after, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_publish_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            result != 0 || obj_word_0_before != obj_word_0_after ||
            obj_word_1_before != obj_word_1_after || obj_word_2_before != obj_word_2_after ||
            obj_word_3_before != obj_word_3_after || obj_word_8_before != obj_word_8_after ||
            primary_0_before != primary_0_after || primary_1_before != primary_1_after ||
            primary_2_before != primary_2_after || primary0_head_0_before != primary0_head_0_after ||
            primary0_head_1_before != primary0_head_1_after ||
            primary0_head_2_before != primary0_head_2_after ||
            primary0_head_3_before != primary0_head_3_after ||
            primary1_head_0_before != primary1_head_0_after ||
            primary1_head_1_before != primary1_head_1_after ||
            primary1_head_2_before != primary1_head_2_after ||
            primary1_head_3_before != primary1_head_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker publish obj={:08X} singleton={:08X} caller_lr={:08X} result={} "
                "obj_head_before=[{:08X} {:08X} {:08X} {:08X}] obj32_before={:08X} "
                "obj_head_after=[{:08X} {:08X} {:08X} {:08X}] obj32_after={:08X} "
                "primary_before=[{:08X} {:08X} {:08X}] primary_after=[{:08X} {:08X} {:08X}] "
                "primary0_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary0_head_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_before=[{:08X} {:08X} {:08X} {:08X}] "
                "primary1_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                obj_ptr,
                singleton_ptr,
                caller_lr,
                result,
                obj_word_0_before,
                obj_word_1_before,
                obj_word_2_before,
                obj_word_3_before,
                obj_word_8_before,
                obj_word_0_after,
                obj_word_1_after,
                obj_word_2_after,
                obj_word_3_after,
                obj_word_8_after,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after,
                primary0_head_0_before,
                primary0_head_1_before,
                primary0_head_2_before,
                primary0_head_3_before,
                primary0_head_0_after,
                primary0_head_1_after,
                primary0_head_2_after,
                primary0_head_3_after,
                primary1_head_0_before,
                primary1_head_1_before,
                primary1_head_2_before,
                primary1_head_3_before,
                primary1_head_0_after,
                primary1_head_1_after,
                primary1_head_2_after,
                primary1_head_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823B7C80) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t obj_ptr = ctx.r3.u32;
    const uint32_t arg_ptr = ctx.r4.u32;
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0 && caller_lr == 0x823B91E8;
    const uint32_t obj68_before = LoadGuestWord(base, obj_ptr, 68);
    const uint32_t obj68_vtable_before = LoadGuestWord(base, obj68_before, 0);
    const uint32_t obj68_slot24_before = LoadGuestWord(base, obj68_vtable_before, 24);
    const uint32_t arg_word0_before = LoadGuestWord(base, arg_ptr, 0);
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);

    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r30.u64 = ctx.r4.u64;

    ctx.lr = 0x823B7CA0;
    __imp__rex_sub_823B9F10(ctx, base);

    int32_t stage1_result = ctx.r3.s32;
    uint32_t primary_0_after_stage1 = LoadGuestWord(base, singleton_ptr, 80);
    uint32_t primary_1_after_stage1 = LoadGuestWord(base, singleton_ptr, 84);
    uint32_t primary_2_after_stage1 = LoadGuestWord(base, singleton_ptr, 88);

    uint32_t stage2_target = 0;
    uint32_t stage2_arg_word0 = 0;
    uint32_t stage2_result = 0;
    if (ctx.r3.s32 >= 0) {
        ctx.r3.u64 = PPC_LOAD_U32(ctx.r31.u32 + 68);
        ctx.r5.s64 = 0;
        ctx.r4.u64 = PPC_LOAD_U32(ctx.r30.u32 + 0);
        stage2_arg_word0 = ctx.r4.u32;
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r3.u32 + 0);
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 24);
        stage2_target = ctx.r11.u32;
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x823B7CC4;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
        stage2_result = ctx.r3.u32;
    }

    if (trace_movie_worker) {
        const uint32_t obj68_after = LoadGuestWord(base, obj_ptr, 68);
        const uint32_t obj68_vtable_after = LoadGuestWord(base, obj68_after, 0);
        const uint32_t obj68_slot24_after = LoadGuestWord(base, obj68_vtable_after, 24);
        const uint32_t arg_word0_after = LoadGuestWord(base, arg_ptr, 0);
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint64_t trace_count =
            g_movie_audio_worker_step84_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            stage1_result != 0 || stage2_result != 0 || obj68_before != obj68_after ||
            obj68_vtable_before != obj68_vtable_after ||
            obj68_slot24_before != obj68_slot24_after || arg_word0_before != arg_word0_after ||
            primary_0_before != primary_0_after_stage1 ||
            primary_1_before != primary_1_after_stage1 ||
            primary_2_before != primary_2_after_stage1 ||
            primary_0_after_stage1 != primary_0_after ||
            primary_1_after_stage1 != primary_1_after ||
            primary_2_after_stage1 != primary_2_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker publish-step84 obj={:08X} arg={:08X} singleton={:08X} "
                "caller_lr={:08X} obj68_before={:08X} obj68_vt_before={:08X} "
                "obj68_slot24_before={:08X} arg0_before={:08X} "
                "stage1_result={} primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after_stage1=[{:08X} {:08X} {:08X}] "
                "stage2_target={:08X} stage2_arg0={:08X} stage2_result={:08X} "
                "obj68_after={:08X} obj68_vt_after={:08X} obj68_slot24_after={:08X} "
                "arg0_after={:08X} primary_after=[{:08X} {:08X} {:08X}]",
                obj_ptr,
                arg_ptr,
                singleton_ptr,
                caller_lr,
                obj68_before,
                obj68_vtable_before,
                obj68_slot24_before,
                arg_word0_before,
                stage1_result,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after_stage1,
                primary_1_after_stage1,
                primary_2_after_stage1,
                stage2_target,
                stage2_arg_word0,
                stage2_result,
                obj68_after,
                obj68_vtable_after,
                obj68_slot24_after,
                arg_word0_after,
                primary_0_after,
                primary_1_after,
                primary_2_after);
        }
    }

    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r30.u64 = PPC_LOAD_U64(ctx.r1.u32 + -24);
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823B9F10) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t obj_ptr = ctx.r3.u32;
    const uint32_t arg_ptr = ctx.r4.u32;
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0 && caller_lr == 0x823B7CA0;

    ctx.r12.u64 = ctx.lr;
    ctx.lr = 0x823B9F18;
    __savegprlr_29(ctx, base);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;
    ctx.r29.u64 = ctx.r4.u64;
    ctx.r11.s64 = 0;
    ctx.r3.s64 = 0;
    ctx.r10.u64 = PPC_LOAD_U8(ctx.r31.u32 + 60);
    const uint32_t iteration_count = ctx.r10.u32;
    ctx.cr6.compare<uint32_t>(ctx.r10.u32, 0, ctx.xer);
    if (ctx.cr6.eq) {
        goto loc_823B9F74_reimpl;
    }

loc_823B9F38_reimpl:
    ctx.cr6.compare<int32_t>(ctx.r3.s32, 0, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823B9F74_reimpl;
    }

    ctx.r30.u64 = ctx.r11.u32 & 0xFF;
    ctx.r10.u64 = PPC_LOAD_U32(ctx.r31.u32 + 36);
    ctx.r5.u64 = ctx.r29.u64;
    ctx.r11.u64 = __builtin_rotateleft64(ctx.r30.u32 | (ctx.r30.u64 << 32), 3) & 0xFFFFFFF8;
    ctx.r3.u64 = ctx.r31.u64;
    ctx.r4.u64 = ctx.r11.u64 + ctx.r10.u64;

    const uint32_t iter_index = ctx.r30.u32;
    const uint32_t desc_ptr = ctx.r4.u32;
    const uint32_t desc_word0 = LoadGuestWord(base, desc_ptr, 0);
    const uint32_t desc_word1 = LoadGuestWord(base, desc_ptr, 4);
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);

    ctx.lr = 0x823B9F5C;
    __imp__rex_sub_823B9700(ctx, base);

    const int32_t iter_result = ctx.r3.s32;
    const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);

    if (trace_movie_worker) {
        const uint64_t trace_count =
            g_movie_audio_worker_step9f10_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            iter_result != 0 || primary_0_before != primary_0_after ||
            primary_1_before != primary_1_after || primary_2_before != primary_2_after;
        const bool should_trace = trace_count <= 96 || changed || (trace_count % 240) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker publish-step9F10 obj={:08X} arg={:08X} singleton={:08X} "
                "caller_lr={:08X} count={} iter={} desc={:08X} desc_words=[{:08X} {:08X}] "
                "result={} primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after=[{:08X} {:08X} {:08X}]",
                obj_ptr,
                arg_ptr,
                singleton_ptr,
                caller_lr,
                iteration_count,
                iter_index,
                desc_ptr,
                desc_word0,
                desc_word1,
                iter_result,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after);
        }
    }

    ctx.r11.s64 = ctx.r30.s64 + 1;
    ctx.r10.u64 = PPC_LOAD_U8(ctx.r31.u32 + 60);
    ctx.r11.u64 = ctx.r11.u32 & 0xFF;
    ctx.r9.u64 = ctx.r11.u64;
    ctx.cr6.compare<uint32_t>(ctx.r9.u32, ctx.r10.u32, ctx.xer);
    if (ctx.cr6.lt) {
        goto loc_823B9F38_reimpl;
    }

loc_823B9F74_reimpl:
    ctx.r1.s64 = ctx.r1.s64 + 112;
    __restgprlr_29(ctx, base);
    return;
}

PPC_FUNC_IMPL(rex_sub_823B9158) {
    PPC_FUNC_PROLOGUE();
    uint32_t ea{};
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0 && caller_lr == 0x823B7EE4;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);

    ctx.r12.u64 = ctx.lr;
    PPC_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
    PPC_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
    ea = -112 + ctx.r1.u32;
    PPC_STORE_U32(ea, ctx.r1.u32);
    ctx.r1.u32 = ea;
    ctx.r31.u64 = ctx.r3.u64;

    const uint32_t obj_ptr = ctx.r31.u32;
    const uint32_t slot72_target_before = LoadGuestWord(base, LoadGuestWord(base, obj_ptr, 0), 72);
    const uint32_t slot84_target_before = LoadGuestWord(base, LoadGuestWord(base, obj_ptr, 0), 84);
    const uint32_t obj_word_44_before = LoadGuestWord(base, obj_ptr, 44);
    const uint32_t obj_word_48_before = LoadGuestWord(base, obj_ptr, 48);
    const uint32_t obj_byte_61_before =
        obj_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(obj_ptr + 61)) : 0;
    const uint32_t obj_half_64_before = LoadGuestHalf(base, obj_ptr, 64);

    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 72);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x823B917C;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
    PPC_STORE_U32(ctx.r1.u32 + 84, ctx.r3.u32);

    const uint32_t slot72_result = PPC_LOAD_U32(ctx.r1.u32 + 84);
    const uint32_t primary_0_after_slot72 = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_after_slot72 = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_after_slot72 = LoadGuestWord(base, singleton_ptr, 88);

    ctx.r3.s64 = 3;
    ctx.lr = 0x823B9188;
    __imp__rex_sub_823A2C30(ctx, base);

    uint32_t callback_arg_before = 0;
    uint32_t callback_arg_after = 0;
    if (obj_word_44_before != 0) {
        ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 44);
        ctx.r10.u64 = PPC_LOAD_U32(ctx.r31.u32 + 48);
        ctx.r3.s64 = ctx.r1.s64 + 80;
        PPC_STORE_U32(ctx.r1.u32 + 80, ctx.r10.u32);
        callback_arg_before = PPC_LOAD_U32(ctx.r1.u32 + 80);
        ctx.ctr.u64 = ctx.r11.u64;
        ctx.lr = 0x823B91A8;
        PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
        callback_arg_after = PPC_LOAD_U32(ctx.r1.u32 + 80);
    }

    const uint32_t primary_0_after_callback = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_after_callback = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_after_callback = LoadGuestWord(base, singleton_ptr, 88);

    ctx.r11.u64 = PPC_LOAD_U8(ctx.r31.u32 + 61);
    ctx.r11.u64 = __builtin_rotateleft64(ctx.r11.u32 | (ctx.r11.u64 << 32), 0) & 0x4;
    ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
    if (!ctx.cr6.eq) {
        ctx.r11.u64 = PPC_LOAD_U16(ctx.r31.u32 + 64);
        ctx.cr6.compare<uint32_t>(ctx.r11.u32, 0, ctx.xer);
        if (ctx.cr6.eq) {
            ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
            ctx.r4.s64 = 1;
            ctx.r3.u64 = ctx.r31.u64;
            ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 60);
            ctx.ctr.u64 = ctx.r11.u64;
            ctx.lr = 0x823B9224;
            PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);
            goto loc_823B91E8_reimpl;
        }
        ctx.r11.s64 = ctx.r11.s64 + 65536;
        ctx.r11.s64 = ctx.r11.s64 + -1;
        PPC_STORE_U16(ctx.r31.u32 + 64, ctx.r11.u16);
    }

    ctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
    ctx.r4.s64 = ctx.r1.s64 + 84;
    ctx.r3.u64 = ctx.r31.u64;
    ctx.r11.u64 = PPC_LOAD_U32(ctx.r11.u32 + 84);
    ctx.ctr.u64 = ctx.r11.u64;
    ctx.lr = 0x823B91E8;
    PPC_CALL_INDIRECT_FUNC(ctx.ctr.u32);

loc_823B91E8_reimpl:
    const uint32_t slot84_result = ctx.r3.u32;
    const uint32_t primary_0_after_slot84 = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_after_slot84 = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_after_slot84 = LoadGuestWord(base, singleton_ptr, 88);

    ctx.r31.u64 = ctx.r3.u64;
    ctx.r3.s64 = 3;
    ctx.lr = 0x823B91F4;
    __imp__rex_sub_823A2C30(ctx, base);
    ctx.r3.u64 = ctx.r31.u64;

    if (trace_movie_worker) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t obj_word_44_after = LoadGuestWord(base, obj_ptr, 44);
        const uint32_t obj_word_48_after = LoadGuestWord(base, obj_ptr, 48);
        const uint32_t obj_byte_61_after =
            obj_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(obj_ptr + 61)) : 0;
        const uint32_t obj_half_64_after = LoadGuestHalf(base, obj_ptr, 64);
        const uint64_t trace_count =
            g_movie_audio_worker_step1_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            result != 0 || slot72_result != 0 || slot84_result != 0 ||
            primary_0_before != primary_0_after_slot72 || primary_1_before != primary_1_after_slot72 ||
            primary_2_before != primary_2_after_slot72 ||
            primary_0_after_slot72 != primary_0_after_callback ||
            primary_1_after_slot72 != primary_1_after_callback ||
            primary_2_after_slot72 != primary_2_after_callback ||
            primary_0_after_callback != primary_0_after_slot84 ||
            primary_1_after_callback != primary_1_after_slot84 ||
            primary_2_after_callback != primary_2_after_slot84 ||
            callback_arg_before != callback_arg_after ||
            obj_word_44_before != obj_word_44_after || obj_word_48_before != obj_word_48_after ||
            obj_byte_61_before != obj_byte_61_after || obj_half_64_before != obj_half_64_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker publish-step1 obj={:08X} singleton={:08X} caller_lr={:08X} "
                "result={} slot72={:08X} slot72_result={:08X} cb44={:08X} cb48={:08X} "
                "cb_arg_before={:08X} cb_arg_after={:08X} slot84={:08X} slot84_result={:08X} "
                "obj61_before={:02X} obj64_before={:04X} obj61_after={:02X} obj64_after={:04X} "
                "primary_before=[{:08X} {:08X} {:08X}] "
                "primary_after_slot72=[{:08X} {:08X} {:08X}] "
                "primary_after_callback=[{:08X} {:08X} {:08X}] "
                "primary_after_slot84=[{:08X} {:08X} {:08X}]",
                obj_ptr,
                singleton_ptr,
                caller_lr,
                result,
                slot72_target_before,
                slot72_result,
                obj_word_44_before,
                obj_word_48_before,
                callback_arg_before,
                callback_arg_after,
                slot84_target_before,
                slot84_result,
                obj_byte_61_before,
                obj_half_64_before,
                obj_byte_61_after,
                obj_half_64_after,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after_slot72,
                primary_1_after_slot72,
                primary_2_after_slot72,
                primary_0_after_callback,
                primary_1_after_callback,
                primary_2_after_callback,
                primary_0_after_slot84,
                primary_1_after_slot84,
                primary_2_after_slot84);
        }
    }

    ctx.r1.s64 = ctx.r1.s64 + 112;
    ctx.r12.u64 = PPC_LOAD_U32(ctx.r1.u32 + -8);
    ctx.lr = ctx.r12.u64;
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 + -16);
    return;
}

PPC_FUNC_IMPL(rex_sub_823A8910) {
    PPC_FUNC_PROLOGUE();
    const uint32_t obj_member_ptr = ctx.r3.u32;
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t singleton_ptr = g_last_movie_singleton_ptr.load(std::memory_order_relaxed);
    const bool trace_movie_worker =
        ac6::audio_policy::IsMovieAudioActive() && ac6::audio_policy::IsDeepTraceEnabled() &&
        singleton_ptr != 0 && caller_lr == 0x823B7F00;
    const uint32_t primary_0_before = LoadGuestWord(base, singleton_ptr, 80);
    const uint32_t primary_1_before = LoadGuestWord(base, singleton_ptr, 84);
    const uint32_t primary_2_before = LoadGuestWord(base, singleton_ptr, 88);
    const uint32_t obj_base_ptr = obj_member_ptr != 0 ? obj_member_ptr - 8 : 0;
    const uint32_t obj_base_word_0_before = LoadGuestWord(base, obj_base_ptr, 0);
    const uint32_t obj_base_word_1_before = LoadGuestWord(base, obj_base_ptr, 4);
    const uint32_t obj_base_word_2_before = LoadGuestWord(base, obj_base_ptr, 8);
    const uint32_t obj_base_word_3_before = LoadGuestWord(base, obj_base_ptr, 12);

    __imp__rex_sub_823A8910(ctx, base);

    if (trace_movie_worker) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t primary_0_after = LoadGuestWord(base, singleton_ptr, 80);
        const uint32_t primary_1_after = LoadGuestWord(base, singleton_ptr, 84);
        const uint32_t primary_2_after = LoadGuestWord(base, singleton_ptr, 88);
        const uint32_t obj_base_word_0_after = LoadGuestWord(base, obj_base_ptr, 0);
        const uint32_t obj_base_word_1_after = LoadGuestWord(base, obj_base_ptr, 4);
        const uint32_t obj_base_word_2_after = LoadGuestWord(base, obj_base_ptr, 8);
        const uint32_t obj_base_word_3_after = LoadGuestWord(base, obj_base_ptr, 12);
        const uint64_t trace_count =
            g_movie_audio_worker_step2_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool changed =
            result != 0 || primary_0_before != primary_0_after ||
            primary_1_before != primary_1_after || primary_2_before != primary_2_after ||
            obj_base_word_0_before != obj_base_word_0_after ||
            obj_base_word_1_before != obj_base_word_1_after ||
            obj_base_word_2_before != obj_base_word_2_after ||
            obj_base_word_3_before != obj_base_word_3_after;
        const bool should_trace = trace_count <= 64 || changed || (trace_count % 180) == 0;
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie worker publish-step2 obj_member={:08X} obj_base={:08X} "
                "singleton={:08X} caller_lr={:08X} result={} "
                "obj_base_before=[{:08X} {:08X} {:08X} {:08X}] "
                "obj_base_after=[{:08X} {:08X} {:08X} {:08X}] "
                "primary_before=[{:08X} {:08X} {:08X}] primary_after=[{:08X} {:08X} {:08X}]",
                obj_member_ptr,
                obj_base_ptr,
                singleton_ptr,
                caller_lr,
                result,
                obj_base_word_0_before,
                obj_base_word_1_before,
                obj_base_word_2_before,
                obj_base_word_3_before,
                obj_base_word_0_after,
                obj_base_word_1_after,
                obj_base_word_2_after,
                obj_base_word_3_after,
                primary_0_before,
                primary_1_before,
                primary_2_before,
                primary_0_after,
                primary_1_after,
                primary_2_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823B9700) {
    PPC_FUNC_PROLOGUE();
    const uint32_t obj_ptr = ctx.r3.u32;
    const uint32_t desc_ptr = ctx.r4.u32;
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t desc_word_0_before = LoadGuestWord(base, desc_ptr, 0);
    const uint32_t desc_word_1_before = LoadGuestWord(base, desc_ptr, 4);
    const uint32_t desc_word_2_before = LoadGuestWord(base, desc_ptr, 8);
    const uint32_t desc_word_3_before = LoadGuestWord(base, desc_ptr, 12);
    const uint32_t obj_word_0_before = LoadGuestWord(base, obj_ptr, 0);
    const uint32_t obj_word_1_before = LoadGuestWord(base, obj_ptr, 4);
    const uint32_t obj_word_2_before = LoadGuestWord(base, obj_ptr, 8);
    const uint32_t obj_word_3_before = LoadGuestWord(base, obj_ptr, 12);
    const uint32_t desc_byte_5_before =
        desc_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(desc_ptr + 5)) : 0;
    const uint32_t desc_byte_6_before =
        desc_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(desc_ptr + 6)) : 0;
    const uint32_t desc_target_before = LoadGuestWord(base, desc_ptr, 0);

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint64_t trace_count =
            g_movie_audio_gate_indirect_entry_trace_count.fetch_add(1, std::memory_order_relaxed) +
            1;
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 64 || (trace_count % 180) == 0);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate indirect entry obj={:08X} desc={:08X} caller_lr={:08X} "
                "desc_b5={:02X} desc_b6={:02X} desc_target={:08X} "
                "obj_head=[{:08X} {:08X} {:08X} {:08X}] "
                "desc_head=[{:08X} {:08X} {:08X} {:08X}]",
                obj_ptr,
                desc_ptr,
                caller_lr,
                desc_byte_5_before,
                desc_byte_6_before,
                desc_target_before,
                obj_word_0_before,
                obj_word_1_before,
                obj_word_2_before,
                obj_word_3_before,
                desc_word_0_before,
                desc_word_1_before,
                desc_word_2_before,
                desc_word_3_before);
        }
    }

    __imp__rex_sub_823B9700(ctx, base);

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t desc_word_0_after = LoadGuestWord(base, desc_ptr, 0);
        const uint32_t desc_word_1_after = LoadGuestWord(base, desc_ptr, 4);
        const uint32_t desc_word_2_after = LoadGuestWord(base, desc_ptr, 8);
        const uint32_t desc_word_3_after = LoadGuestWord(base, desc_ptr, 12);
        const uint32_t desc_byte_5_after =
            desc_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(desc_ptr + 5)) : 0;
        const uint32_t desc_byte_6_after =
            desc_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(desc_ptr + 6)) : 0;
        const uint64_t result_trace_count =
            g_movie_audio_gate_indirect_result_trace_count.fetch_add(1, std::memory_order_relaxed) +
            1;
        const bool primary_movie_zero_path =
            caller_lr == 0x823B9F5C && desc_byte_5_before == 0x01 && desc_byte_6_before == 0x01 &&
            result == 0;
        const bool periodic_zero_trace =
            ac6::audio_policy::IsDeepTraceEnabled() && primary_movie_zero_path &&
            (result_trace_count <= 64 || (result_trace_count % 180) == 0);
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (periodic_zero_trace || result != 0 || desc_word_0_before != desc_word_0_after ||
             desc_word_1_before != desc_word_1_after || desc_word_2_before != desc_word_2_after ||
             desc_word_3_before != desc_word_3_after || desc_byte_5_before != desc_byte_5_after ||
             desc_byte_6_before != desc_byte_6_after);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate indirect result obj={:08X} desc={:08X} caller_lr={:08X} result={} "
                "periodic_zero={} "
                "desc_b5_before={:02X} desc_b6_before={:02X} desc_b5_after={:02X} "
                "desc_b6_after={:02X} desc_head_after=[{:08X} {:08X} {:08X} {:08X}]",
                obj_ptr,
                desc_ptr,
                caller_lr,
                result,
                periodic_zero_trace,
                desc_byte_5_before,
                desc_byte_6_before,
                desc_byte_5_after,
                desc_byte_6_after,
                desc_word_0_after,
                desc_word_1_after,
                desc_word_2_after,
                desc_word_3_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823B3BA8) {
    PPC_FUNC_PROLOGUE();
    const uint32_t movie_state_ptr = ctx.r3.u32;
    const uint32_t arg4 = ctx.r4.u32;
    const uint32_t arg5 = ctx.r5.u32;
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);
    const uint32_t voice_count =
        movie_state_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(movie_state_ptr + 56)) : 0;
    const uint32_t state_292 =
        movie_state_ptr != 0 ? static_cast<uint32_t>(PPC_LOAD_U8(movie_state_ptr + 292)) : 0;
    const uint32_t gate_ptr = LoadGuestWord(base, movie_state_ptr, 184);

    if (caller_lr == 0x823B9810 && voice_count == 3 && state_292 == 1 &&
        gate_ptr == 0xA1910000 && arg5 == 0x2042E258) {
        ac6::audio_policy::OnMovieGateDriverEntered(caller_lr, movie_state_ptr);
    }

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint64_t trace_count =
            g_movie_audio_gate_driver_entry_trace_count.fetch_add(1, std::memory_order_relaxed) +
            1;
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 48 || (trace_count % 120) == 0);
        if (should_trace) {
            const uint32_t lock_count = LoadGuestWord(base, movie_state_ptr + 296, 4);
            const uint32_t lock_owner = LoadGuestWord(base, movie_state_ptr + 296, 8);
            const uint32_t queue_ptr = LoadGuestWord(base, movie_state_ptr, 200);
            const uint32_t queue_count = LoadGuestWord(base, movie_state_ptr, 204);
            const uint32_t queue_busy = LoadGuestWord(base, movie_state_ptr, 208);
            const uint32_t gate_flags = LoadGuestWord(base, gate_ptr, 4);
            REXAPU_DEBUG(
                "AC6 movie gate driver entry state={:08X} gate={:08X} arg4={:08X} arg5={:08X} "
                "voice_count={} state_292={} lock_count={} lock_owner={:08X} queue_ptr={:08X} "
                "queue_count={} queue_busy={} gate_flags={:08X} caller_lr={:08X}",
                movie_state_ptr,
                gate_ptr,
                arg4,
                arg5,
                voice_count,
                state_292,
                lock_count,
                lock_owner,
                queue_ptr,
                queue_count,
                queue_busy,
                gate_flags,
                caller_lr);
        }
    }

    __imp__rex_sub_823B3BA8(ctx, base);
}

PPC_FUNC_IMPL(rex_sub_823AE3B8) {
    PPC_FUNC_PROLOGUE();
    const uint32_t gate_ptr = ctx.r3.u32;
    const uint32_t flags_before = LoadGuestWord(base, gate_ptr, 4);
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);

    __imp__rex_sub_823AE3B8(ctx, base);

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t flags_after = LoadGuestWord(base, gate_ptr, 4);
        const uint64_t trace_count =
            g_movie_audio_gate_ready_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 64 || result != 0 || flags_before != flags_after ||
             (trace_count % 240) == 0);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate ready gate={:08X} result={} flags_before={:08X} "
                "flags_after={:08X} caller_lr={:08X}",
                gate_ptr,
                result,
                flags_before,
                flags_after,
                caller_lr);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823AED08) {
    PPC_FUNC_PROLOGUE();
    const uint32_t gate_ptr = ctx.r3.u32;
    const uint32_t flags_before = LoadGuestWord(base, gate_ptr, 4);
    const uint32_t slot_count_before = LoadGuestWord(base, gate_ptr, 0);
    const uint32_t caller_lr = static_cast<uint32_t>(ctx.lr);

    __imp__rex_sub_823AED08(ctx, base);

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint32_t result = ctx.r3.u32;
        const uint32_t flags_after = LoadGuestWord(base, gate_ptr, 4);
        const uint32_t slot_count_after = LoadGuestWord(base, gate_ptr, 0);
        const uint64_t trace_count =
            g_movie_audio_gate_arm_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 48 || flags_before != flags_after || result != 0 ||
             (trace_count % 180) == 0);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate arm gate={:08X} result={} slots_before={} flags_before={:08X} "
                "slots_after={} flags_after={:08X} caller_lr={:08X}",
                gate_ptr,
                result,
                slot_count_before,
                flags_before,
                slot_count_after,
                flags_after,
                caller_lr);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823AEE50) {
    PPC_FUNC_PROLOGUE();
    const uint32_t gate_ptr = ctx.r3.u32;
    const uint32_t slot_count_before = LoadGuestWord(base, gate_ptr, 0);
    const uint32_t flags_before = LoadGuestWord(base, gate_ptr, 4);
    const uint32_t slots_base_before = LoadGuestWord(base, gate_ptr, 8);
    const uint16_t first_cookie_before =
        (slot_count_before != 0 && slots_base_before != 0)
            ? LoadGuestHalf(base, slots_base_before, 80)
            : 0;
    const uint32_t first_slot_word_before =
        (slot_count_before != 0 && slots_base_before != 0)
            ? LoadGuestWord(base, slots_base_before, 0)
            : 0;

    __imp__rex_sub_823AEE50(ctx, base);

    const uint32_t result = ctx.r3.u32;
    const uint32_t slot_count_after = LoadGuestWord(base, gate_ptr, 0);
    const uint32_t flags_after = LoadGuestWord(base, gate_ptr, 4);
    const uint32_t slots_base_after = LoadGuestWord(base, gate_ptr, 8);
    const uint16_t first_cookie_after =
        (slot_count_after != 0 && slots_base_after != 0)
            ? LoadGuestHalf(base, slots_base_after, 80)
            : 0;
    const uint32_t first_slot_word_after =
        (slot_count_after != 0 && slots_base_after != 0)
            ? LoadGuestWord(base, slots_base_after, 0)
            : 0;

    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint64_t trace_count =
            g_movie_audio_gate_poll_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 64 || result != 0 || flags_before != flags_after ||
             first_cookie_before != first_cookie_after || (trace_count % 240) == 0);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate poll gate={:08X} result={} slots_before={} flags_before={:08X} "
                "slots_base_before={:08X} first_cookie_before={:04X} "
                "first_slot_word_before={:08X} slots_after={} flags_after={:08X} "
                "slots_base_after={:08X} first_cookie_after={:04X} "
                "first_slot_word_after={:08X}",
                gate_ptr,
                result,
                slot_count_before,
                flags_before,
                slots_base_before,
                static_cast<uint32_t>(first_cookie_before),
                first_slot_word_before,
                slot_count_after,
                flags_after,
                slots_base_after,
                static_cast<uint32_t>(first_cookie_after),
                first_slot_word_after);
        }
    }
}

PPC_FUNC_IMPL(rex_sub_823AED88) {
    PPC_FUNC_PROLOGUE();
    const uint32_t gate_ptr = ctx.r3.u32;
    const uint32_t slot_count_before = LoadGuestWord(base, gate_ptr, 0);
    const uint32_t flags_before = LoadGuestWord(base, gate_ptr, 4);
    const auto started_at = std::chrono::steady_clock::now();

    __imp__rex_sub_823AED88(ctx, base);

    const auto finished_at = std::chrono::steady_clock::now();
    if (ac6::audio_policy::IsMovieAudioActive()) {
        const uint64_t trace_count =
            g_movie_audio_gate_wait_trace_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(finished_at - started_at).count();
        const uint32_t result = ctx.r3.u32;
        const uint32_t slot_count_after = LoadGuestWord(base, gate_ptr, 0);
        const uint32_t flags_after = LoadGuestWord(base, gate_ptr, 4);
        const bool should_trace =
            ac6::audio_policy::IsDeepTraceEnabled() &&
            (trace_count <= 48 || result != 0 || elapsed_ms >= 5.0 ||
             flags_before != flags_after || (trace_count % 120) == 0);
        if (should_trace) {
            REXAPU_DEBUG(
                "AC6 movie gate wait gate={:08X} result={} elapsed_ms={:.3f} "
                "slots_before={} flags_before={:08X} slots_after={} flags_after={:08X}",
                gate_ptr,
                result,
                elapsed_ms,
                slot_count_before,
                flags_before,
                slot_count_after,
                flags_after);
        }
    }
}
