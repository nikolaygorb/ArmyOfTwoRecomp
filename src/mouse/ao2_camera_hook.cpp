#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "armyoftworecomp_pch.h"
#include "game_constants.h"
#include "mouse_look.h"

REXCVAR_DEFINE_BOOL(ao2_mouse_look_zoom_scaling, true, "Mouse",
                    "Scale mouse sensitivity with the camera's field of view, so zoomed aiming "
                    "(precision, sniping) turns by the same amount of screen per pixel")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(ao2_mouse_look_zoom_ref_fov, 0.0, "Mouse",
                      "Horizontal FOV (degrees) at which the mouse sensitivity is unscaled. 0 = "
                      "the widest FOV seen this session")
    .range(0.0, 150.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(ao2_mouse_look_log, false, "Mouse",
                    "Log the mouse camera hook (first calls, then every 240th) - needs log_level "
                    "info or lower")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

extern "C" REX_FUNC(__imp__sub_822DEAF0);

namespace
{
  // Unreal rotator: 65536 units per full turn.
  constexpr double kUnitsPerRadian = 65536.0 / (2.0 * 3.14159265358979323846);
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

  constexpr uint32_t kVtGetController = 876;
  constexpr uint32_t kVtLimitViewRotation = 880;
  constexpr uint32_t kVtIsLocalPlayerController = 816;
  constexpr uint32_t kVtGetCameraActor = 268;
  constexpr uint32_t kPawnCameraManager = 1736;
  constexpr uint32_t kCameraAspect = 456;
  constexpr uint32_t kCameraHorizontalFov = 460;

  // Where vtables live (the image) and where code lives, to refuse anything a
  // stray field or a different class layout would turn into a wild call.
  constexpr uint32_t kImageBegin = 0x82000000;
  constexpr uint32_t kImageEnd = 0x831A0000;

  bool GuestReadable(uint32_t address, uint32_t size)
  {
    if (!address || (address & 3))
    {
      return false;
    }
    auto *runtime = rex::Runtime::instance();
    auto *memory = runtime ? runtime->memory() : nullptr;
    auto *heap = memory ? memory->LookupHeap(address) : nullptr;
    return heap && heap->QueryRangeAccess(address, address + size - 1) !=
                       rex::memory::PageAccess::kNoAccess;
  }

  // Calls the argument-less virtual at byte offset `slot` of `object`'s vtable
  // and returns r3. nullopt (and nothing called) when anything looks off.
  std::optional<uint32_t> CallVirtual(PPCContext &ctx, uint8_t *base, uint32_t object, uint32_t slot)
  {
    if (!GuestReadable(object, 4))
    {
      return std::nullopt;
    }
    const uint32_t vtable = REX_LOAD_U32(object);
    if (vtable < kImageBegin || vtable >= kImageEnd || !GuestReadable(vtable + slot, 4))
    {
      return std::nullopt;
    }
    const uint32_t function = REX_LOAD_U32(vtable + slot);
    if (function < GameConstants::kCodeBase || function >= GameConstants::kCodeEnd ||
        (function & 3))
    {
      return std::nullopt;
    }
    ctx.r3.u64 = object;
    REX_CALL_INDIRECT_FUNC(function);
    return ctx.r3.u32;
  }

  float GuestFloat(uint8_t *base, uint32_t address)
  {
    return std::bit_cast<float>(static_cast<uint32_t>(REX_LOAD_U32(address)));
  }

  struct Rotator
  {
    int32_t pitch;
    int32_t yaw;
    int32_t roll;
  };

  Rotator LoadRotator(uint8_t *base, uint32_t address)
  {
    return Rotator{static_cast<int32_t>(REX_LOAD_U32(address)),
                   static_cast<int32_t>(REX_LOAD_U32(address + 4)),
                   static_cast<int32_t>(REX_LOAD_U32(address + 8))};
  }

  void StoreRotator(uint8_t *base, uint32_t address, const Rotator &r)
  {
    REX_STORE_U32(address, static_cast<uint32_t>(r.pitch));
    REX_STORE_U32(address + 4, static_cast<uint32_t>(r.yaw));
    REX_STORE_U32(address + 8, static_cast<uint32_t>(r.roll));
  }

  // Angles are 16-bit, so a difference is only meaningful modulo 65536.
  int32_t Wrap16(int32_t v)
  {
    return static_cast<int16_t>(static_cast<uint16_t>(v));
  }

  // The unit-less "unzoomed" reference: the widest horizontal FOV seen.
  double ZoomScale(PPCContext &ctx, uint8_t *base, uint32_t pawn, double &fov_out)
  {
    static double widest_fov = 0.0;
    fov_out = 0.0;
    if (!REXCVAR_GET(ao2_mouse_look_zoom_scaling) || !GuestReadable(pawn + kPawnCameraManager, 4))
    {
      return 1.0;
    }
    const uint32_t manager = REX_LOAD_U32(pawn + kPawnCameraManager);
    const auto camera = CallVirtual(ctx, base, manager, kVtGetCameraActor);
    if (!camera || !GuestReadable(*camera + kCameraAspect, 8))
    {
      return 1.0;
    }
    const double fov = GuestFloat(base, *camera + kCameraHorizontalFov);
    if (!std::isfinite(fov) || fov < 10.0 || fov > 150.0)
    {
      return 1.0;
    }
    fov_out = fov;

    if (fov > widest_fov && fov <= 120.0)
    {
      widest_fov = fov;
    }
    double reference = REXCVAR_GET(ao2_mouse_look_zoom_ref_fov);
    if (reference <= 0.0)
    {
      reference = widest_fov;
    }
    if (reference < 10.0)
    {
      return 1.0;
    }
    const double scale = std::tan(fov * kDegToRad * 0.5) / std::tan(reference * kDegToRad * 0.5);
    return std::clamp(scale, 0.05, 1.0);
  }

  class PersistenceCheck
  {
  public:
    using Clock = std::chrono::steady_clock;

    void Reset() { valid_ = false; }

    // Returns true once there is enough evidence that the writes are discarded.
    bool Observe(const Rotator &incoming, Clock::time_point now)
    {
      bool discarded = false;
      // Two calls inside one frame (microseconds apart) say nothing about
      // whether the game kept last frame's write.
      if (valid_ && now - last_time_ > std::chrono::microseconds(1500))
      {
        const double dp = Wrap16(incoming.pitch - written_.pitch);
        const double dy = Wrap16(incoming.yaw - written_.yaw);
        sum_turn_sq_ += double(turn_pitch_) * turn_pitch_ + double(turn_yaw_) * turn_yaw_;
        sum_cross_ += dp * turn_pitch_ + dy * turn_yaw_;
        if (sum_turn_sq_ > kEvidence)
        {
          const double ratio = sum_cross_ / sum_turn_sq_;
          if (REXCVAR_GET(ao2_mouse_look_log))
          {
            REXLOG_INFO("mouse hook persistence ratio {:.2f} (~0 = camera keeps the writes, "
                        "~-1 = discards them)",
                        ratio);
          }
          discarded = ratio < -0.5;
          sum_turn_sq_ = sum_cross_ = 0.0;
        }
      }
      return discarded;
    }

    void Record(const Rotator &written, int32_t turn_pitch, int32_t turn_yaw, Clock::time_point now)
    {
      valid_ = true;
      written_ = written;
      turn_pitch_ = turn_pitch;
      turn_yaw_ = turn_yaw;
      last_time_ = now;
    }

  private:
    // Squared units of mouse turn to look at before judging: roughly two
    // seconds of ordinary aiming.
    static constexpr double kEvidence = 1.5e6;

    bool valid_ = false;
    Rotator written_{};
    int32_t turn_pitch_ = 0;
    int32_t turn_yaw_ = 0;
    Clock::time_point last_time_{};
    double sum_turn_sq_ = 0.0;
    double sum_cross_ = 0.0;
  };

  void ApplyMouseTurn(PPCContext &ctx, uint8_t *base, uint32_t pawn, uint32_t result)
  {
    using Clock = std::chrono::steady_clock;
    static PersistenceCheck persistence;
    static uint32_t log_counter = 0;

    auto &look = ao2::mouse::MouseLook::Get();
    if (!look.Enabled() || !GuestReadable(result, 12))
    {
      persistence.Reset();
      return;
    }

    // Own frame for the calls below: the callees use the stack below r1.
    struct StackFrame
    {
      PPCContext &ctx;
      uint32_t saved;
      explicit StackFrame(PPCContext &c) : ctx(c), saved(c.r1.u32) { ctx.r1.u32 = saved - 0x100; }
      ~StackFrame() { ctx.r1.u32 = saved; }
    } frame(ctx);

    // Only the local human's pawn: AI partners and enemies run the same native.
    const auto controller = CallVirtual(ctx, base, pawn, kVtGetController);
    if (!controller || !*controller)
    {
      return;
    }
    const auto is_local = CallVirtual(ctx, base, *controller, kVtIsLocalPlayerController);
    if (!is_local || !(*is_local & 0xFF))
    {
      return;
    }

    const Rotator in = LoadRotator(base, result);
    const auto now = Clock::now();
    if (persistence.Observe(in, now))
    {
      persistence.Reset();
      look.Abandon("the camera discards what the hook writes (LimitViewRotation is not what "
                   "drives the view in this state)");
      return;
    }

    double fov = 0.0;
    const double zoom = ZoomScale(ctx, base, pawn, fov);
    const auto turn = look.Consume(kUnitsPerRadian, zoom);
    if (!turn || (turn->yaw == 0 && turn->pitch == 0))
    {
      // Nothing written; the next frame's comparison starts from what the game
      // itself produced.
      persistence.Record(in, 0, 0, now);
      if (turn && REXCVAR_GET(ao2_mouse_look_log) && (log_counter++ < 30 || log_counter % 240 == 0))
      {
        REXLOG_INFO("mouse hook: pawn {:08X} idle, in p={} y={} fov {:.1f} zoom {:.2f}", pawn,
                    in.pitch, in.yaw, fov, zoom);
      }
      return;
    }

    const Rotator wanted{in.pitch + turn->pitch, in.yaw + turn->yaw, in.roll};
    Rotator out = wanted;
    const uint32_t scratch = ctx.r1.u32 + 0x60;
    const uint32_t vtable = REX_LOAD_U32(pawn);
    const uint32_t limiter =
        GuestReadable(vtable + kVtLimitViewRotation, 4) ? REX_LOAD_U32(vtable + kVtLimitViewRotation)
                                                         : 0;
    if (limiter >= GameConstants::kCodeBase && limiter < GameConstants::kCodeEnd && !(limiter & 3))
    {
      ctx.r3.u64 = scratch;
      ctx.r4.u64 = pawn;
      ctx.r5.u64 = (uint64_t(uint32_t(wanted.pitch)) << 32) | uint32_t(wanted.yaw);
      ctx.r6.u64 = uint64_t(uint32_t(wanted.roll)) << 32;
      REX_CALL_INDIRECT_FUNC(limiter);
      const uint32_t limited = ctx.r3.u32;
      if (GuestReadable(limited, 12))
      {
        out = LoadRotator(base, limited);
      }
    }
    StoreRotator(base, result, out);

    // What actually got through, after the limiter, is what the next frame is
    // expected to hand back.
    persistence.Record(out, Wrap16(out.pitch - in.pitch), Wrap16(out.yaw - in.yaw), now);

    if (REXCVAR_GET(ao2_mouse_look_log) && (log_counter++ < 30 || log_counter % 240 == 0))
    {
      REXLOG_INFO("mouse hook: pawn {:08X} ctrl {:08X} in p={} y={} turn p={} y={} out p={} y={} "
                  "fov {:.1f} zoom {:.2f}",
                  pawn, *controller, in.pitch, in.yaw, turn->pitch, turn->yaw, out.pitch, out.yaw,
                  fov, zoom);
    }
  }
} // namespace

// AAO2CharacterNative::execLimitViewRotation
extern "C" REX_FUNC(sub_822DEAF0)
{
  const uint32_t pawn = ctx.r3.u32;
  const uint32_t result = ctx.r5.u32;
  __imp__sub_822DEAF0(ctx, base);
  ApplyMouseTurn(ctx, base, pawn, result);
}
