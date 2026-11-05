// Mid-asm hooks replacing the xenia-canary code patches for Army of Two.
// Declared as [[midasm_hook]] entries in patches.toml. Every hook returns true
// when its patch is enabled (the generated code then skips the original
// instruction and jumps to jump_address_on_true) and false otherwise.
#include <algorithm>
#include <cstdint>

#include <rex/ppc/context.h>

#include "game_cvars.h"

// "Unlock FPS": nop for lfs f0,21288(r11) in the frame limiter.
bool Ao2FpsUnlockLimiterHook(PPCFPSCRRegister &fpscr)
{
  if (!REXCVAR_GET(ao2_fps_unlock))
    return false;
  // The skipped lfs would have switched the FPU to non-flush mode.
  fpscr.disableFlushMode();
  return true;
}

// "Unlock FPS": li r10,<mode> instead of li r10,2, where mode is
// ao2_fps_unlock_mode (0 = unlimited, 1 = 60 FPS, 2 = 30 FPS).
bool Ao2FpsUnlockIntervalHook(PPCRegister &r10)
{
  if (!REXCVAR_GET(ao2_fps_unlock))
    return false;
  r10.u64 = uint64_t(std::clamp(REXCVAR_GET(ao2_fps_unlock_mode), 0, 2));
  return true;
}

// "16x Anisotropic Filtering": li r5,16 instead of mr r5,r30.
bool Ao2Anisotropic16xHook(PPCRegister &r5)
{
  if (!REXCVAR_GET(ao2_anisotropic_16x))
    return false;
  r5.u64 = 16;
  return true;
}
