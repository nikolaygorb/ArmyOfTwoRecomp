#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>

#include "mouse_look.h"

REXCVAR_DEFINE_BOOL(ao2_mouse_zoom_scaling, true, "Mouse",
                    "Scale mouse look with the camera's field of view, so zoomed aiming turns "
                    "the picture by the same amount per mouse count")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(ao2_mouse_zoom_sensitivity, 1.0, "Mouse",
                      "Extra mouse look factor while zoomed in (camera FOV under 70 deg); the "
                      "turn is already scaled to the FOV")
    .range(0.01, 10.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(ao2_mouse_look_log, false, "Mouse",
                    "Log the mouse look hooks (first calls, then every 240th) - needs log_level "
                    "info or lower")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

extern "C" REX_FUNC(__imp__sub_8235F0A8);
extern "C" REX_FUNC(__imp__sub_822F1FA8);

namespace
{
  constexpr double kRotationUnitsPerDegree = 65536.0 / 360.0;
  // On-foot camera FOV, the one ao2_mouse_sensitivity is set for.
  constexpr double kBaseFov = 70.0;
  // A larger pitch step per frame could wrap past +-180 deg and land on the wrong limit.
  constexpr float kMaxPitchStep = 8192.0f;

  // Globals.
  constexpr uint32_t kGEngine = 0x8311F58C;     // +772 GameViewportClient
  constexpr uint32_t kGWorld = 0x83122588;      // +40 its package
  constexpr uint32_t kGObjObjects = 0x8311471C; // TArray<UObject*>: data, count
  constexpr uint32_t kFNameNames = 0x83101004;  // FName::Names data: entry +16 = chars
  // AAO2CameraActor::StaticClass(), cached by its IsA cast (sub_822FDA58).
  constexpr uint32_t kCameraActorClass = 0x830E1064;

  // UObject / UStruct.
  constexpr uint32_t kObjOuter = 40;
  constexpr uint32_t kObjName = 44;
  constexpr uint32_t kObjClass = 52;
  constexpr uint32_t kStructSuper = 60;

  // AAO2PlayerControllerNative.
  constexpr uint32_t kCtrlPawn = 452;
  constexpr uint32_t kCtrlCamera = 832;
  constexpr uint32_t kCtrlAimingGraph = 2004;
  constexpr uint32_t kCtrlLookHolder = 2024;
  constexpr uint32_t kCtrlInvertY = 2772; // top bit: the in-game Invert Y option
  constexpr uint32_t kCtrlViewReturn = 2868;
  constexpr uint32_t kHolderRotation = 3616;
  constexpr uint32_t kCameraFov = 460;

  // GameViewportClient vtable +268: (client, controller id) -> ULocalPlayer; +64 its actor.
  constexpr uint32_t kVtLocalPlayerById = 268;
  constexpr uint32_t kLocalPlayerActor = 64;

  uint32_t g_log_counter = 0;

  uint32_t LoadU32(const uint8_t *base, uint32_t address)
  {
    uint32_t v;
    std::memcpy(&v, base + address, 4);
    return std::byteswap(v);
  }

  void StoreU32(uint8_t *base, uint32_t address, uint32_t value)
  {
    const uint32_t v = std::byteswap(value);
    std::memcpy(base + address, &v, 4);
  }

  float LoadF32(const uint8_t *base, uint32_t address)
  {
    return std::bit_cast<float>(LoadU32(base, address));
  }

  void StoreF32(uint8_t *base, uint32_t address, float f)
  {
    StoreU32(base, address, std::bit_cast<uint32_t>(f));
  }

  const char *NameOf(const uint8_t *base, uint32_t object)
  {
    const uint32_t names = LoadU32(base, kFNameNames);
    const uint32_t entry =
        names && object ? LoadU32(base, names + LoadU32(base, object + kObjName) * 4) : 0;
    return entry ? reinterpret_cast<const char *>(base + entry + 16) : "";
  }

  // Calls a guest function with (r3, r4) and returns r3; the context is
  // restored afterwards.
  uint32_t CallGuest(PPCContext &ctx, uint8_t *base, uint32_t function, uint32_t r3, uint32_t r4)
  {
    const PPCContext saved = ctx;
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    rex::runtime::ResolveIndirectFunction(function)(ctx, base);
    const uint32_t result = ctx.r3.u32;
    ctx = saved;
    return result;
  }

  // The PlayerController of a controller id, as the engine routes input.
  uint32_t ControllerOf(PPCContext &ctx, uint8_t *base, uint32_t controller_id)
  {
    const uint32_t engine = LoadU32(base, kGEngine);
    const uint32_t client = engine ? LoadU32(base, engine + 772) : 0;
    const uint32_t vtable = client ? LoadU32(base, client) : 0;
    if (!vtable)
    {
      return 0;
    }
    const uint32_t player =
        CallGuest(ctx, base, LoadU32(base, vtable + kVtLocalPlayerById), client, controller_id);
    return player ? LoadU32(base, player + kLocalPlayerActor) : 0;
  }

  // The main menu and the lobby: the world of the package Shell. Their camera
  // is a player controller's too and UpdateLookingAction turns it, but the
  // mouse is not for it there.
  bool InShell(const uint8_t *base)
  {
    const uint32_t world = LoadU32(base, kGWorld);
    const uint32_t package = world ? LoadU32(base, world + kObjOuter) : 0;
    return package && std::strcmp(NameOf(base, package), "Shell") == 0;
  }

  // The keyboard and mouse belong to the first player (mnk feeds guest user 0).
  // Split-screen runs the hooks for every local player: only that one's takes
  // the mouse. While it has no character (the main menu's camera is another
  // controller's) the one there is takes it.
  bool TakesMouse(PPCContext &ctx, uint8_t *base, uint32_t controller)
  {
    const uint32_t owner = ControllerOf(ctx, base, 0);
    return !owner || owner == controller || !LoadU32(base, owner + kCtrlLookHolder);
  }

  // FOV of the player's camera in degrees, 0 when there is no camera actor.
  float CameraFov(const uint8_t *base, uint32_t controller)
  {
    const uint32_t camera = LoadU32(base, controller + kCtrlCamera);
    const uint32_t want = LoadU32(base, kCameraActorClass);
    if (!camera || !want)
    {
      return 0.0f;
    }
    uint32_t cls = LoadU32(base, camera + kObjClass);
    for (int depth = 0; cls && depth < 32; ++depth, cls = LoadU32(base, cls + kStructSuper))
    {
      if (cls == want)
      {
        return LoadF32(base, camera + kCameraFov);
      }
    }
    return 0.0f;
  }

  // Mouse look factor for the current zoom: the picture moves under the mouse
  // at the same rate at the screen center whatever the zoom.
  double ZoomScale(float fov)
  {
    if (!REXCVAR_GET(ao2_mouse_zoom_scaling) || !(fov > 1.0f && fov < 170.0f))
    {
      return 1.0;
    }
    constexpr double kHalfDegree = 3.14159265358979323846 / 360.0;
    double scale = std::tan(fov * kHalfDegree) / std::tan(kBaseFov * kHalfDegree);
    if (fov < kBaseFov)
    {
      scale *= REXCVAR_GET(ao2_mouse_zoom_sensitivity);
    }
    return scale;
  }

  struct UnitTurn
  {
    float yaw;
    float pitch; // + = look up
  };

  // The mouse turn of this look update in rotation units, with the zoom and
  // the in-game Invert Y option applied. nullopt while the pointer is not
  // ours yet or nothing moved.
  std::optional<UnitTurn> TakeTurn(const uint8_t *base, uint32_t controller, float &fov)
  {
    fov = CameraFov(base, controller);
    const auto turn = ao2::mouse::MouseLook::Get().Consume(ZoomScale(fov));
    if (!turn || (turn->yaw == 0.0 && turn->pitch == 0.0))
    {
      return std::nullopt;
    }
    const bool invert = (LoadU32(base, controller + kCtrlInvertY) & 0x80000000) != 0;
    const double pitch = (invert ? turn->pitch : -turn->pitch) * kRotationUnitsPerDegree;
    return UnitTurn{float(turn->yaw * kRotationUnitsPerDegree),
                    std::clamp(float(pitch), -kMaxPitchStep, kMaxPitchStep)};
  }

  // After a camera move of its own (holding RB looks at the partner) the game
  // turns the view back to the rotation it had before: controller +2868 top
  // bit, the rotation at +2872. UpdateLookingAction ends that return once the
  // stick turns the view by a rotation unit in a frame; a mouse turn written
  // straight into the rotation would keep being pulled back. The mouse ends it
  // like the stick.
  void EndViewReturn(uint8_t *base, uint32_t controller, const UnitTurn &turn)
  {
    const uint32_t flags = LoadU32(base, controller + kCtrlViewReturn);
    if ((flags & 0x80000000) && (std::fabs(turn.yaw) >= 1.0f || std::fabs(turn.pitch) >= 1.0f))
    {
      StoreU32(base, controller + kCtrlViewReturn, flags & 0x7FFFFFFF);
    }
  }

  bool ShouldLog()
  {
    return REXCVAR_GET(ao2_mouse_look_log) && (g_log_counter++ < 30 || g_log_counter % 240 == 0);
  }

  // ---------------------------------------------------------------------
  // Turrets
  // ---------------------------------------------------------------------

  // A turret's gun (and the view on it) follows the controller's rotation
  // through skeletal controls (AO2SkelControl_Turret: TurretRotate /
  // TurretPitch) at LagDegreesPerSecond = 360 at most: enough for the stick, a
  // speed limit under the mouse (and a gun that goes the long way round after
  // a fast half turn). The controls of a turret a player sits on follow at once.
  //
  // The controls are on the mesh of the turret the player's pawn is, or of the
  // vehicle the pawn is a seat of (the hovercraft's gunner: Actor.Base = the
  // vehicle).
  struct Turret
  {
    uint32_t pawn = 0;
    uint32_t vehicle = 0; // whose mesh has the controls, 0 = none found
    std::array<uint32_t, 8> controls{};
  };
  std::array<Turret, 4> g_turrets; // by pawn, the oldest is replaced
  size_t g_next_turret = 0;
  uint32_t g_turret_control_class = 0;

  void UnlagTurret(uint8_t *base, uint32_t pawn)
  {
    constexpr uint32_t kSkelComponent = 60; // SkelControlBase.SkelComponent
    constexpr uint32_t kLag = 232;          // UTSkelControl_TurretConstrained.LagDegreesPerSecond
    constexpr uint32_t kActorBase = 96;     // Actor.Base
    constexpr float kNoLag = 36000.0f;

    const Turret *known = nullptr;
    for (const Turret &t : g_turrets)
    {
      if (t.pawn == pawn)
      {
        known = &t;
      }
    }
    if (!known)
    {
      Turret t;
      t.pawn = pawn;
      const uint32_t seat_of = LoadU32(base, pawn + kActorBase);
      size_t found = 0;
      const uint32_t objects = LoadU32(base, kGObjObjects);
      const uint32_t count = LoadU32(base, kGObjObjects + 4);
      g_turret_control_class = 0;
      for (uint32_t i = 0; objects && i < count && found < t.controls.size(); ++i)
      {
        const uint32_t o = LoadU32(base, objects + i * 4);
        const uint32_t cls = o ? LoadU32(base, o + kObjClass) : 0;
        if (!cls || (g_turret_control_class && cls != g_turret_control_class))
        {
          continue;
        }
        if (!g_turret_control_class)
        {
          if (std::strcmp(NameOf(base, cls), "AO2SkelControl_Turret") != 0)
          {
            continue;
          }
          g_turret_control_class = cls;
        }
        // The component's Outer is the vehicle. The pawn's own controls first;
        // a seat's are its vehicle's.
        const uint32_t component = LoadU32(base, o + kSkelComponent);
        const uint32_t owner = component ? LoadU32(base, component + kObjOuter) : 0;
        if (owner && (owner == pawn || (owner == seat_of && t.vehicle != pawn)))
        {
          if (owner == pawn && t.vehicle != pawn)
          {
            found = 0;
          }
          t.vehicle = owner;
          t.controls[found++] = o;
        }
      }
      for (size_t i = found; i < t.controls.size(); ++i)
      {
        t.controls[i] = 0;
      }
      if (REXCVAR_GET(ao2_mouse_look_log))
      {
        REXLOG_INFO("mouse look: turret {:08X}: {} controls on {:08X}", pawn, found, t.vehicle);
      }
      g_turrets[g_next_turret] = t;
      known = &g_turrets[g_next_turret];
      g_next_turret = (g_next_turret + 1) % g_turrets.size();
    }
    for (const uint32_t o : known->controls)
    {
      // Still that vehicle's control (not an object freed and reused since).
      const uint32_t component = o && LoadU32(base, o + kObjClass) == g_turret_control_class
                                     ? LoadU32(base, o + kSkelComponent)
                                     : 0;
      if (component && LoadU32(base, component + kObjOuter) == known->vehicle &&
          LoadF32(base, o + kLag) != kNoLag)
      {
        StoreF32(base, o + kLag, kNoLag);
      }
    }
  }

  // The pawn is a vehicle its player drives: an SVehicle that is not a turret
  // and not a seat of another vehicle (UTWeaponPawn), by its class names.
  bool IsDrivenVehicle(const uint8_t *base, uint32_t pawn)
  {
    static std::map<uint32_t, bool> by_class;
    const uint32_t cls = LoadU32(base, pawn + kObjClass);
    if (const auto it = by_class.find(cls); it != by_class.end())
    {
      return it->second;
    }
    bool vehicle = false, other = false;
    for (uint32_t c = cls, depth = 0; c && depth < 24; c = LoadU32(base, c + kStructSuper), ++depth)
    {
      const char *name = NameOf(base, c);
      vehicle = vehicle || std::strcmp(name, "SVehicle") == 0;
      other = other || std::strstr(name, "Turret") || std::strcmp(name, "UTWeaponPawn") == 0;
    }
    if (by_class.size() > 256)
    {
      by_class.clear();
    }
    return by_class[cls] = vehicle && !other;
  }
} // namespace

// AAO2PlayerControllerNative::UpdateLookingAction (r3 = controller, f1 = dt).
extern "C" REX_FUNC(sub_8235F0A8)
{
  const uint32_t controller = ctx.r3.u32;
  const uint32_t holder = controller ? LoadU32(base, controller + kCtrlLookHolder) : 0;
  // Driving a vehicle (the controller's pawn is the vehicle, not the
  // character) this still runs, but the view is the vehicle's camera: a turn
  // added here shows nowhere. Not taking the mouse there leaves it to mnk.
  if (!ao2::mouse::MouseLook::Get().Enabled() || !holder ||
      LoadU32(base, controller + kCtrlPawn) != holder || InShell(base) ||
      !TakesMouse(ctx, base, controller))
  {
    __imp__sub_8235F0A8(ctx, base);
    return;
  }

  float fov = 0.0f;
  if (const auto turn = TakeTurn(base, controller, fov))
  {
    const uint32_t rotation = holder + kHolderRotation;
    const float pitch_in = LoadF32(base, rotation);
    const float yaw_in = LoadF32(base, rotation + 4);
    StoreF32(base, rotation, pitch_in + turn->pitch);
    StoreF32(base, rotation + 4, yaw_in + turn->yaw);
    EndViewReturn(base, controller, *turn);
    if (ShouldLog())
    {
      REXLOG_INFO("mouse look: ctrl {:08X} holder {:08X} dt {:.4f} fov {:.1f} turn y={:.1f} "
                  "p={:.1f} rot ({:.1f},{:.1f}) -> ({:.1f},{:.1f})",
                  controller, holder, ctx.f1.f64, fov, turn->yaw, turn->pitch, pitch_in, yaw_in,
                  pitch_in + turn->pitch, yaw_in + turn->yaw);
    }
  }
  __imp__sub_8235F0A8(ctx, base);
}

// UAO2AimingInputModel::execGetAnalogIntention2D (r3 = the aiming graph, r5 =
// result FVector): (input object, yaw intention, pitch intention, dt) -> this
// frame's turn from the right stick (x = yaw, y = pitch, rotation units).
// UpdateLookingAction calls the graph itself; scripts call it through this
// thunk where they turn the view themselves - on a turret or at a vehicle's
// gun, when the controller's pawn is not the character. The mouse turn is
// added to the stick's there.
extern "C" REX_FUNC(sub_822F1FA8)
{
  const uint32_t aim = ctx.r3.u32;
  const uint32_t result = ctx.r5.u32;
  __imp__sub_822F1FA8(ctx, base);
  if (!ao2::mouse::MouseLook::Get().Enabled() || !aim || !result || InShell(base))
  {
    return;
  }
  uint32_t controller = 0;
  for (uint32_t id = 0; id < 4 && !controller; ++id)
  {
    const uint32_t c = ControllerOf(ctx, base, id);
    if (c && LoadU32(base, c + kCtrlAimingGraph) == aim)
    {
      controller = c;
    }
  }
  const uint32_t pawn = controller ? LoadU32(base, controller + kCtrlPawn) : 0;
  const uint32_t holder = controller ? LoadU32(base, controller + kCtrlLookHolder) : 0;
  if (!pawn || !holder || pawn == holder || IsDrivenVehicle(base, pawn) ||
      !TakesMouse(ctx, base, controller))
  {
    return;
  }
  UnlagTurret(base, pawn);

  float fov = 0.0f;
  if (const auto turn = TakeTurn(base, controller, fov))
  {
    const float yaw_in = LoadF32(base, result);
    const float pitch_in = LoadF32(base, result + 4);
    StoreF32(base, result, yaw_in + turn->yaw);
    StoreF32(base, result + 4, pitch_in + turn->pitch);
    if (ShouldLog())
    {
      REXLOG_INFO("mouse look: turret ctrl {:08X} pawn {:08X} fov {:.1f} stick ({:.1f},{:.1f}) "
                  "+ mouse ({:.1f},{:.1f})",
                  controller, pawn, fov, yaw_in, pitch_in, turn->yaw, turn->pitch);
    }
  }
}
