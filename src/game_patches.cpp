#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "game_cvars.h"
#include "game_constants.h"
#include "game_patches.h"

namespace game_patches
{
    bool IsEnabled(PatchId patch)
    {
        switch (patch)
        {
        case PatchId::FpsUnlock:
            return REXCVAR_GET(ao2_fps_unlock);
        case PatchId::DisableMsaa:
            return REXCVAR_GET(ao2_disable_msaa);
        case PatchId::AnisotropicFiltering16x:
            return REXCVAR_GET(ao2_anisotropic_16x);
        }
        return false;
    }
} // namespace game_patches

namespace
{
    // Writes through the guest heap's own Protect()/TranslateVirtual(), rather
    // than raw host-pointer arithmetic + rex::memory::Protect(). Some platforms
    // (Linux) don't support combined writable+executable pages, so the guest
    // heap keeps a separate write-view mapping for code pages; writing to
    // `virtual_membase() + address` directly bypasses that view and segfaults.
    bool WriteGuestBytes(uint32_t address, const uint8_t *bytes, uint32_t size)
    {
        if (!bytes || !size)
        {
            REXLOG_WARN("Cannot apply game patch at guest address {:08X}: empty patch data", address);
            return false;
        }

        auto *rt = rex::Runtime::instance();
        auto *memory = rt ? rt->memory() : nullptr;
        auto *heap = memory ? memory->LookupHeap(address) : nullptr;
        if (!heap)
        {
            REXLOG_WARN("Cannot apply game patch at guest address {:08X}: no guest heap", address);
            return false;
        }

        const uint64_t last_address = static_cast<uint64_t>(address) + size - 1;
        if (last_address > UINT32_MAX ||
            address / heap->page_size() != last_address / heap->page_size())
        {
            REXLOG_WARN("Cannot apply game patch at guest address {:08X}: range crosses a guest page",
                        address);
            return false;
        }

        constexpr uint32_t writable = rex::memory::kMemoryProtectRead |
                                      rex::memory::kMemoryProtectWrite;
        uint32_t old_protect{};
        if (!heap->Protect(address, size, writable, &old_protect))
        {
            REXLOG_WARN("Cannot make guest range {:08X}+{:X} writable for game patch", address, size);
            return false;
        }

        std::memcpy(memory->TranslateVirtual(address), bytes, size);
        if (!heap->Protect(address, size, old_protect, nullptr))
        {
            REXLOG_WARN("Cannot restore guest protection for game patch at {:08X}", address);
            return false;
        }
        return true;
    }

    // Ported from xenia-canary game-patches for Army of Two (4541084C)
    // https://github.com/xenia-canary/game-patches/blob/main/patches/4541084C%20-%20Army%20of%20Two%20(Europe).patch.toml

    // "Black Shading Fix" - disables MSAA.
    void ApplyDisableMsaaPatch()
    {
        if (!game_patches::IsEnabled(game_patches::PatchId::DisableMsaa))
        {
            return;
        }

        const auto patch = GameConstants::PatchConstants::DisableMsaa();
        uint8_t value = static_cast<uint8_t>(patch.value);
        WriteGuestBytes(static_cast<uint32_t>(patch.address), &value, 1);
    }
} // namespace

namespace game_patches
{
    void ApplyEnabledPatches()
    {
        ApplyDisableMsaaPatch();
    }
} // namespace game_patches
