# Army Of Two XBOX360 Recompilation

<img src="assets/icon.png"
     alt="Army of Two"
     width="400"
     align="right"
     style="margin-left: 24px;">

A static recompilation of [**Army of Two**](https://en.wikipedia.org/wiki/Army_of_Two) (2008, EA Montreal / Electronic Arts, Xbox 360;
Title ID `4541084C`, retail hash `AA1EA03FEC9A549C`) to native x86-64 (Windows / Linux),
built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

Static recompilation translates the Xbox 360 PowerPC code inside the game's
`default.xex` into native C++ that compiles and runs on a PC. There is no
emulator and no interpreter in the loop; file I/O, GPU commands, audio and
threading go through the ReXGlue runtime.

<br clear="right">

## Status

Everything works smoothly - codegen runs clean, the build compiles, and the
executable boots to real GPU rendering.

Project-side additions on top of the plain recompilation:

- **Direct mouse look** ([`src/mouse/`](src/mouse/)) - raw 1:1 mouse input
  fed straight into the game camera on foot, with FOV-aware zoom scaling;
  stick emulation stays in charge elsewhere. See `ao2_mouse_*` in
  [`settings/README.md`](settings/README.md#mappingtoml-reference).
- **GPU wait hook** ([`src/render/gpu_wait_hook.cpp`](src/render/gpu_wait_hook.cpp)) -
  the render thread yields instead of busy-spinning while it waits for
  command-buffer space (`ao2_gpu_wait_mode`).
- Ported xenia-canary game patches (FPS unlock, MSAA fix, 16x AF).

## Requirements

- CMake 3.25+
- Git (SDK submodule + patches)
- Ninja
- Clang / LLVM
- [ABGX360](https://github.com/BakasuraRCE/abgx360) to dump the Xbox 360 disc,
  or [extract-xiso](https://github.com/XboxDev/extract-xiso/releases) to
  unpack an existing ISO
- Your own legally-owned copy of Army of Two, extracted from the Xbox 360 disc/ISO

The ReXGlue SDK is a git submodule - see [Getting the SDK](#getting-the-sdk).

## Getting the SDK

The ReXGlue SDK is pinned as a git submodule in `thirdparty/rexglue-sdk` and
built together with the game:

```bash
git clone --recursive <this repo>
# or, in an existing clone:
git submodule update --init --recursive
```

Local SDK fixes live in [`thirdparty/patches/`](thirdparty/patches/) and are
applied to the submodule at configure time by
[`cmake/apply_sdk_patches.cmake`](cmake/apply_sdk_patches.cmake)
(idempotent; the submodule itself is never edited).

## Getting the game data

1. Dump the Xbox 360 disc with
   [ABGX360](https://github.com/BakasuraRCE/abgx360) (verify against the
   hashes below) or extract an existing ISO with
   [extract-xiso](https://github.com/XboxDev/extract-xiso/releases), which
   unpacks the disc's file tree.
2. Copy the extracted contents directly into `assets/`, so `default.xex`
   sits at `assets/default.xex` alongside the rest of the disc's files
   (`AO2Game/`, `$SystemUpdate/`, ...).
3. `assets/` is gitignored (`assets/*` in `.gitignore`, with a couple of
   small tracked exceptions) - nothing from the disc is, or should be,
   committed to this repo.

Target retail release (verify your dump matches):

| Field | Value |
|---|---|
| Title ID | `4541084C` |
| Module Hash | `AA1EA03FEC9A549C` |
| Media ID | `7E3E9BEA` |

## Build

```powershell
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release
```

```bash
cmake --preset linux-amd64-release
cmake --build --preset linux-amd64-release
```

```bash
cmake --preset mac-amd64-release
cmake --build --preset mac-amd64-release
```

Other presets (`*-debug`, `*-relwithdebinfo`, `*-arm64`) are listed in
[`CMakePresets.json`](CMakePresets.json).

Codegen (translating `assets/default.xex` into `generated/default/*.cpp`) runs
automatically as a build step (`armyoftworecomp_codegen` CMake target)
whenever `armyoftworecomp_manifest.toml` or an included `.toml`
changes. It can also be built on its own:
`cmake --build --preset win-amd64-release --target armyoftworecomp_codegen`.

## Run

```powershell
cd out\build\win-amd64-release
.\armyoftworecomp.exe
```

```bash
cd out/build/linux-amd64-release
./armyoftworecomp
```

```bash
cd out/build/mac-amd64-release
./armyoftworecomp
```

Both `--game_data_root` and `--gpu_plugin` are optional: `OnConfigurePaths()`
in [`src/armyoftworecomp_app.h`](src/armyoftworecomp_app.h)
defaults `game_data_root` to `<repo_root>/assets` when it isn't set via
flag/env var, and `gpu_plugin = "xenos"` already lives in
[`settings/hardware.toml`](settings/hardware.toml).

Useful extra flags/env vars while developing:

| Flag / env var | Effect |
|---|---|
| `--game_data_root <path>` | Overrides the default `<repo_root>/assets` game-files location. |
| `--gpu_plugin xenos` | Overrides `settings/hardware.toml`'s `gpu_plugin`. Only needed if you want a different plugin than the file specifies. |
| `--graphics_backend d3d12\|vulkan\|any` | Forces the graphics API `rexgpu-xenos` uses (cvar, default `"any"`, which picks D3D12 first). See [`settings/README.md`](settings/README.md). |
| `--ao2_fps_unlock=true` | Ported xenia-canary `game-patches` "Unlock FPS" patch for Army of Two retail. Enabled by default in [`settings/hardware.toml`](settings/hardware.toml). See [`settings/README.md`](settings/README.md). |
| `--ao2_fps_unlock_mode=0\|1\|2` | Frame-rate target when `ao2_fps_unlock` is on: `0`=unlimited, `1`=60 FPS, `2`=30 FPS. Default `1`. |
| `--ao2_disable_msaa=true` | Ported xenia-canary `game-patches` "Black Shading Fix" (disables MSAA). Enabled by default in [`settings/hardware.toml`](settings/hardware.toml). |
| `--ao2_anisotropic_16x=true` | Ported xenia-canary `game-patches` "16x Anisotropic Filtering". Off by default. |
| `--ao2_gpu_wait_mode=0\|1\|2` | Render-thread wait for command-buffer space: `0`=busy spin (original), `1`=yield (default), `2`=sleep 200us. |
| `--ao2_mouse_direct_look=true` | Direct mouse look on foot. Enabled in [`settings/mapping.toml`](settings/mapping.toml). |

Logs are written to `out\build\<preset>\logs\*.log` (the exe is built `WIN32`,
so nothing prints to the console).

## Configuration

Rendering/window/vsync and input-backend defaults are checked in under
[`settings/`](settings/README.md) (`hardware.toml` / `mapping.toml`), loaded
automatically at startup. CLI flags and `REX_*` environment variables always
override them - see [`settings/README.md`](settings/README.md) for the full
reference and precedence rules.

> This is an unofficial, non-commercial fan project. It is not affiliated
> with, endorsed, sponsored or approved by Electronic Arts Inc., EA Montreal
> or Microsoft Corporation. *Army of Two* is a trademark of Electronic Arts
> Inc.; Xbox and Xbox 360 are trademarks of Microsoft Corporation. All
> trademarks belong to their respective owners.
>
> This repository contains no game files, code or data from the game and
> does not distribute any. To use it you need your own legally owned copy of
> the game. Do not request or share game files in this project. The cover
> image is the property of Electronic Arts Inc. and is used for
> identification purposes only.

> [IMPORTANT] This is DEMO of [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) usage. It is a fan home-made research project.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) ([releases](https://github.com/rexglue/rexglue-sdk/releases))
- [ABGX360](https://github.com/BakasuraRCE/abgx360) - used to dump the Xbox 360 disc
- [extract-xiso](https://github.com/XboxDev/extract-xiso) ([releases](https://github.com/XboxDev/extract-xiso/releases)) -
  used to unpack the Xbox 360 ISO into the file tree copied into `assets/`
- [xenia](https://github.com/xenia-project/xenia) / [xenia-canary](https://github.com/xenia-canary/xenia-canary) -
  ReXGlue's runtime is derived from Xenia's
- [xenia-canary/game-patches](https://github.com/xenia-canary/game-patches) -
  source of the ported `ao2_fps_unlock` / `ao2_disable_msaa` /
  `ao2_anisotropic_16x` patches
- [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) -
  `settings/gamecontrollerdb.txt`
