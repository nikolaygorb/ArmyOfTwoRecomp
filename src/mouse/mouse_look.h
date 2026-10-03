// mouse_look.h - raw mouse motion for a direct camera hook, bypassing the
// emulated right stick.
//
// The keyboard/mouse-to-pad driver (thirdparty/rexglue-sdk's mnk driver)
// turns mouse motion into right-stick deflection. That deflection then goes
// through everything the title applies to a real thumbstick (deadzone, response
// curve, acceleration ramp, low-pass filter), so the mouse never feels like a
// mouse. This module is the alternative: it collects raw mouse deltas, scales
// them to an angle and hands whole angular units to a hook sitting in the
// title's own view-rotation code (see ao2_camera_hook.cpp), which adds them to
// the camera directly.
//
// Self-contained on purpose, so the whole src/mouse/ folder can be copied into
// another ReXGlue-based project: this file only talks to rex::ui::Window,
// rex::cvar and the mnk driver's SetMouseLookActive(); it knows nothing about
// the guest's memory layout. Per-project wiring:
//   1. MouseLook::Get().Attach(window(), drawer) once the window exists
//      (ArmyoftworecompApp::OnCreateDialogs);
//   2. a camera hook that calls Consume() every frame (ao2_camera_hook.cpp).
//
// Ownership of the pointer. The mnk driver normally owns the pointer capture
// and the mouse -> stick mapping. While a hook is calling Consume() this
// module takes both over, in a fixed order so the two never fight:
//   claim    SetMouseLookActive(false): mnk stops emitting stick motion and
//            releases its capture on its next poll;
//   engage   once mnk has let go, this module hides + locks the pointer and
//            starts collecting deltas;
//   release  when Consume() stops being called (menu, cutscene, vehicle - any
//            state the hook point doesn't run in), on focus loss, when an
//            overlay wants the mouse, or after Abandon(): the pointer is given
//            back and SetMouseLookActive(true) hands the mouse to the stick
//            mapping again.
// So wherever the hook doesn't run, the mouse keeps working exactly as it did
// before this module existed.
#pragma once

#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace rex::ui
{
  class ImGuiDrawer;
  class WindowedAppContext;
}

namespace ao2::mouse
{

  // Whole angular units, in whatever unit the caller asked Consume() for.
  struct TurnDelta
  {
    int32_t yaw;   // + = turn right
    int32_t pitch; // + = look up
  };

  class MouseLook final : public rex::ui::WindowInputListener, public rex::ui::WindowListener
  {
  public:
    static MouseLook &Get();

    MouseLook(const MouseLook &) = delete;
    MouseLook &operator=(const MouseLook &) = delete;

    // Call once, after the main window has opened. imgui is optional: when
    // given, the mouse is handed back while an overlay wants it.
    void Attach(rex::ui::Window *window, rex::ui::ImGuiDrawer *imgui = nullptr);

    // True when ao2_mouse_direct_look is on and direct look has not been
    // abandoned. Cheap; lets a hook skip its own work when there is nothing
    // to do.
    bool Enabled() const;

    // Call every frame from the camera hook. units_per_radian converts to the
    // title's angle unit (65536 / 2pi for Unreal rotators); extra_scale
    // multiplies the sensitivity (zoom compensation). Returns the turn to add
    // to the camera this frame, or nullopt when the mouse is not (yet) owned
    // by direct look - add nothing and let the title run unmodified. The
    // sub-unit remainder of each frame is carried into the next, so slow
    // motion is never rounded away.
    std::optional<TurnDelta> Consume(double units_per_radian, double extra_scale = 1.0);

    // The hook concluded that adding to the camera does not work in this
    // build. Releases the mouse back to the stick mapping for the rest of the
    // session.
    void Abandon(const char *reason);

    // rex::ui::WindowInputListener
    void OnMouseMove(rex::ui::MouseEvent &e) override;

    // rex::ui::WindowListener
    void OnLostFocus(rex::ui::UISetupEvent &) override;
    void OnGotFocus(rex::ui::UISetupEvent &) override;
    void OnClosing(rex::ui::UIEvent &) override;

  private:
    MouseLook() = default;
    ~MouseLook();

    enum class Phase
    {
      kIdle,      // mnk owns the mouse
      kHandoff,   // mnk told to let go, waiting for it to do so
      kEngaging,  // capture request posted to the UI thread
      kEngaged,   // pointer locked, deltas flowing
      kReleasing, // release request posted to the UI thread
    };

    using Clock = std::chrono::steady_clock;
    using UiTask = std::function<void()>;

    // These expect mu_ held. They only change the phase and return the work
    // for the UI thread (empty when there is none); the caller posts it with
    // Post() after dropping the lock, because queueing may block and the UI
    // thread needs mu_ to deliver mouse motion.
    UiTask BeginReleaseLocked();
    UiTask BeginEngageLocked();
    TurnDelta TakeTurnLocked(double units_per_radian, double extra_scale);
    bool OverlayWantsMouse() const;

    // Call without mu_ held.
    void Post(UiTask task);

    void WatchdogLoop();

    rex::ui::Window *window_ = nullptr;
    rex::ui::WindowedAppContext *ui_ = nullptr;
    rex::ui::ImGuiDrawer *imgui_ = nullptr;

    mutable std::mutex mu_;
    Phase phase_ = Phase::kIdle;
    bool has_focus_ = true;
    bool abandoned_ = false;
    // Set by the UI-thread engage task, read by the UI-thread release task:
    // whether there is anything to undo on the window.
    bool captured_ = false;
    rex::ui::Window::CursorVisibility saved_cursor_ = rex::ui::Window::CursorVisibility::kVisible;
    Clock::time_point handoff_since_{};
    Clock::time_point last_consume_{};

    double pending_dx_ = 0.0;
    double pending_dy_ = 0.0;
    double carry_yaw_ = 0.0;
    double carry_pitch_ = 0.0;

    std::thread watchdog_;
    std::condition_variable watchdog_cv_;
    bool watchdog_stop_ = false;
  };

} // namespace ao2::mouse
