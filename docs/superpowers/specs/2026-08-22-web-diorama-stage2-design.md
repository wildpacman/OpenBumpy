# Web port stage 2 — the diorama in the browser — design

Date: 2026-08-22
Status: approved (pending spec review)

## Overview

Bring the 3D diorama presentation to the Emscripten/WebAssembly build by
reopening the `#ifdef __EMSCRIPTEN__` seams stage 1 deliberately left, and rename
the mode from **3D** to **DIORAMA** on both platforms.

Stage 1 (`docs/superpowers/specs/2026-08-20-web-port-classic-design.md`) shipped
the classic flat presentation only, guarding every GL path out of the web build
rather than deleting it. This stage is the test of whether that seam was real.

## What the exploration established

Stage 1's spec estimated this work from a reading of the code. Two of those
estimates were wrong, and both were corrected by compiling rather than reasoning:

**The C++ needs no porting at all.** All four `platform_gl3` translation units —
`gl33.cpp`, `gl_util.cpp`, `gl_presenter.cpp`, `scene_renderer.cpp` — compile
under `emcc` with zero errors, verified directly. Stage 1's spec claimed
`gl33.h` would need a parallel path because the GLES 3 headers do not define the
`PFNGL*PROC` typedef family. That is true of the *system* GLES headers, but the
port does not use them: it includes SDL's own `SDL_opengl_glext.h`, which is
self-contained (2636 typedefs, no platform dependency). The loader rewrite that
spec anticipated is not needed.

**The shader count is seven, not five.** Five GLSL files live in `shaders3d/`
(`scene.vert`, `wall.frag`, `sprite.frag`, `shadow.frag`, `bloom.frag`) and two
more are inline string literals in `gl_presenter.cpp` (`kFlatVert`, `kFlatFrag`)
for the flat blit. The inline pair is the one stage 2 brings up *first*, since
the flat presentation goes through it.

## Goals

- `Alt+3` and the settings row work in the browser, presenting the same diorama
  the desktop build shows.
- The mode is called **DIORAMA** everywhere in the UI.
- One copy of each shader body serves both platforms.
- Desktop behaviour unchanged apart from the label.
- If WebGL2 is unavailable, the build falls back to the flat `SDL_Renderer`
  presentation exactly as the desktop build falls back when GL 3.3 is missing.

## Non-goals

- The HD/xBRZ mode (`Alt+H`) stays out. It lives on the unmerged
  `feat/hd-render-mode` branch and is a different feature — a pixel-art
  upscaler, not a diorama. Naming this mode "HD" was considered and rejected for
  exactly that collision.
- The offline dev tools (`--render-3d`, `--present-parity`) stay compiled out of
  the web build. They write files from a command line that a browser does not
  have.
- No change to the diorama's look. The lighting pass (core glow, ceiling light,
  wall bloom, overhead shadows, glossy tops) is settled; this stage moves it, it
  does not tune it.

## Naming

**DIORAMA**, decided by the project owner.

The settings page allows **11 characters** for a label: labels start at x=48,
values at x=224, one glyph cell is 16px (`src/video/settings_renderer.cpp`).
`DIORAMA` is 7 and fits with room; `DIORAMA MODE` is 12 and would overlap the
value column.

**What does not change:**

- The `Alt+3` hotkey. It is muscle memory and documented in the README.
- The `render3d` key in `bumpy_port.cfg` and in `localStorage`. Renaming it
  would silently reset the mode for everyone with an existing config, since
  unknown keys are ignored by design.
- The internal `SettingsEvent::toggle_3d` enumerator and the `render3d` field
  names. They are not user-visible, and renaming them would churn the tests for
  no gain.

Only the rendered label changes.

## Architecture

### Shader preamble injected in C++

Each shader currently opens with `#version 330 core`, which GLSL ES 3.00 rejects.
Rather than fork the shaders per platform, the version line comes out of the
sources and is supplied by the compiler wrapper.

`compile_shader` in `src/platform_gl3/gl_util.cpp` is the single choke point —
every shader on both platforms, file-backed and inline, passes through it. It
already calls `gl.ShaderSource(shader, 1, &src, &len)`, which takes an array, so
the preamble is prepended as a second array element with no string
concatenation:

```cpp
#ifdef __EMSCRIPTEN__
constexpr const char* kGlslPreamble = "#version 300 es\nprecision highp float;\n";
#else
constexpr const char* kGlslPreamble = "#version 330 core\n";
#endif
```

`precision highp float;` is required in ES fragment shaders, which have no
default float precision, and is legal (if redundant) in vertex shaders — so one
preamble serves both stages.

The seven shader bodies lose their `#version` line and are otherwise untouched.
GLSL 330 and GLSL ES 3.00 agree on everything the port uses: `in`/`out`,
`layout(location = N)` on vertex inputs, `texture()`, and a declared fragment
output.

### Context request

The constructor asks for GL 3.3 core. Under Emscripten it asks for GLES 3.0
instead — `SDL_GL_CONTEXT_PROFILE_ES` with major 3, minor 0 — which SDL maps to
a WebGL2 context. The window keeps `SDL_WINDOW_OPENGL`, and the existing
try/catch around `GlPresenter` construction keeps the flat fallback: a browser
without WebGL2 gets the stage-1 presentation rather than a failure.

### Link options

- `-sMAX_WEBGL_VERSION=2` — without it Emscripten creates a WebGL1 context and
  every GLES 3.0 entry point fails.
- `-sGL_ENABLE_GET_PROC_ADDRESS` — `load_gl33` resolves all 39 entry points
  through `SDL_GL_GetProcAddress`, which under Emscripten routes to
  `emscripten_webgl_get_proc_address` and is compiled out without this flag.

### Shaders in the asset image

`shaders3d/` is currently copied next to the executable for desktop only. The
web build preloads it to `/assets/shaders3d`, which needs **no code change**:
`shader_dir()` in `SdlApp::run` already falls back to `asset_root / "shaders3d"`
when no directory sits beside the executable, and `asset_root` is `/assets` in
the web build.

### Reopening the seams

`platform_gl3/*` joins the Emscripten build, and the `#ifdef __EMSCRIPTEN__`
regions in `src/platform_sdl3/sdl_app.h` and `sdl_app.cpp` come out. This is the
seam stage 1 was built around: stage 1 finished with **zero deleted lines** in
those files, so removing the guards should restore the desktop code exactly.

Two guards stay: the dev-tool regions in `src/app/main.cpp`, and the forced
`square_pixels = false` — the web build remains 4:3 only.

### Default state

Removing the forced `render3d = false` lets the web build inherit the desktop
default, which is on. The diorama is a much heavier frame than the flat path,
and the browser already spends 0.375 of each period working on a *flat* splash
frame (stage 1's measurement). Whether it fits the tick budget is unknown.

**This stage does not guess.** The default goes on to match desktop, and the
acceptance pass re-runs the `BUMPY_PACE_PROBE` measurement with the diorama
active. If `busy` approaches 1.0, the default becomes a decision for the owner
rather than an assumption in a spec.

## Testing

**Desktop regression.** The native build and the full Catch2 suite stay green,
**with no test file edited**. Checked: `settings_renderer_test.cpp` does not
reference the `"3D"` label at all, and `settings_overlay_test.cpp` mentions 3D
only in a test name and a comment while asserting on the `toggle_3d` enumerator,
which this stage keeps. The rename touches rendered text that no test pins.

**Web.** No automated browser harness, as in stage 1. Verification is that the
web target compiles and links clean with forced rebuilds on both platforms, and
then a checklist the owner runs. The diorama is a *visual* feature: whether it
looks right in the browser is not something the build can answer.

Acceptance items added to `docs/web-acceptance.md`:

1. The settings row reads `DIORAMA` and can be toggled.
2. `Alt+3` toggles the diorama in a level.
3. The diorama renders — walls, sprites, shadows, the core glow — and matches
   the desktop build side by side.
4. The screen-change darken keeps the diorama on screen (`present_3d_wipe`)
   rather than popping to flat.
5. The setting survives a reload (it persists through the same `render3d` key).
6. A browser without WebGL2 still starts, in flat presentation.
7. Pace probe with the diorama active: `busy` stays below 1.0 in a level.

## Files touched

| File | Change |
|---|---|
| `CMakeLists.txt` | compile `platform_gl3` for Emscripten; add the two link options; preload `shaders3d` |
| `src/platform_gl3/gl_util.cpp` | the preamble, injected in `compile_shader` |
| `src/platform_gl3/gl_presenter.cpp` | drop `#version` from `kFlatVert`/`kFlatFrag` |
| `shaders3d/*.vert`, `*.frag` | drop the `#version` line from all five |
| `src/platform_sdl3/sdl_app.cpp` | ES context request; remove the GL guards |
| `src/platform_sdl3/sdl_app.h` | remove the GL guards |
| `src/video/settings_renderer.cpp` | label `3D` → `DIORAMA`; restore the row on web |
| `src/game/settings_overlay.h`, `.cpp` | web video rows 1 → 2 |
| `docs/web-acceptance.md` | the seven items above |
| `README.md`, `docs/PROJECT_STATUS.md` | stage 2 done; the mode is DIORAMA |

## Risks

1. **Performance.** The one genuinely open question, and the reason the pace
   probe is in the acceptance list rather than a footnote. A diorama frame is far
   heavier than a flat one, and Asyncify already costs a fixed slice of every
   tick.
2. **Shader dialect surprises.** The bodies should port unchanged, but GLSL ES
   3.00 is stricter about implicit conversions than GLSL 330. Compile failures
   surface as a thrown `std::runtime_error` carrying the info log, which the
   existing code turns into "3D disabled" rather than a crash — so a dialect
   problem degrades to the flat path instead of a black screen.
3. **Visual divergence.** Precision qualifiers and WebGL2's floating-point
   behaviour could shift the look subtly. Only a side-by-side comparison against
   the desktop build will catch it, which is why acceptance item 3 asks for one.
