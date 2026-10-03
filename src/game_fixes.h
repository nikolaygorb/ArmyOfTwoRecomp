// game_fixes.h - guest-function overrides
//
// Included once, from src/main.cpp, after the generated init header. Every
// definition here is a strong `extern "C"` symbol that replaces the weak alias
// codegen emits for the same name (DEFINE_REX_FUNC in
// generated/default/armyoftworecomp_pch.h):
//
//   __imp__sub_82XXXXXX  the recompiled body (always callable)
//   sub_82XXXXXX         weak alias -> __imp__ (what call sites and the
//                        dispatch table reference)
//
// generated/ is never touched. One override per guest function.
#pragma once

#include <atomic>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>

namespace game_fixes
{
  // ---------------------------------------------------------------------------
  // 1. Serialize XInput calls
  // ---------------------------------------------------------------------------
  // rex::input::InputSystem has no lock: GetState / GetCapabilities / SetState
  // each re-enumerate the devices and rebuild InputSystem::devices_
  // (std::vector<DeviceInfo>, with heap-allocated name/guid strings). The game
  // reaches the XamInput* imports from several call sites. With a pad
  // connected it crashed once, ~45 s in, with ntdll heap failure 0xC0000374,
  // type 8 (block not busy = double free) in
  // InputSystem::RefreshDevices <- GetState <- XamInputGetState. The race is
  // timing-dependent: most runs never hit it.
  //
  // The three XDK wrappers below are the only guest code that calls the
  // XamInput* imports, so one lock around them serializes every guest call.
  inline std::mutex g_input_mutex;
  inline std::atomic<uint32_t> g_input_contention{0};

  class InputLock
  {
  public:
    InputLock()
    {
      if (!g_input_mutex.try_lock())
      {
        if (g_input_contention.fetch_add(1, std::memory_order_relaxed) == 0)
        {
          REXLOG_WARN("[fix] XamInput called from two guest threads at once; serialized (logged once)");
        }
        g_input_mutex.lock();
      }
    }
    ~InputLock() { g_input_mutex.unlock(); }
    InputLock(const InputLock &) = delete;
    InputLock &operator=(const InputLock &) = delete;
  };
} // namespace game_fixes

// XInputGetCapabilities: b XamInputGetCapabilities
REX_HOOK_RAW(sub_82A3AD68)
{
  game_fixes::InputLock lock;
  __imp__sub_82A3AD68(ctx, base);
}

// XInputGetState: r5 = r4, r4 = 1, b XamInputGetState
REX_HOOK_RAW(sub_82A3AD70)
{
  game_fixes::InputLock lock;
  __imp__sub_82A3AD70(ctx, base);
}

// XInputSetState (rumble): XamInputGetCapabilities, then XamInputSetState
REX_HOOK_RAW(sub_82A3AD80)
{
  game_fixes::InputLock lock;
  __imp__sub_82A3AD80(ctx, base);
}
