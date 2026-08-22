# Web port (Emscripten/WebAssembly), stage 1 — classic presentation — design

Date: 2026-08-20
Status: approved (pending spec review)

## Overview

Build the existing SDL3 port for the browser with Emscripten, so the game runs
from a URL with no install. **Stage 1 ships the classic (flat, original-look)
presentation only**, using the `SDL_Renderer` path that `SdlApp` already carries
as its no-GL fallback (`src/platform_sdl3/sdl_app.cpp:110-149`). No shader is
touched and no GL code is compiled in this stage.

The 3D diorama (`Alt+3`) is **stage 2**, deferred to its own branch and spec.
(The HD/xBRZ mode lives only on a separate, currently-unmerged branch,
`feat/hd-render-mode` — it is not part of desktop master, so this design does
not treat it as an existing feature the web build lacks. If that branch is
ever merged, stage 2's GLES work would extend to it too.) The `#ifdef` seams
introduced here are exactly the points that stage 2 reopens.

### Why this is not "just a recompile"

Five things genuinely have to change; everything else does compile as-is:

1. The main loop blocks (`while (running)` at `sdl_app.cpp:459`, with
   `SDL_Delay` + a sub-millisecond spin at `:449-451`). A blocking loop freezes
   the browser tab.
2. `src/core/asset_manifest.cpp` uses `windows.h` + `bcrypt.h` for SHA-256. It
   is the **only** OS dependency anywhere in `bumpy_core`.
3. Asset-root discovery walks parent directories via `std::filesystem`
   (`src/app/main.cpp:63+`) — meaningless in a virtual filesystem.
4. `bumpy_port.cfg` is written next to the executable
   (`src/app/main.cpp:108-110`) — no such place in a browser.
5. Audio may not start before a user gesture, and the SDL3 audio callback
   interacts badly with Asyncify (see "Audio" below).

### What stage 2 has to do (recorded here so it is not re-litigated later)

The port's GL surface is **56 entry points**: 17 called directly as `gl*()`
(GL 1.1, exported by `opengl32.dll`) — 16 in `src/platform_gl3/*.cpp` plus
`glScissor` in `src/platform_sdl3/sdl_app.cpp`, which clears the diorama's
letterbox bars — plus 39 loaded through the `BUMPY_GL33_FUNCS` X-macro in
`src/platform_gl3/gl33.h`. The 39 are spelled
`X(PFNGLCREATESHADERPROC, CreateShader)` and so do not show up in a grep for
`gl[A-Z]` — an earlier revision of this section counted only the direct calls
and put the surface at 16. A later one counted both lists but scoped the grep
for direct calls to `src/platform_gl3/*.cpp`, missed `glScissor`, and put the
surface at 55. Count both lists, over every file in `src/` that calls GL — as
of stage 2 that is the three `platform_gl3` translation units plus
`sdl_app.cpp`.

The conclusion is unchanged, and holds for the corrected number: all 56 are core
GLES 3.0. The 39 are ES 2.0 functions except the three VAO entry points
(`GenVertexArrays`, `BindVertexArray`, `DeleteVertexArrays`), which ES 3.0
promoted to core; the 17 direct calls are all ES 2.0 core (`glScissor` is GL 1.0
and ES 2.0 core). Only two texture formats are used
(`GL_RGBA8`, `GL_R8`/`GL_RED`), both ES 3.0 sized formats, and the only
`glPixelStorei` parameters are `GL_UNPACK_ALIGNMENT`/`GL_PACK_ALIGNMENT`, which
ES 2.0 already has. There are no geometry or compute shaders, no buffer mapping,
and no threads anywhere in the project (`std::thread` appears zero times).

Stage 2 therefore covers four things, not one:

1. **Context.** Request an ES context instead of GL 3.3 core.
2. **Shaders.** Port the seven GLSL sources from `#version 330 core` to
   `#version 300 es` plus precision qualifiers: the five files in `shaders3d/`
   (`scene.vert`, `wall.frag`, `sprite.frag`, `shadow.frag`, `bloom.frag`) and
   the two inline in `src/platform_gl3/gl_presenter.cpp`. `SceneRenderer` reads
   `shaders3d/` off the filesystem, so those five also have to join the
   `--preload-file` set in `CMakeLists.txt`.
3. **Loader headers.** `gl33.h` includes `<SDL3/SDL_opengl.h>` and
   `<SDL3/SDL_opengl_glext.h>` and its X-macro is written against the
   `PFNGL*PROC` typedef family, which the GLES 3 headers do not define.
   `gl33.h`/`gl33.cpp` need a parallel path for the web build. This is loader
   work, not a shader port, and it was hidden by the undercount above.
4. **Loader linkage.** `load_gl33` resolves every pointer through
   `SDL_GL_GetProcAddress`, which under Emscripten needs
   `-sGL_ENABLE_GET_PROC_ADDRESS` at link time.

## Goals

- The full classic game playable in a current desktop browser: splash, menu,
  password entry, world map, all nine worlds, high scores, game over, outro.
- Sound (OPL2 music + PC-speaker SFX) working.
- The Tab settings overlay working, minus the options that do not apply.
- **The desktop build and its test suite are unchanged in behaviour.** Every
  edit to shared code is either `#ifdef`-guarded or covered by an existing test.
- Assets baked into the build so the game runs from a plain static URL.

## Non-goals

- No 3D mode, no GL of any kind (stage 2). (HD/xBRZ is not part of desktop
  master — see the Overview's note — so it is not a web-build non-goal either.)
- No 16:10 aspect option in the web build (see "Display" — this is a decision,
  not an omission).
- No mobile/touch controls.
- No changes to gameplay, timing sources, PRNG, the `App` state machine, or the
  `--render-*` RE dump tooling.
- No new persisted state. High scores stay session-only, as in the original and
  as `src/core/port_config.h` documents.
- No hosting/deployment work. The output is a static directory; where it gets
  published is a separate decision.

## Decisions taken by the user

| Question | Decision |
|---|---|
| Asset delivery | **Baked into the build** (`--preload-file`), not user-supplied. |
| Scope of this branch | **Classic only.** 3D deferred to stage 2; HD is a separate, unmerged branch (`feat/hd-render-mode`), not part of desktop master. |
| Display aspect | **4:3 only.** The 16:10 option is removed from the web build. |
| Main-loop strategy | **Asyncify**, not a `run()` refactor. |
| Toolchain | Claude installs emsdk to `C:\dev\emsdk`. Done: **6.0.8**, binaries at `C:\dev\emsdk\upstream\emscripten\*.exe`. |

On aspect, the user's standing constraint for the future: 4:3 is the base
geometry. If widescreen is ever added to the web build, it must derive from 4:3
(extending the view) exactly as the desktop port's widescreen-4:3 does — never
by stretching a 16:10 image.

## Architecture

### Main loop — Asyncify

`run()` is left structurally intact. Under `#ifdef __EMSCRIPTEN__`, only
`wait_next_tick` (`sdl_app.cpp:441-455`) changes: `SDL_Delay(n)` becomes
`emscripten_sleep(n)`, and the final busy-spin becomes a yield loop
(`emscripten_sleep(0)`) instead of a pure spin. The `next_frame` deadline
arithmetic, the resync-when-behind rule, and every caller stay exactly as they
are, so the desktop pacing is untouched by construction.

Rejected alternatives, with reasons, so they are not revisited:

- **Refactor to `emscripten_set_main_loop`.** Would require hoisting ~40 locals
  and several lambdas out of a ~380-line loop body that is the most
  behaviourally-critical code in the port. High regression risk in the desktop
  build for a size/speed win that does not matter at 320×200. It also does not
  solve the frame-rate problem below.
- **`-sPROXY_TO_PTHREAD`.** Zero code change, but needs SharedArrayBuffer and
  therefore COOP/COEP response headers, which constrains every future hosting
  choice.

#### Resolved: frame pacing (measured 2026-08-21)

The game ticks at 70.086 Hz (HARD), 35.043 Hz (EASY), or alternating (MEDIUM),
derived from the original's retrace timing. Browsers clamp `setTimeout` and
composite at display refresh (commonly 60 Hz), so two failure modes were
possible: timer clamping stretching each period, or one tick per composited
frame capping the game at 60/70.086 ≈ 86 % speed.

Neither happened. Keeping the `SDL_GetPerformanceCounter` deadline as the
authority — it is wall-clock, not frame-counted, so it is not tied to the
refresh rate — was sufficient on its own.

**Measured**, with a `BUMPY_PACE_PROBE`-guarded probe instrumenting
`wait_next_tick` itself (the single mechanism under test, exercised on every
screen, so no gameplay is needed to run it):

Both platforms measured in Release; the browser tab was kept foregrounded
throughout, since background tabs are timer-throttled.

| | achieved (mean) | achieved (range) | requested | busy (mean) |
|---|---|---|---|---|
| Desktop, 13 samples | 70.086 Hz | all 13 identical | 70.086 Hz | 0.026 |
| Browser, 12 samples | 70.0866 Hz | 70.028 - 70.143 Hz | 70.087 Hz | 0.375 |

**Difference of the means: 0.0008 %**, against a 2 % gate. Taking instead the
single worst browser sample — the conservative reading, since a mean can hide
jitter — the largest deviation from desktop is **0.083 %**, still twenty-four
times inside the gate. The `requestAnimationFrame` catch-up fallback is
therefore not needed and is not implemented.

**Caveat, deliberately recorded.** These samples come from the splash screen,
whose frame is cheap. The browser spends 0.375 of each period working against
the desktop's 0.026 — still ample headroom, but roughly fourteen times the
share. Much of that gap is per-wait Asyncify unwind/rewind overhead, which is
fixed per tick rather than proportional to frame cost, so it should not scale
with a busier in-level frame. That is an argument, not a measurement: the
acceptance playthrough re-runs the probe during actual gameplay to confirm the
busy fraction stays below 1.0 on a level frame.

### Portable SHA-256

New `src/core/sha256.{h,cpp}` (self-contained, ~120 lines), test-driven against
the standard FIPS-180-4 vectors in a new `tests/cpp/sha256_test.cpp`.
`asset_manifest.cpp` switches to it **on all platforms**, and `bcrypt` is dropped
from `target_link_libraries`. The existing `tests/cpp/asset_manifest_test.cpp`
is the regression net proving the swap is behaviour-preserving.

This is deliberately not `#ifdef`-ed: it leaves `bumpy_core` free of every OS
dependency, which is a strict improvement for the desktop build too.

### Platform layer

`platform_gl3/*` is excluded from the Emscripten build. `SdlApp` gets
`#if !defined(__EMSCRIPTEN__)` guards around the `gl_` member, its construction
(`sdl_app.cpp:93-108`), its use in `present_frame` (`:425-436`), the `Alt+3`
handler (`:502-506`), and the destructor's `gl_.reset()`. The web build then
takes the existing `SDL_Renderer` fallback path with no further change.

These guards are the stage-2 seam: stage 2 replaces "excluded" with "GLES 3.0
context + ported shaders" at these same points.

### Display

`square_pixels` is forced `false` in the web build and the toggle is removed:

- `SDL_SetRenderLogicalPresentation(renderer_, 320, 240, LETTERBOX)`, fixed.
- The `Alt+A` handler (`sdl_app.cpp:496-501`) is compiled out.
- The ASPECT row is removed from the settings overlay — `settings_overlay.cpp:73`
  (`toggle_aspect`) and `settings_renderer.cpp:96` (the `"16.10"`/`"4.3"` row) —
  and the remaining rows renumbered.

`Alt+Enter` keeps working: `SDL_SetWindowFullscreen` maps to canvas fullscreen
under Emscripten, and a keypress is a valid user gesture.

The persisted `fullscreen` flag is **not** applied at startup in the web build.
It defaults to `true`, so a first-time visitor with no `localStorage` entry would
have the page take over their screen unasked — hostile, and unreliable anyway
since a fullscreen request outside a user gesture is refused. Unlike
`square_pixels`, the flag itself is left alone rather than forced `false`:
`Alt+Enter` and the overlay's FULLSCREEN row still toggle and persist it.

### Assets and configuration

All 47 original data files (586 KB total) are preloaded into `/assets`. The
preload list is **explicit, not the repository root** — a bare
`--preload-file .@/assets` would sweep in `build/`, `dist/`, and `.git` and
produce a multi-gigabyte `.data`. A CMake-generated staging directory collects
exactly the files the game opens at runtime, and that directory is what gets
preloaded. Under Emscripten the asset-root search is replaced by a fixed
`/assets`, skipping the parent-directory walk entirely. The manifest check does
**not** run in the web build: assets baked in at build time cannot drift, and
the three non-runtime manifest entries (`BUMPY.EXE`, `BUMP-Y.EXE`,
`OLD-GAMES.NFO`, ~400 KB) are not shipped at all. The source tree the staging
directory copies from is verified on the desktop side by
`tests/cpp/asset_manifest_test.cpp`, which is a stronger guarantee at zero
runtime cost.

`bumpy_port.cfg` persistence moves to `localStorage`: `load_port_config` and
`save_port_config` (`src/core/port_config.cpp`) get an `__EMSCRIPTEN__` branch
backed by a small `EM_JS` get/set pair, keyed `bumpy_port_cfg`, storing the same
`key=value` text the file format already uses. No IDBFS, no `syncfs`, no async
filesystem initialisation — the payload is five booleans, of which the web build
only meaningfully uses `music`, `sfx`, and `fullscreen`.

### Audio

`SdlAudio` (`src/platform_sdl3/sdl_audio.cpp`) currently uses SDL3's *callback*
stream, driven by SDL's audio thread. Under Asyncify, a JS-initiated call into
wasm while the main stack is unwound is a known hazard.

The web build therefore uses **push mode**: `SDL_OpenAudioDeviceStream` with a
null callback, plus a `pump()` that renders and pushes samples from the main
loop, targeting a ~100 ms queued buffer. This sidesteps the reentrancy hazard and
also absorbs the timer jitter that would otherwise cause underruns. Desktop keeps
the callback path unchanged.

Audio start is gated behind the shell's click-to-play screen, which satisfies the
browser's user-gesture requirement before `SDL_Init(SDL_INIT_AUDIO)` runs.

### HTML shell

`src/web/shell.html`: black page, centred canvas, a loading indicator for the
`.data` fetch, and a **CLICK TO PLAY** gate that both unlocks audio and gives the
canvas keyboard focus. Arrow keys, space, and Tab are `preventDefault`-ed so the
page does not scroll and Tab does not move focus off the canvas — Tab is the
settings-overlay key.

The gate doubles as the sign-off. Quitting (the overlay's QUIT row, or Escape
from the menu) ends the run loop and returns from `main()`; with `EXIT_RUNTIME=0`
the page survives, so without this the player is left on a dead canvas with no
message. `main()` notifies the shell through an `EM_JS` hook and the gate
reappears reading **THANKS FOR PLAYING / CLICK TO RESTART**. That click calls
`location.reload()`, not a second `Module.callMain([])`: re-entering `main()`
after `SDL_Quit()` is untested, and settings survive a reload anyway.

### Build

`CMakeLists.txt` gains `if(EMSCRIPTEN)` branches rather than a second file:

- SDL3 continues to come from `FetchContent` (SDL3 supports Emscripten
  natively); no `opengl32`, no `bcrypt`.
- `bumpy_platform_sdl3` drops the `platform_gl3/*` sources.
- The `audio_render` tool and `bumpy_tests` targets are excluded (tests run
  natively).
- Link options: `-sASYNCIFY`, `-sALLOW_MEMORY_GROWTH`, `-sEXIT_RUNTIME=0`,
  `--preload-file`, `--shell-file src/web/shell.html`, `-O2`.
- Output lands in `dist/web/` as `bumpy.html` + `.js` + `.wasm` + `.data`.

A `CMakePresets.json` preset (`web-release`) wraps the `emcmake` toolchain file
so the build is one command.

## Testing

**Desktop regression (the primary safety net).** The native build and the full
Catch2 suite must stay green after the SHA-256 swap, the `#ifdef` guards, and the
settings-overlay row removal. `sha256_test.cpp` is new and TDD; `asset_manifest_test.cpp`,
`port_config_test.cpp`, and `settings_overlay_test.cpp`/`settings_renderer_test.cpp`
cover the changed shared code.

**Browser acceptance (manual, no automated harness in scope).**

1. Page loads, click-to-play, splash appears.
2. Menu navigation and difficulty selection.
3. World map: movement, cloud jump, entering a board.
4. In-level play: bounce, springs, exit portal, death, lives.
5. Sound: intro music, SFX, and the Tab overlay's music/SFX toggles.
6. Password screen accepts a world code and enters that world.
7. High scores: name entry and the table.
8. Settings persist across a page reload (`localStorage`).
9. `Alt+Enter` fullscreen — and the page does **not** go fullscreen on load.
10. All nine worlds load (asset preload covers every `MONDE*.VEC`/`D*.BUM`).
11. QUIT (overlay row, or Escape from the menu) shows the sign-off gate; a
    click reloads into a fresh game.
12. **Frame-pacing measurement** against the 2 % gate above.

## Files touched

| File | Change |
|---|---|
| `CMakeLists.txt` | `if(EMSCRIPTEN)` branches; drop `bcrypt` everywhere |
| `CMakePresets.json` | `web-release` preset |
| `src/core/sha256.{h,cpp}` | **new** — portable SHA-256 |
| `tests/cpp/sha256_test.cpp` | **new** — FIPS-180-4 vectors |
| `src/core/asset_manifest.cpp` | BCrypt → portable SHA-256 |
| `src/core/port_config.cpp` | `localStorage` branch under `__EMSCRIPTEN__` |
| `src/app/main.cpp` | fixed `/assets` root; cfg path under Emscripten; exit notice to the shell |
| `src/platform_sdl3/sdl_app.cpp` | GL guards; forced 4:3; no startup fullscreen; Asyncify yield; audio pump |
| `src/platform_sdl3/sdl_audio.{h,cpp}` | push-mode stream under Emscripten |
| `src/game/settings_overlay.cpp` | ASPECT row removed in web build |
| `src/video/settings_renderer.cpp` | ASPECT row removed in web build |
| `src/web/shell.html` | **new** — page shell |

## Risks

1. **Frame pacing** — the one open technical question. Gated by measurement, with
   a defined fallback (rAF + catch-up). Highest-priority item to prove early.
2. **Audio underruns** — mitigated by push mode and a generous buffer, but
   browser audio under a jittery main loop is a known trouble spot. May need
   buffer tuning.
3. **SDL3 Emscripten maturity** — SDL3's Emscripten backend is less
   battle-tested than SDL2's, and the port pins a specific SDL revision
   (`CMakeLists.txt`, `GIT_TAG 8e37db5e`) that predates some Emscripten fixes.
   Mitigation if it misbehaves: move the pin forward to a newer SDL3 revision
   for both desktop and web, re-running the desktop suite to confirm no
   regression. Dropping to SDL2 is explicitly out of scope.
4. **Asyncify build size/speed** — expected +30-40 % wasm size. Acceptable, and
   reducible later with `ASYNCIFY_ONLY` if it ever matters.

## Phasing

1. Portable SHA-256 (TDD, desktop-only change, keeps the suite green).
2. emsdk + CMake Emscripten branch; get *something* linking.
3. Asyncify yield + GL guards → first frame in a browser tab.
4. **Frame-pacing measurement and gate.**
5. Assets, `localStorage` config, forced 4:3, overlay row removal.
6. Audio push mode.
7. HTML shell polish.
8. Full manual acceptance pass.
