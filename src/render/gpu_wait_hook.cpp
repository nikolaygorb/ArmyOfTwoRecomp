// gpu_wait_hook.cpp - stops the render thread from busy-spinning while it waits
// for the emulated GPU.
//
// sub_82A50708 is the XDK's BlockUntilRingSpace(dev, bytes): until the GPU read
// pointer (written back by the command processor thread) has freed enough of
// the command ring, it polls sub_82A428A8(state). The poll returns nonzero to
// keep waiting and zero once the wait is over or the 5000-tick no-progress
// timeout has been handled. The original poll is a few nops, so while the
// emulated GPU is behind the render thread spins flat out - tens of thousands
// of polls per frame in a level - and competes with the GPU command thread for
// cores. The poll's result and timeout logic are untouched; only the pause
// between polls changes.
//
// sub_82A428A8 overrides the weak recompiled symbol; __imp__sub_82A428A8 is the
// untouched original.

#include <chrono>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "armyoftworecomp_pch.h"

REXCVAR_DEFINE_INT32(ao2_gpu_wait_mode, 1, "Gameplay",
                     "Pause between polls while the game waits for free command-buffer space: "
                     "0 = busy spin (original), 1 = yield the core, 2 = sleep 200us")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82A428A8);

REX_HOOK_RAW(sub_82A428A8)
{
  __imp__sub_82A428A8(ctx, base);
  if (ctx.r3.u32 == 0)
  {
    return;
  }
  switch (REXCVAR_GET(ao2_gpu_wait_mode))
  {
  case 1:
    std::this_thread::yield();
    break;
  case 2:
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    break;
  default:
    break;
  }
}
