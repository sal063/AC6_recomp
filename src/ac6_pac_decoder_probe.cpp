#include "ac6_pac_decoder_probe.h"

#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/memory.h>
#include <rex/ppc/types.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

bool EnvFlag(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] && std::string_view(value) != "0";
}

bool TraceEnabled() {
    static const bool enabled = [] {
        const bool work_items = EnvFlag("AC6_TRACE_PAC_WORK_ITEMS");
        const bool stacks = EnvFlag("AC6_TRACE_PAC_STACKS");
        // ac6_performance_mode forces log_level=error, which silences the
        // probe's REXFS_INFO output, the PAC dumper's diagnostic lines, and
        // the kernel hook's stack-trace REXKRNL_INFO lines. Lift the relevant
        // categories so those reach the log file when their env-var gate is on.
        if (work_items) {
            rex::SetCategoryLevel(rex::log::fs(), spdlog::level::info);
        }
        if (stacks) {
            rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::info);
        }
        return work_items;
    }();
    return enabled;
}

struct TargetState {
    uint32_t first_work_item = 0;
    uint64_t hit_count = 0;
};

std::mutex& Mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<uint32_t, TargetState>& Targets() {
    static std::unordered_map<uint32_t, TargetState> m;
    return m;
}

}  // namespace

void ac6PacWorkerDispatchHook(PPCRegister& r28, PPCRegister& ctr) {
    if (!TraceEnabled()) return;

    const uint32_t target = ctr.u32;
    const uint32_t work_item = r28.u32;

    bool first_sighting = false;
    uint64_t total = 0;
    size_t distinct = 0;
    {
        std::scoped_lock lock(Mutex());
        auto& targets = Targets();
        auto& slot = targets[target];
        if (slot.hit_count == 0) {
            slot.first_work_item = work_item;
            first_sighting = true;
        }
        slot.hit_count++;
        total = slot.hit_count;
        distinct = targets.size();
    }

    if (first_sighting) {
        REXFS_INFO(
            "[AC6 PAC WORKER] new dispatch target=0x{:08X} first_work_item=0x{:08X} "
            "(distinct_targets={})",
            target, work_item, distinct);
    } else if (total == 100 || total == 1000 || (total % 10000) == 0) {
        REXFS_INFO("[AC6 PAC WORKER] target=0x{:08X} hits={}", target, total);
    }
}

namespace {

struct L2TargetState {
    uint32_t first_r3 = 0;
    uint32_t first_r4 = 0;
    uint32_t first_r5 = 0;
    uint32_t first_r6 = 0;
    uint64_t hit_count = 0;
    // Up to N distinct (r5, r6) tuples observed for this target; the decoder's
    // slot will eventually be called with an r5 matching a known csize/usize.
    static constexpr size_t kMaxSamples = 12;
    std::vector<std::pair<uint32_t, uint32_t>> samples;
};

std::mutex& L2Mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<uint32_t, L2TargetState>& L2Targets() {
    static std::unordered_map<uint32_t, L2TargetState> m;
    return m;
}

}  // namespace

void ac6PacWorkerL2DispatchHook(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5,
                                PPCRegister& r6, PPCRegister& ctr) {
    if (!TraceEnabled()) return;

    const uint32_t target = ctr.u32;

    bool first_sighting = false;
    bool new_sample = false;
    uint64_t total = 0;
    size_t distinct = 0;
    size_t samples_count = 0;
    L2TargetState snapshot{};
    {
        std::scoped_lock lock(L2Mutex());
        auto& targets = L2Targets();
        auto& slot = targets[target];
        if (slot.hit_count == 0) {
            slot.first_r3 = r3.u32;
            slot.first_r4 = r4.u32;
            slot.first_r5 = r5.u32;
            slot.first_r6 = r6.u32;
            first_sighting = true;
        }
        slot.hit_count++;

        // Bounded distinct-(r5,r6) capture so the decoder's argument signature
        // becomes observable across later calls (first-sighting often catches
        // state-init zeros).
        if (slot.samples.size() < L2TargetState::kMaxSamples) {
            const std::pair<uint32_t, uint32_t> key{r5.u32, r6.u32};
            if (std::find(slot.samples.begin(), slot.samples.end(), key) ==
                slot.samples.end()) {
                slot.samples.push_back(key);
                new_sample = true;
            }
        }

        total = slot.hit_count;
        distinct = targets.size();
        samples_count = slot.samples.size();
        snapshot = slot;
    }

    if (first_sighting) {
        REXFS_INFO(
            "[AC6 PAC L2] new target=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} "
            "r6=0x{:08X} (distinct_l2_targets={})",
            target, snapshot.first_r3, snapshot.first_r4, snapshot.first_r5,
            snapshot.first_r6, distinct);
    } else if (new_sample) {
        REXFS_INFO(
            "[AC6 PAC L2 sample] target=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} "
            "r6=0x{:08X} (sample {} / {}, hits={})",
            target, r3.u32, r4.u32, r5.u32, r6.u32, samples_count,
            L2TargetState::kMaxSamples, total);
    } else if (total == 100 || total == 1000 || (total % 10000) == 0) {
        REXFS_INFO("[AC6 PAC L2] target=0x{:08X} hits={}", target, total);
    }
}

void ac6PacDecoderDumpHook(PPCRegister& r4, PPCRegister& r10, PPCRegister& r11,
                           PPCRegister& r31) {
    auto* memory = REX_KERNEL_MEMORY();
    if (!memory) return;

    auto load_u8 = [memory](uint32_t va) -> uint8_t {
        if (!memory->LookupHeap(va)) return 0;
        return *static_cast<const uint8_t*>(memory->TranslateVirtual(va));
    };
    auto load_u32_be = [memory](uint32_t va) -> uint32_t {
        if (va > UINT32_MAX - 3) return 0;
        if (!memory->LookupHeap(va) || !memory->LookupHeap(va + 3)) return 0;
        return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual(va));
    };

    const uint8_t codec = load_u8(r11.u32 + 1);
    const uint32_t csize = load_u32_be(r11.u32 + 8);
    const uint32_t usize = load_u32_be(r11.u32 + 12);
    const uint32_t source_offset = load_u32_be(r31.u32 + 22888);

    if (usize == 0 || r4.u32 == 0 || r4.u32 > UINT32_MAX - usize) return;
    if (!memory->LookupHeap(r4.u32) || !memory->LookupHeap(r4.u32 + usize - 1)) return;
    const auto* host = memory->TranslateVirtual<const uint8_t*>(r4.u32);
    if (!host) return;

    Ac6DumpPacDecodedEntry(static_cast<uint16_t>(r10.u32 & 0xFFFFu),
                           codec, csize, usize, source_offset, host);
}
