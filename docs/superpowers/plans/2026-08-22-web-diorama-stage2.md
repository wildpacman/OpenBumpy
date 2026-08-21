# Web port stage 2 — the diorama in the browser — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the 3D diorama presentation in the browser through WebGL2, and rename the mode to DIORAMA on both platforms.

**Architecture:** Stage 1 guarded every GL path out of the Emscripten build without deleting a line; this stage reopens those guards. The C++ needs no porting — all four `platform_gl3` translation units already compile under `emcc`. What changes is the context request (GLES 3.0 instead of GL 3.3 core), two link options, and where the GLSL `#version` line lives: it moves out of the seven shader bodies into `compile_shader`, which is the single choke point every shader passes through, so one body serves both dialects.

**Tech Stack:** C++20, SDL3, OpenGL 3.3 core (desktop) / WebGL2 via GLES 3.0 (browser), GLSL 330 and GLSL ES 3.00, Emscripten 6.0.8, CMake + Ninja, Catch2 (desktop only).

**Spec:** `docs/superpowers/specs/2026-08-22-web-diorama-stage2-design.md`

**Branch:** `feat/web-port` (continues stage 1; do not branch again)

## Global Constraints

- **Desktop behaviour must not change**, apart from the settings label. The native build and the full Catch2 suite must be green at the end of every task, **with no test file edited** — verified: no test asserts on the `"3D"` label.
- **The diorama's rendered output must not change on desktop.** Task 1 gates on a byte-identical `--render-3d` dump.
- **The mode is DIORAMA in the UI only.** The `Alt+3` hotkey, the `render3d` key in `bumpy_port.cfg`/`localStorage`, and the internal `SettingsEvent::toggle_3d` / `render3d` identifiers all stay. Renaming the config key would silently reset the mode for anyone with an existing config, since unknown keys are ignored by design.
- **Label width limit: 11 characters.** Labels start at x=48, values at x=224, one glyph cell is 16px (`src/video/settings_renderer.cpp`). `DIORAMA` is 7.
- **The web build stays 4:3 only.** The forced `square_pixels = false` guard is NOT removed.
- **The offline GL dev tools stay out of the web build** (`--render-3d`, `--present-parity` in `src/app/main.cpp`). They write files from a command line a browser does not have.
- **No change to the diorama's look.** This stage moves it; it does not tune it.
- **No browser automation.** A standing decision on this project: no subagent drives the owner's browser. Verification is builds plus a checklist the owner runs. Do not open a browser, start an HTTP server, or use any `mcp__claude-in-chrome__*` tool.
- **Do not launch `bumpy_port.exe` interactively** — it starts fullscreen and would seize the owner's screen. The `--render-*` dev tools are fine: they run headless and exit.
- Toolchain: emcc at `C:/dev/emsdk/upstream/emscripten/emcc.exe`; ninja at `C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe`.
- **Forced rebuilds when checking for warnings.** An incremental build does not recompile an unchanged file and so proves nothing about its diagnostics.

---

### Task 1: Move the GLSL version line into `compile_shader`

Desktop-only in effect, and the riskiest change in the plan: it touches every shader the diorama uses. The gate is a byte-identical headless dump.

**Files:**
- Modify: `src/platform_gl3/gl_util.cpp` (the preamble + `compile_shader`)
- Modify: `src/platform_gl3/gl_presenter.cpp` (`kFlatVert`, `kFlatFrag`)
- Modify: `shaders3d/scene.vert`, `wall.frag`, `sprite.frag`, `shadow.frag`, `bloom.frag`

**Interfaces:**
- Consumes: `Gl33` and `compile_shader(const Gl33&, GLenum, std::string_view)` as they exist.
- Produces: shader sources that carry no `#version` line. Every later task and every future shader edit depends on this: a shader body must never re-introduce one, or it lands in the middle of the source and fails to compile.

- [ ] **Step 1: Capture the baseline dump**

The desktop build already exists at `build/windows-debug`. Render a diorama frame headlessly:

```
cmake --build build/windows-debug --config Debug --target bumpy_port
build\windows-debug\Debug\bumpy_port.exe --render-3d 1 MONDE1.VEC 0 baseline-3d.bmp
```

Run from the repository root (`C:\dev\BUMPY`) — the tool resolves assets by relative path. Record the file's size and SHA-256:

```
sha256sum baseline-3d.bmp
```

Keep this value. It is the gate for Step 6. If this command fails, stop and report BLOCKED — without a baseline there is no way to prove the shader change is inert.

- [ ] **Step 2: Add the preamble**

In `src/platform_gl3/gl_util.cpp`, inside the existing anonymous-namespace-free top of `namespace bumpy {`, add above `compile_shader`:

```cpp
namespace {

// The GLSL version line lives here rather than in the shader sources. WebGL2 speaks
// GLSL ES 3.00, which rejects "#version 330 core" and gives fragment shaders no
// default float precision; desktop GL 3.3 rejects "#version 300 es". The bodies are
// otherwise identical, so keeping one copy of each and supplying the dialect here
// beats forking seven shaders into fourteen -- the diorama's look was tuned once and
// should not have to be tuned twice.
#ifdef __EMSCRIPTEN__
constexpr const char* kGlslPreamble = "#version 300 es\nprecision highp float;\n";
#else
constexpr const char* kGlslPreamble = "#version 330 core\n";
#endif
constexpr GLint kGlslPreambleLen =
    static_cast<GLint>(std::char_traits<char>::length(kGlslPreamble));

}  // namespace
```

Add `#include <string>` to the includes if it is not already there (it provides `std::char_traits`).

- [ ] **Step 3: Feed the preamble as a second source element**

In the same file, replace these three lines of `compile_shader`:

```cpp
    const GLchar* src = source.data();
    const GLint len = static_cast<GLint>(source.size());
    gl.ShaderSource(shader, 1, &src, &len);
```

with:

```cpp
    // #version must be the first thing in a shader, so the preamble is element 0 of
    // the source array rather than a concatenation -- ShaderSource already takes one.
    const GLchar* srcs[2] = {kGlslPreamble, source.data()};
    const GLint lens[2] = {kGlslPreambleLen, static_cast<GLint>(source.size())};
    gl.ShaderSource(shader, 2, srcs, lens);
```

- [ ] **Step 4: Strip `#version` from the five shader files**

Delete the first line (`#version 330 core`) from each of `shaders3d/scene.vert`, `shaders3d/wall.frag`, `shaders3d/sprite.frag`, `shaders3d/shadow.frag`, `shaders3d/bloom.frag`. Change nothing else in them.

- [ ] **Step 5: Strip `#version` from the two inline shaders**

In `src/platform_gl3/gl_presenter.cpp`, change:

```cpp
constexpr const char* kFlatVert = R"GLSL(#version 330 core
layout(location = 0) in vec2 a_pos;
```

to:

```cpp
constexpr const char* kFlatVert = R"GLSL(
layout(location = 0) in vec2 a_pos;
```

and:

```cpp
constexpr const char* kFlatFrag = R"GLSL(#version 330 core
in vec2 v_uv;
```

to:

```cpp
constexpr const char* kFlatFrag = R"GLSL(
in vec2 v_uv;
```

The leading newline left in each raw string is harmless — it becomes line 1 of the body, after the injected preamble.

- [ ] **Step 6: Verify the dump is byte-identical**

```
cmake --build build/windows-debug --config Debug --target bumpy_port
build\windows-debug\Debug\bumpy_port.exe --render-3d 1 MONDE1.VEC 0 after-3d.bmp
sha256sum baseline-3d.bmp after-3d.bmp
```

Expected: **the two digests match exactly.** The diorama is a pile of floating-point shading; if the digests differ, the preamble is not equivalent to what the shaders declared before — stop and report rather than accepting "looks the same".

- [ ] **Step 7: Run the full desktop suite**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS, all tests, no test file edited.

- [ ] **Step 8: Delete the scratch dumps and commit**

```bash
rm -f baseline-3d.bmp after-3d.bmp
git add src/platform_gl3/gl_util.cpp src/platform_gl3/gl_presenter.cpp shaders3d
git commit -m "refactor(gl): supply the GLSL version line from C++, not the shaders

WebGL2 speaks GLSL ES 3.00 and rejects \"#version 330 core\"; desktop GL 3.3
rejects \"#version 300 es\". The seven shader bodies are otherwise identical
across the two dialects, so the version line moves into compile_shader -- the
one function every shader on both platforms passes through -- instead of the
sources forking into fourteen files that would each have to be tuned twice.

Verified inert on desktop: the --render-3d headless dump is byte-identical
before and after."
```

---

### Task 2: Compile the GL layer for the web

After this task the web build contains `GlPresenter` and `SceneRenderer` and the shaders ship in the asset image — but nothing constructs them yet, so the game still runs flat. That separation is deliberate: a linking failure here is a build problem, and a black screen in Task 3 is a runtime problem, and mixing them makes both harder to read.

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the shader sources from Task 1.
- Produces: `platform_gl3` objects in the Emscripten build; `shaders3d/` mounted at `/assets/shaders3d`; the two GL link options.

- [ ] **Step 1: Compile `platform_gl3` on both platforms**

In `CMakeLists.txt`, replace:

```cmake
# Stage 1 of the web port is flat-only: no GL is compiled for Emscripten. Stage 2
# (WebGL2 + GLES 3.00 shaders) reopens this branch.
if(NOT EMSCRIPTEN)
  target_sources(bumpy_platform_sdl3 PRIVATE
    src/platform_gl3/gl33.cpp
    src/platform_gl3/gl_util.cpp
    src/platform_gl3/gl_presenter.cpp
    src/platform_gl3/scene_renderer.cpp)
  target_link_libraries(bumpy_platform_sdl3 PUBLIC opengl32)
endif()
```

with:

```cmake
# The GL layer builds for both platforms: desktop links opengl32 for the GL 1.1
# entry points, while Emscripten resolves everything through WebGL2 and needs no
# library. The sources themselves are identical -- they compile under emcc unchanged,
# because the port includes SDL's self-contained SDL_opengl_glext.h rather than the
# system GLES headers that lack the PFNGL*PROC typedefs.
target_sources(bumpy_platform_sdl3 PRIVATE
  src/platform_gl3/gl33.cpp
  src/platform_gl3/gl_util.cpp
  src/platform_gl3/gl_presenter.cpp
  src/platform_gl3/scene_renderer.cpp)
if(NOT EMSCRIPTEN)
  target_link_libraries(bumpy_platform_sdl3 PUBLIC opengl32)
endif()
```

- [ ] **Step 2: Add the two GL link options**

In the `if(EMSCRIPTEN)` block at the end of `CMakeLists.txt`, inside `target_link_options(bumpy_port PRIVATE ...)`, add after the `-sEXPORTED_RUNTIME_METHODS=callMain` line:

```cmake
    # WebGL2 is GLES 3.0; without this Emscripten creates a WebGL1 context and every
    # GLES 3.0 entry point fails at runtime rather than at link time.
    -sMAX_WEBGL_VERSION=2
    # load_gl33 resolves all 39 post-1.1 entry points through SDL_GL_GetProcAddress,
    # which routes to emscripten_webgl_get_proc_address and is compiled out without this.
    -sGL_ENABLE_GET_PROC_ADDRESS
```

- [ ] **Step 3: Stage the shaders into the asset image**

`shader_dir()` in `SdlApp::run` falls back to `asset_root / "shaders3d"` when no directory sits beside the executable, and `asset_root` is `/assets` in the web build — so mounting the directory there needs no code change.

In the `if(EMSCRIPTEN)` block, after the existing `add_custom_target(bumpy_web_assets DEPENDS ${BUMPY_WEB_STAGED})` line, add:

```cmake
  # The diorama reads its GLSL at runtime, so the shaders ship in the asset image.
  # /assets/shaders3d is exactly where shader_dir() looks once SDL_GetBasePath()
  # yields nothing usable, which is the case in a browser.
  set(BUMPY_WEB_SHADERS scene.vert wall.frag sprite.frag shadow.frag bloom.frag)
  set(BUMPY_WEB_SHADERS_STAGED "")
  foreach(shader IN LISTS BUMPY_WEB_SHADERS)
    add_custom_command(
      OUTPUT ${BUMPY_WEB_STAGE}/shaders3d/${shader}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/shaders3d/${shader}
              ${BUMPY_WEB_STAGE}/shaders3d/${shader}
      DEPENDS ${CMAKE_SOURCE_DIR}/shaders3d/${shader}
      COMMENT "staging web shader ${shader}")
    list(APPEND BUMPY_WEB_SHADERS_STAGED ${BUMPY_WEB_STAGE}/shaders3d/${shader})
  endforeach()
  add_custom_target(bumpy_web_shaders DEPENDS ${BUMPY_WEB_SHADERS_STAGED})
  add_dependencies(bumpy_port bumpy_web_shaders)
```

The existing `--preload-file ${BUMPY_WEB_STAGE}@/assets` then picks the directory up, because it preloads the whole staging tree.

- [ ] **Step 4: Build for the web with a forced rebuild**

```
find src -name "*.cpp" -exec touch {} \; && cmake --build --preset web-release 2>&1 | grep -iE "warning|error"
```

Expected: **no output from the grep**, and exit 0. This is the first time `emcc` links `GlPresenter` and `SceneRenderer` into the wasm; if anything fails, report the exact errors rather than guessing at fixes.

- [ ] **Step 5: Confirm the shaders reached the asset image**

```
ls build/web-release/web-assets/shaders3d/
ls -l build/web-release/bumpy.data
```

Expected: five files listed, and `bumpy.data` grown from 600,148 bytes by roughly the shaders' combined size (a few KB). Report the new byte count.

- [ ] **Step 6: Verify the desktop build is unaffected**

```
find src tests -name "*.cpp" -exec touch {} \; && cmake --build build/windows-debug --config Debug 2>&1 | grep -E "warning C|error C"
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: no grep output, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(web): compile the GL layer and ship the shaders

platform_gl3 now builds for both platforms -- the sources needed no porting, so
the only asymmetry left is opengl32, which exists to reach the GL 1.1 entry
points that a browser resolves through WebGL2 instead. MAX_WEBGL_VERSION=2 is
what makes the context GLES 3.0 rather than WebGL1, and GL_ENABLE_GET_PROC_ADDRESS
is what keeps SDL_GL_GetProcAddress from being compiled away under the loader's
feet. Nothing constructs a presenter yet; that is the next commit."
```

---

### Task 3: Request a WebGL2 context and reopen the runtime seams

The moment of truth for stage 1's central claim: it finished with zero deleted lines in these files, so removing the guards should restore the desktop code exactly.

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h` (3 guarded regions)
- Modify: `src/platform_sdl3/sdl_app.cpp` (the context request + the GL regions)

**Interfaces:**
- Consumes: the GL objects and shaders from Task 2.
- Produces: `SdlApp::gl_available()` returning a real answer in the browser; `present_3d_level()` and `present_3d_wipe()` live on both platforms.

- [ ] **Step 1: Ask for a GLES 3.0 context under Emscripten**

In `src/platform_sdl3/sdl_app.cpp`, replace the whole guarded constructor block — from `#ifndef __EMSCRIPTEN__` through the `#endif` that closes it, i.e. the region that currently ends with the `#else` comment "The web build (stage 1) is flat-only" and its bare `{` — with this single unguarded version:

```cpp
    // Preferred path: a GL 3.3 core context on desktop, GLES 3.0 (WebGL2) in a
    // browser. The GlPresenter carries both the flat and the diorama presentation on
    // either. Attributes must be set before window creation.
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    window_ = SDL_CreateWindow("Bumpy's Arcade Fantasy", 960, 600,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window_) {
        try {
            gl_ = std::make_unique<GlPresenter>(window_);
        } catch (const std::exception& error) {
            // A machine without GL 3.3, or a browser without WebGL2, falls back to the
            // flat SDL_Renderer presentation rather than failing to start.
            std::cerr << "warning: no usable GL context, falling back to SDL_Renderer"
                         " (diorama disabled): " << error.what() << '\n';
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }
    if (!gl_) {
```

Everything from `// Fallback: the original SDL_Renderer presentation, flat only.` onward stays exactly as it is.

- [ ] **Step 2: Remove the header guards**

In `src/platform_sdl3/sdl_app.h`, remove all three `__EMSCRIPTEN__` regions, keeping the desktop side of each:

- the `#ifndef` around `#include "platform_gl3/gl_presenter.h"` — keep the include, unconditional
- the `#ifdef`/`#else` around `gl_available()` — keep only `return gl_ != nullptr;`
- the `#ifndef` around the `std::unique_ptr<GlPresenter> gl_;` member and its comment — keep both, unconditional

- [ ] **Step 3: Remove the runtime guards**

In `src/platform_sdl3/sdl_app.cpp`, remove the remaining `__EMSCRIPTEN__` regions that exist only to exclude GL, keeping the desktop side of each. Find them with:

```
grep -n "__EMSCRIPTEN__" src/platform_sdl3/sdl_app.cpp
```

Remove the guards around: the `scene_renderer.h` include; the destructor's `gl_.reset()`; the whole 3D state block and the `present_3d_level`/`present_3d_wipe` lambdas (drop the `#else` stubs that return `false`); `present_frame`'s GL branch; the `Alt+3` and shader-reload key handlers; the `toggle_3d` overlay case; and both `wipe_3d` sites.

**Do NOT remove** these, which are not about GL:

- the `render3d` seed line — but do change it back to `config.render3d && gl_available()` on both platforms, deleting the `#ifdef` that pinned it to `false`
- the forced `square_pixels = false` / `config.square_pixels = false` block — the web build stays 4:3 only
- the `wait_next_tick` yield region — that is Asyncify, not GL
- the audio pump call — that is the push-mode audio path
- the `BUMPY_PACE_PROBE` region

When you are done, `grep -c "__EMSCRIPTEN__" src/platform_sdl3/sdl_app.cpp` should count only the guards in that keep-list. Report the number and what each remaining one is for.

- [ ] **Step 4: Verify the desktop build is byte-for-byte unaffected in behaviour**

```
find src tests -name "*.cpp" -exec touch {} \; && cmake --build build/windows-debug --config Debug 2>&1 | grep -E "warning C|error C"
ctest --test-dir build/windows-debug -C Debug --output-on-failure
build\windows-debug\Debug\bumpy_port.exe --render-3d 1 MONDE1.VEC 0 after-seam.bmp
```

Expected: no grep output, all tests pass, and the dump renders. Compare its SHA-256 against the Task 1 post-change digest if you still have it; if not, just confirm the command succeeds and the file is a valid non-empty BMP. Then `rm -f after-seam.bmp`.

- [ ] **Step 5: Build for the web with a forced rebuild**

```
find src -name "*.cpp" -exec touch {} \; && cmake --build --preset web-release 2>&1 | grep -iE "warning|error"
```

Expected: no grep output, exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp
git commit -m "feat(web): create a WebGL2 context and run the diorama in the browser

Stage 1 guarded every GL path out of the web build without deleting a line,
on the argument that stage 2 could simply take the guards back out. This is
that removal, and the argument held: the desktop side of each guard was still
exactly where it was left.

The only real asymmetry is the context request -- GLES 3.0 in a browser,
GL 3.3 core on desktop -- and the fallback is unchanged, so a browser without
WebGL2 still starts in the flat presentation instead of failing."
```

---

### Task 4: DIORAMA

**Files:**
- Modify: `src/video/settings_renderer.cpp` (the label, and restore the row on web)
- Modify: `src/game/settings_overlay.h` (`kVideoRowCount` on web: 1 → 2)
- Modify: `src/game/settings_overlay.cpp` (web video dispatch)

**Interfaces:**
- Consumes: `gl_available()` from Task 3, which now returns a real answer in the browser.
- Produces: a VIDEO page with DIORAMA + FULLSCREEN on web, DIORAMA + ASPECT + FULLSCREEN on desktop.

- [ ] **Step 1: Restore the web row count**

In `src/game/settings_overlay.h`, replace:

```cpp
#ifdef __EMSCRIPTEN__
// Web build: FULLSCREEN alone. It is 4:3 only, so there is no ASPECT row, and stage 1
// compiles no GL, so a 3D row could never do anything -- showing an option that cannot
// be chosen is worse than not offering it. Stage 2 restores the 3D row with the context.
inline constexpr int kVideoRowCount = 1;  // FULLSCREEN
#else
inline constexpr int kVideoRowCount = 3;  // 3D, ASPECT, FULLSCREEN
#endif
```

with:

```cpp
#ifdef __EMSCRIPTEN__
inline constexpr int kVideoRowCount = 2;  // DIORAMA, FULLSCREEN (the web build is 4:3 only)
#else
inline constexpr int kVideoRowCount = 3;  // DIORAMA, ASPECT, FULLSCREEN
#endif
```

- [ ] **Step 2: Restore the web dispatch**

In `src/game/settings_overlay.cpp`, replace:

```cpp
#ifdef __EMSCRIPTEN__
            case 0: return SettingsEvent::toggle_fullscreen;
#else
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
#endif
```

with:

```cpp
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
#ifdef __EMSCRIPTEN__
            case 1: return SettingsEvent::toggle_fullscreen;
#else
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
#endif
```

- [ ] **Step 3: Restore the row and rename the label**

In `src/video/settings_renderer.cpp`, replace:

```cpp
#ifdef __EMSCRIPTEN__
        row(0, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#else
        row(0, "3D", view.render3d ? "ON" : "OFF");
        row(1, "ASPECT", view.square_pixels ? "16.10" : "4.3");
        row(2, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#endif
```

with:

```cpp
        // "DIORAMA", not "3D": the name says what the mode is rather than how it is
        // drawn, and it does not collide with the separate xBRZ "HD" mode. Seven glyph
        // cells, well inside the 11 the label column allows.
        row(0, "DIORAMA", view.render3d ? "ON" : "OFF");
#ifdef __EMSCRIPTEN__
        row(1, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#else
        row(1, "ASPECT", view.square_pixels ? "16.10" : "4.3");
        row(2, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#endif
```

- [ ] **Step 4: Verify no test pins the label**

```
grep -rn '"3D"' tests/cpp/
```

Expected: **no matches.** If there are any, stop and report — the plan's constraint says no test file is edited, and a match means that constraint needs revisiting rather than silently breaking.

- [ ] **Step 5: Verify both builds**

```
find src tests -name "*.cpp" -exec touch {} \; && cmake --build build/windows-debug --config Debug 2>&1 | grep -E "warning C|error C"
ctest --test-dir build/windows-debug -C Debug --output-on-failure
find src -name "*.cpp" -exec touch {} \; && cmake --build --preset web-release 2>&1 | grep -iE "warning|error"
```

Expected: no grep output from either build, all tests pass.

- [ ] **Step 6: Confirm the label reached the web binary**

```
strings -n 3 build/web-release/bumpy.wasm | grep -x "DIORAMA"
strings -n 2 build/web-release/bumpy.wasm | grep -x "3D"
```

Expected: `DIORAMA` found, `3D` **not** found.

- [ ] **Step 7: Commit**

```bash
git add src/video/settings_renderer.cpp src/game/settings_overlay.h src/game/settings_overlay.cpp
git commit -m "feat: rename the 3D mode to DIORAMA, and restore its row on the web

The row comes back now that it can actually do something. The name changes with
it: \"3D\" describes the technique, \"DIORAMA\" describes what the player sees,
and it avoids colliding with the separate xBRZ mode that already answers to HD
on its own branch.

Only the rendered label changes. Alt+3, the render3d config key, and the
internal toggle_3d enumerator all stay -- renaming the key would silently reset
the mode for anyone with an existing config, since unknown keys are ignored."
```

---

### Task 5: Documentation and the acceptance checklist

**Files:**
- Modify: `docs/web-acceptance.md`
- Modify: `README.md`
- Modify: `docs/PROJECT_STATUS.md`

**Interfaces:**
- Consumes: everything above.
- Produces: the checklist the owner runs, since the diorama's correctness is a visual question no build can answer.

- [ ] **Step 1: Add the stage 2 acceptance items**

In `docs/web-acceptance.md`, add a new section before the closing "What was verified without you" section:

```markdown
## 8. The diorama — carried (stage 2, never seen in a browser)

The whole point of stage 2, and the part no build can check: whether it looks
right. Compare against the desktop build running the same board side by side.

- [ ] Tab overlay → VIDEO shows **DIORAMA** and **FULLSCREEN**, and DIORAMA can
      be toggled (it was inert through all of stage 1)
- [ ] `Alt+3` toggles the diorama inside a level
- [ ] The diorama actually renders: walls, sprites, the ball's core glow, the
      ceiling light, overhead shadows, glossy block tops
- [ ] It matches the desktop build on the same board — no missing pass, no
      washed-out or oversaturated shading
- [ ] Leaving a board keeps the diorama on screen through the edge-to-centre
      darken rather than popping to flat for the transition
- [ ] The setting survives a page reload (it persists through the same
      `render3d` key as the desktop build)
- [ ] **Pace probe with the diorama active.** Rebuild with
      `-DBUMPY_PACE_PROBE=ON`, play a level on HARD with the tab foregrounded
      and DevTools **undocked**, and confirm `busy` stays below 1.0. A diorama
      frame is far heavier than the flat one that measured 0.375, so this is
      the number that decides whether the diorama can stay on by default.

If the diorama does not appear at all, the console will say why: a failed
context or a shader that would not compile is reported as
`warning: no usable GL context` or a GL info log, and the game falls back to the
flat presentation rather than showing a black screen.
```

- [ ] **Step 2: Update the README, narrowly**

**Scope limit, deliberate:** the rename is the settings label, not the product.
`README.md` is built around the release name Bumpy3D — the title, the
`Bumpy3D.exe` package, the `#3d-render-mode` anchor, the screenshot captions.
Sweeping "3D" out of it would rename the release, which is a decision already
taken the other way. Touch only the "Browser build" section's differences list.

In `README.md`, replace:

```markdown
Differences from the desktop build, all deliberate:

- 4:3 only — no ASPECT row and no `Alt+A`.
- No 3D (`Alt+3`) mode.
- Settings persist to `localStorage` instead of `bumpy_port.cfg`.
```

with:

```markdown
Differences from the desktop build, all deliberate:

- 4:3 only — no ASPECT row and no `Alt+A`.
- Settings persist to `localStorage` instead of `bumpy_port.cfg`.

The 3D diorama runs in the browser too, on a WebGL2 context, and the settings
overlay calls it DIORAMA on both platforms. A browser without WebGL2 falls back
to the flat presentation rather than refusing to start.
```

Change nothing else in the README.

- [ ] **Step 3: Update PROJECT_STATUS**

In `docs/PROJECT_STATUS.md`, update the "Last updated" note to lead with stage 2 (keeping the existing newest-first / "Prior:" structure), and add a Roadmap entry beneath the stage 1 one:

```markdown
- **Web port (browser), stage 2 — DONE.** The diorama runs in the browser on a
  WebGL2 (GLES 3.0) context. The GL sources needed no porting; the GLSL version
  line moved into `compile_shader` so one shader body serves both dialects. The
  mode is now called DIORAMA in the UI on both platforms; `Alt+3` and the
  `render3d` config key are unchanged. Verified: both builds compile warning-free
  and the desktop suite is green; the in-browser look is still pending (see
  `docs/web-acceptance.md`).
```

- [ ] **Step 4: Verify the desktop build once more**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add docs/web-acceptance.md README.md docs/PROJECT_STATUS.md
git commit -m "docs: stage 2 is in, and its acceptance is still the owner's

The diorama's correctness is a visual question, so the checklist carries seven
items nobody has run -- including the pace probe with the diorama active, which
is what decides whether it can stay on by default."
```

---

## Notes for the executor

- **Task 1's byte-identical dump is a hard gate.** The diorama is floating-point shading; "looks the same" is not evidence. If the digests differ, stop.
- **Task 3 is the test of stage 1's central claim.** If removing a guard does not cleanly restore the desktop code, that is worth reporting in detail — it means the seam was not what stage 1 believed.
- **Guard, still.** Everything you remove in Task 3 is GL-specific. The Asyncify yield, the audio pump, the forced 4:3, and the pace probe are not, and they stay.
- **Never claim a verification you did not run.** Every "Expected:" line is a command whose real output must be seen.
