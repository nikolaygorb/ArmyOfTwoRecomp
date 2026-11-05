// game_constants.h - Game-specific constants
#pragma once

#include <cstdint>

namespace GameConstants
{
  constexpr uint32_t kCodeBase = 0x82210000;
  constexpr uint32_t kCodeEnd = 0x8309C604;
}

namespace GameConstants::PatchConstants
{
  struct Patch
  {
    std::uintptr_t address;
    std::uint32_t value;
  };

  // Ported from xenia-canary game-patches for Army of Two retail (4541084C).
  // Only data patches: the code patches are mid-asm hooks (patches.toml).

  // "Black Shading Fix" - disables MSAA.
  constexpr Patch DisableMsaa()
  {
    return Patch{
        0x830B6FE7,
        0x04};
  }
}