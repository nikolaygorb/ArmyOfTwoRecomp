#include "game_cvars.h"

REXCVAR_DEFINE_STRING(graphics_backend, "any", "GPU",
                      "Graphics API backend: any, d3d11 (experimental), d3d12, vulkan")
    .allowed({"any", "d3d11", "d3d12", "vulkan"});

REXCVAR_DEFINE_BOOL(ao2_fps_unlock, false, "Gameplay",
                    "Unlock FPS. Game speed is tied to FPS, kicks in above ~90FPS");

REXCVAR_DEFINE_INT32(ao2_fps_unlock_mode, 1, "Gameplay",
                     "Frame-rate target used when ao2_fps_unlock is enabled: 0=unlimited, 1=60FPS, 2=30FPS");

REXCVAR_DEFINE_BOOL(ao2_disable_msaa, false, "Gameplay",
                    "Black Shading Fix: disables MSAA");

REXCVAR_DEFINE_BOOL(ao2_anisotropic_16x, false, "Gameplay",
                    "16x Anisotropic Filtering");

REXCVAR_DEFINE_BOOL(dev_debug_runtime, false, "Debug",
                    "Enable runtime debug tools (stub sweep, missing function scan)");
