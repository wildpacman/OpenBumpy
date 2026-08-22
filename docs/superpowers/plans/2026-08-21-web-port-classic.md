# Web port (Emscripten/WebAssembly), stage 1 — classic presentation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the existing SDL3 port for the browser with Emscripten so the classic (flat, original-look) game runs from a static URL, with sound, settings, and all nine worlds.

**Architecture:** The web build reuses the `SDL_Renderer` presentation path `SdlApp` already carries as its no-GL fallback, so no shader or GL code is compiled in this stage. The blocking run loop is kept intact and made browser-safe with Asyncify (`emscripten_sleep` replacing `SDL_Delay` plus the busy-spin). Assets are baked into the `.data` image; the five config flags persist to `localStorage`; audio switches from SDL's pull callback to a main-loop push to avoid Asyncify re-entrancy.

**Tech Stack:** C++20, SDL3 (FetchContent, pinned `8e37db5e`), Emscripten 6.0.8 (`C:\dev\emsdk`), CMake 4.2.1 + Ninja, Catch2 (desktop tests only).

**Spec:** `docs/superpowers/specs/2026-08-20-web-port-classic-design.md`

**Branch:** `feat/web-port`

## Global Constraints

- **Desktop behaviour must not change.** Every edit to shared code is either
  `#ifdef`-guarded on `__EMSCRIPTEN__` or covered by an existing test. The native
  build and the full Catch2 suite must be green at the end of every task.
- **Stage 1 is classic only.** No GL, no shaders, no 3D (`Alt+3`), no HD/xBRZ.
  `platform_gl3/*` is not compiled for the web.
- **4:3 only in the web build.** `square_pixels` is forced `false`; the ASPECT row
  and `Alt+A` are compiled out. 4:3 stays the base geometry for any future
  widescreen work.
- **No new persisted state.** High scores stay session-only, as in the original.
- **Assets are baked in.** 47 runtime files, 586 KB, preloaded to `/assets`.
- **Toolchain paths on this machine:**
  - emcc: `C:/dev/emsdk/upstream/emscripten/emcc.exe` (note: `.exe`, not `.bat`)
  - toolchain file: `C:/dev/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`
  - ninja: `C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe`
- **Known toolchain hazard:** CMake 4.x removed compatibility with
  `cmake_minimum_required` below 3.5. If a FetchContent dependency (SDL, Catch2,
  ymfm) fails to configure with a "Compatibility with CMake < 3.5 has been
  removed" error, add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to the configure step.
- **Game tick rates** (needed for the pacing gate): `kVgaRefreshHz = 70.086`
  (HARD, 1 retrace), `kGameTickHz = 35.043` (EASY, 2 retraces), MEDIUM alternates.

---

### Task 1: Portable SHA-256

Removes `bumpy_core`'s only OS dependency. `src/core/asset_manifest.cpp` currently
pulls in `windows.h` + `bcrypt.h`, which cannot compile under Emscripten. This is
a desktop-only change in effect: the same code runs on both platforms afterwards,
and the existing `tests/cpp/asset_manifest_test.cpp` proves the swap is
behaviour-preserving.

**Files:**
- Create: `src/core/sha256.h`
- Create: `src/core/sha256.cpp`
- Test: `tests/cpp/sha256_test.cpp`
- Modify: `src/core/asset_manifest.cpp` (replace the BCrypt `sha256_file`)
- Modify: `CMakeLists.txt` (add sources, drop the `bcrypt` link)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `class bumpy::Sha256` with `void update(const void* data, std::size_t size) noexcept`
    and `std::string hex_digest()` (64 lowercase hex chars; finalises the hash).
  - `std::string bumpy::sha256_hex(const void* data, std::size_t size)` — one-shot helper.

- [ ] **Step 1: Write the failing test**

Create `tests/cpp/sha256_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/sha256.h"

#include <algorithm>
#include <string>

namespace {

std::string hex_of(const std::string& text) {
    return bumpy::sha256_hex(text.data(), text.size());
}

}  // namespace

// FIPS 180-4 / NIST CAVP known-answer vectors.
TEST_CASE("sha256 matches the standard vectors") {
    REQUIRE(hex_of("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(hex_of("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56 bytes: the padding lands exactly on the two-block boundary.
    REQUIRE(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 112 bytes.
    REQUIRE(hex_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                   "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("sha256 is independent of how the input is chunked") {
    const std::string message(1000, 'x');
    const std::string one_shot = bumpy::sha256_hex(message.data(), message.size());

    bumpy::Sha256 streamed;
    // Chunk sizes chosen to straddle the 64-byte block boundary repeatedly.
    std::size_t offset = 0;
    for (const std::size_t chunk : {1U, 63U, 64U, 65U, 127U, 300U}) {
        const std::size_t take = std::min(chunk, message.size() - offset);
        streamed.update(message.data() + offset, take);
        offset += take;
    }
    streamed.update(message.data() + offset, message.size() - offset);

    REQUIRE(streamed.hex_digest() == one_shot);
}

TEST_CASE("sha256 handles a message longer than 64 KB") {
    const std::string message(1'000'000, 'a');
    REQUIRE(bumpy::sha256_hex(message.data(), message.size()) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
```

Register it in `CMakeLists.txt` — add to the `bumpy_tests` source list, after
`tests/cpp/port_config_test.cpp`:

```cmake
  tests/cpp/sha256_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```
cmake --build build/windows-debug --config Debug --target bumpy_tests
```

Expected: FAIL to compile — `core/sha256.h` does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/core/sha256.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bumpy {

// Streaming SHA-256 (FIPS 180-4). Deliberately dependency-free: this replaces a
// Windows BCrypt call that was bumpy_core's only OS dependency, and the web build
// cannot link bcrypt at all.
class Sha256 {
public:
    void update(const void* data, std::size_t size) noexcept;
    // Appends the padding, consumes the final block, and returns the digest as 64
    // lowercase hex characters. Call once: it mutates the internal state.
    [[nodiscard]] std::string hex_digest();

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t total_bytes_{};
};

// One-shot convenience over the whole buffer.
[[nodiscard]] std::string sha256_hex(const void* data, std::size_t size);

}  // namespace bumpy
```

Create `src/core/sha256.cpp`:

```cpp
#include "core/sha256.h"

#include <algorithm>
#include <cstring>

namespace bumpy {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

void Sha256::compress(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        const auto* p = block + i * 4;
        w[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(p[0]) << 24) |
                                         (static_cast<std::uint32_t>(p[1]) << 16) |
                                         (static_cast<std::uint32_t>(p[2]) << 8) |
                                         static_cast<std::uint32_t>(p[3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t a = w[i - 15];
        const std::uint32_t b = w[i - 2];
        const std::uint32_t s0 = rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3);
        const std::uint32_t s1 = rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += size;
    while (size != 0) {
        const std::size_t take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == buffer_.size()) {
            compress(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::string Sha256::hex_digest() {
    // The length is in bits and covers the message only, so capture it before the
    // padding bytes go through update() and inflate total_bytes_.
    const std::uint64_t bit_length = total_bytes_ * 8;

    constexpr std::uint8_t kPadStart = 0x80;
    update(&kPadStart, 1);
    constexpr std::uint8_t kZero = 0;
    while (buffered_ != 56) {
        update(&kZero, 1);
    }
    std::array<std::uint8_t, 8> length_be{};
    for (std::size_t i = 0; i < 8; ++i) {
        length_be[i] = static_cast<std::uint8_t>(bit_length >> (56 - i * 8));
    }
    update(length_be.data(), length_be.size());  // completes the final block

    constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const auto word : state_) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto byte = static_cast<std::uint8_t>(word >> shift);
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0fU]);
        }
    }
    return hex;
}

std::string sha256_hex(const void* data, std::size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hash.hex_digest();
}

}  // namespace bumpy
```

Add the source to `bumpy_core` in `CMakeLists.txt`, right after
`src/core/asset_manifest.cpp`:

```cmake
  src/core/sha256.cpp
```

- [ ] **Step 4: Run the test to verify it passes**

```
cmake --build build/windows-debug --config Debug --target bumpy_tests
build\windows-debug\Debug\bumpy_tests.exe "[sha256],sha256*"
```

Expected: PASS — 3 test cases.
(If the tag filter matches nothing, run `bumpy_tests.exe` with no arguments; the
new cases are named `sha256 ...`.)

- [ ] **Step 5: Switch asset_manifest to the portable hash**

In `src/core/asset_manifest.cpp`, replace the header block:

```cpp
#include "core/asset_manifest.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
```

with:

```cpp
#include "core/asset_manifest.h"

#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
```

Then delete the whole `check()` helper and the BCrypt `sha256_file()` (everything
from `void check(NTSTATUS status, const char* operation) {` down to the closing
brace of `sha256_file`, i.e. up to and including the `} catch (...) { ... throw; }
}` block), and put this in their place — still the first thing inside the
anonymous `namespace {`:

```cpp
std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream source(path, std::ios::binary);
    if (!source) {
        throw std::runtime_error("cannot open asset: " + path.string());
    }
    bumpy::Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (source) {
        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(source.gcount());
        if (count != 0) {
            hash.update(buffer.data(), count);
        }
    }
    if (!source.eof()) {
        throw std::runtime_error("cannot read asset: " + path.string());
    }
    return hash.hex_digest();
}
```

`is_plain_filename()` and everything below it stay exactly as they are.

- [ ] **Step 6: Drop the bcrypt link**

In `CMakeLists.txt`, delete this line entirely:

```cmake
target_link_libraries(bumpy_core PUBLIC bcrypt)
```

- [ ] **Step 7: Run the full suite**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS, all tests. In particular `asset manifest recognizes the supplied
BUMPY executable` must still report `file_count == 50` with no missing and no
changed entries — that is the proof the hash swap is exact.

- [ ] **Step 8: Commit**

```bash
git add src/core/sha256.h src/core/sha256.cpp tests/cpp/sha256_test.cpp \
        src/core/asset_manifest.cpp CMakeLists.txt
git commit -m "refactor(core): portable SHA-256, dropping the BCrypt dependency

asset_manifest was the only place in bumpy_core that reached for an OS API
(windows.h + bcrypt.h), which blocks any non-Windows build. Replace it with a
self-contained FIPS 180-4 implementation, verified against the standard
known-answer vectors and against the existing manifest test, which still hashes
all 50 supplied assets to their recorded digests."
```

---

### Task 2: Emscripten build — CMake branch, GL guards, assets

First successful link. After this task `emcmake` produces `bumpy.html` + `.js` +
`.wasm` + `.data`, but the game will still hang the tab when opened (the loop
blocks until Task 3), so browser testing is deferred.

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `src/platform_sdl3/sdl_app.h` (guard the `GlPresenter` include + member)
- Modify: `src/platform_sdl3/sdl_app.cpp` (guard GL construction, presentation, Alt+3)
- Modify: `src/app/main.cpp` (fixed `/assets` root, no manifest warning, stub cfg path)
- Modify: `docs/superpowers/specs/2026-08-20-web-port-classic-design.md` (record the manifest decision)

**Interfaces:**
- Consumes: `bumpy::Sha256` from Task 1 (indirectly — it is what lets
  `asset_manifest.cpp` compile under Emscripten at all).
- Produces: a `web-release` CMake preset; `/assets` as the web asset root.

- [ ] **Step 1: Guard the GL presenter out of the web build (header)**

In `src/platform_sdl3/sdl_app.h`, replace the include:

```cpp
#include "platform_gl3/gl_presenter.h"
```

with:

```cpp
#ifndef __EMSCRIPTEN__
#include "platform_gl3/gl_presenter.h"
#endif
```

Replace the `gl_available()` accessor:

```cpp
    [[nodiscard]] bool gl_available() const noexcept { return gl_ != nullptr; }
```

with:

```cpp
#ifdef __EMSCRIPTEN__
    // Stage 1 of the web port compiles no GL at all, so the 3D presentation is never
    // available and every caller that gates on it takes the flat path.
    [[nodiscard]] bool gl_available() const noexcept { return false; }
#else
    [[nodiscard]] bool gl_available() const noexcept { return gl_ != nullptr; }
#endif
```

And guard the member declaration:

```cpp
#ifndef __EMSCRIPTEN__
    // Declared AFTER window_ so member-destruction order (reverse of declaration) tears
    // gl_ down BEFORE window_ is destroyed -- the GL context it owns must go while the
    // window that hosts it still exists.
    std::unique_ptr<GlPresenter> gl_;
#endif
```

- [ ] **Step 2: Guard the GL paths in the run loop**

In `src/platform_sdl3/sdl_app.cpp`, wrap the GL context attributes and the
GL-window attempt in the constructor. Replace the block from
`SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);` through the closing
`}` of `if (!gl_) {`'s guard condition — that is, everything from the first
`SDL_GL_SetAttribute` down to the line before
`// Fallback: the original SDL_Renderer presentation, flat only.` — so it reads:

```cpp
#ifndef __EMSCRIPTEN__
    // Preferred path: a GL 3.3 core context (the GlPresenter carries both the flat
    // and the 3D presentation). Attributes must be set before window creation.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    window_ = SDL_CreateWindow("Bumpy's Arcade Fantasy", 960, 600,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window_) {
        try {
            gl_ = std::make_unique<GlPresenter>(window_);
        } catch (const std::exception& error) {
            std::cerr << "warning: OpenGL 3.3 unavailable, falling back to SDL_Renderer"
                         " (3D mode disabled): " << error.what() << '\n';
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }
    if (!gl_) {
#else
    // The web build (stage 1) is flat-only: no GL is compiled, so go straight to
    // the SDL_Renderer path the desktop build uses as its fallback.
    {
#endif
```

In the destructor, guard the explicit reset:

```cpp
#ifndef __EMSCRIPTEN__
    gl_.reset();
#endif
```

In `run()`, guard `present_frame`'s GL branch:

```cpp
    auto present_frame = [&]() {
#ifndef __EMSCRIPTEN__
        if (gl_) {
            gl_->present_flat(frame, square_pixels ? 200 : 240);
            return;
        }
#endif
        const auto rgba = frame.to_rgba();
```

The remaining GL references in `run()` form eight more regions. Guard each with
`#ifndef __EMSCRIPTEN__`; never delete the code — it is the seam stage 2 reopens.
Line numbers are as of commit `0e1b921` and will drift as you edit, so match on
the quoted code.

**(a) The `scene_renderer.h` include (line 6).** Wrap it:

```cpp
#ifndef __EMSCRIPTEN__
#include "platform_gl3/scene_renderer.h"
#endif
```

**(b) The `render3d` seed (line 284).** Replace:

```cpp
    bool render3d = config.render3d && gl_ != nullptr;
```

with:

```cpp
#ifdef __EMSCRIPTEN__
    bool render3d = false;  // stage 1 of the web port compiles no GL
#else
    bool render3d = config.render3d && gl_ != nullptr;
#endif
```

**(c) The whole 3D state block (lines 302-424).** This runs from the comment
`// --- 3D diorama state (Alt+3).` through the closing `};` of the
`present_3d_wipe` lambda. Every local it declares (`scene_renderer`,
`scene_renderer_failed`, `sprite_cache`, `scene_world`, `scene_board`, `wipe_3d`,
`wipe_quads`, `wipe_light_x`, `wipe_light_y`, `shader_dir`, `collect_live_quads`)
is used only inside this block and in regions (g) and (h) below, so the whole
thing can be replaced wholesale. Wrap the existing block in `#ifndef __EMSCRIPTEN__`
and add this `#else`:

```cpp
#else
    // Stage 1 of the web port compiles no GL, so both 3D presentations are permanently
    // unavailable and every call site below falls through to the flat path unchanged.
    auto present_3d_level = []() { return false; };
    auto present_3d_wipe = []() { return false; };
#endif
```

The three call sites (`present_3d_wipe()` at the two screen-change points,
`present_3d_level()` in the final present) then need no guards at all.

**(d) The `present_frame` GL branch** — already done in the step above.

**(e) The `Alt+3` handler and the shader-reload hotkey.** Wrap the whole
`} else if (event.key.key == SDLK_3 && (event.key.mod & SDL_KMOD_ALT)) {` branch
and the shader-reload branch that follows it (the one referencing
`scene_renderer->reload_shaders()`) in `#ifndef __EMSCRIPTEN__` / `#endif`. Both
are `else if` links in a chain, so guarding them individually is well-formed.

**(f) The overlay's `toggle_3d` case.** Replace:

```cpp
        switch (overlay.update(input, gl_ != nullptr)) {
```

with:

```cpp
#ifdef __EMSCRIPTEN__
        switch (overlay.update(input, false)) {
#else
        switch (overlay.update(input, gl_ != nullptr)) {
#endif
```

and wrap the `case SettingsEvent::toggle_3d:` body in `#ifndef __EMSCRIPTEN__`.

**(g) The settings view flag.** Replace:

```cpp
            view.render3d_available = gl_ != nullptr;
```

with:

```cpp
#ifdef __EMSCRIPTEN__
            view.render3d_available = false;
#else
            view.render3d_available = gl_ != nullptr;
#endif
```

**(h) The two `wipe_3d` sites.** Wrap `wipe_3d = false;` (the "3D close is done"
line after a transition) and the whole `wipe_3d = render3d && scene_renderer &&
...` stash block (through the closing `}` of `if (wipe_3d) { ... }`) in
`#ifndef __EMSCRIPTEN__` / `#endif`.

- [ ] **Step 3: Point the web build at a fixed asset root**

In `src/app/main.cpp`, wrap the three filesystem-dependent helpers. Replace the
block from `bool has_asset_manifest(...)` through the end of
`warn_if_assets_changed(...)` so it becomes:

```cpp
#ifdef __EMSCRIPTEN__

// The web build's assets are baked into the .data image and mounted at /assets by
// --preload-file, so there is nothing to search for and nothing that can drift.
std::filesystem::path find_asset_root(std::string_view) {
    return "/assets";
}

// The browser has no directory next to an executable; PortConfig persists to
// localStorage instead (see src/core/port_config.cpp), so this path is never opened.
std::filesystem::path config_file_path(std::string_view) {
    return "/bumpy_port.cfg";
}

// Nothing to verify: the assets ship inside the build, so they cannot differ from
// the manifest. The source tree they are staged from is verified on the desktop
// side by tests/cpp/asset_manifest_test.cpp.
void warn_if_assets_changed(const std::filesystem::path&) {}

#else

// ... the existing has_asset_manifest / add_root_candidates / find_asset_root /
// ... config_file_path / warn_if_assets_changed definitions, unchanged ...

#endif
```

Keep the existing bodies verbatim inside the `#else` — including
`has_asset_manifest` and `add_root_candidates`, which would otherwise warn as
unused in the web build.

- [ ] **Step 4: Add the Emscripten branch to CMakeLists.txt**

Guard the GL sources and the Windows GL library. Replace:

```cmake
add_library(bumpy_platform_sdl3
  src/platform_sdl3/sdl_app.cpp
  src/platform_sdl3/sdl_audio.cpp
  src/platform_gl3/gl33.cpp
  src/platform_gl3/gl_util.cpp
  src/platform_gl3/gl_presenter.cpp
  src/platform_gl3/scene_renderer.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3 opengl32)
```

with:

```cmake
add_library(bumpy_platform_sdl3
  src/platform_sdl3/sdl_app.cpp
  src/platform_sdl3/sdl_audio.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3)
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

Guard the two POST_BUILD copies (the SDL3 DLL and `shaders3d/`) — neither means
anything for a wasm build. Wrap both `add_custom_command(TARGET bumpy_port POST_BUILD ...)`
blocks in:

```cmake
if(NOT EMSCRIPTEN)
  # ... both existing add_custom_command blocks ...
endif()
```

Wrap the `audio_render` target and the whole test section (`enable_testing()`
through `set_tests_properties(...)`) in `if(NOT EMSCRIPTEN)` / `endif()` — tests
run natively.

Then append the web packaging block at the end of the file:

```cmake
if(EMSCRIPTEN)
  # Preload only what the game opens at runtime: 47 files, 586 KB. The manifest also
  # lists BUMPY.EXE, BUMP-Y.EXE and OLD-GAMES.NFO (~400 KB of DOS binaries and docs)
  # that nothing loads -- shipping them would nearly double every player's download.
  set(BUMPY_WEB_ASSETS
    BUMPRESE.VEC BUMPY.BNK BUMPY.MID BUMSPJEU.BIN DDFNT2.CAR DESSFIN.VEC
    FLECHE.BIN MASKBUMP.VEC QUELDISK SCORE.VEC TITRE.VEC)
  foreach(n RANGE 1 9)
    list(APPEND BUMPY_WEB_ASSETS D${n}.BUM D${n}.DEC D${n}.PAV MONDE${n}.VEC)
  endforeach()

  set(BUMPY_WEB_STAGE ${CMAKE_CURRENT_BINARY_DIR}/web-assets)
  set(BUMPY_WEB_STAGED "")
  foreach(asset IN LISTS BUMPY_WEB_ASSETS)
    add_custom_command(
      OUTPUT ${BUMPY_WEB_STAGE}/${asset}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/${asset} ${BUMPY_WEB_STAGE}/${asset}
      DEPENDS ${CMAKE_SOURCE_DIR}/${asset}
      COMMENT "staging web asset ${asset}")
    list(APPEND BUMPY_WEB_STAGED ${BUMPY_WEB_STAGE}/${asset})
  endforeach()
  add_custom_target(bumpy_web_assets DEPENDS ${BUMPY_WEB_STAGED})
  add_dependencies(bumpy_port bumpy_web_assets)

  set_target_properties(bumpy_port PROPERTIES OUTPUT_NAME bumpy SUFFIX ".html")
  target_link_options(bumpy_port PRIVATE
    # The run loop still blocks; Asyncify is what turns emscripten_sleep into a real
    # yield so the browser gets its thread back (see src/platform_sdl3/sdl_app.cpp).
    -sASYNCIFY
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4194304
    # main() is held back until the shell's click-to-play gate, so the audio device
    # opens inside a user gesture.
    -sINVOKE_RUN=0
    -sEXPORTED_RUNTIME_METHODS=callMain
    --preload-file ${BUMPY_WEB_STAGE}@/assets
    --shell-file ${CMAKE_SOURCE_DIR}/src/web/shell.html)
  set_property(TARGET bumpy_port APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/web/shell.html)
endif()
```

- [ ] **Step 5: Create a placeholder shell so the link succeeds**

Task 7 writes the real shell. For now create `src/web/shell.html` with the
minimum Emscripten requires:

```html
<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><title>Bumpy3D</title></head>
<body style="margin:0;background:#000">
<canvas id="canvas" tabindex="-1"></canvas>
<script>var Module = { canvas: document.getElementById('canvas') };</script>
{{{ SCRIPT }}}
</body>
</html>
```

- [ ] **Step 6: Add the web-release preset**

In `CMakePresets.json`, add to `configurePresets` (paths are machine-specific,
matching the existing `windows-debug` preset's hardcoded generator):

```json
    {
      "name": "web-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/web-release",
      "toolchainFile": "C:/dev/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_MAKE_PROGRAM": "C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
      }
    }
```

and to `buildPresets`:

```json
    {
      "name": "web-release",
      "configurePreset": "web-release"
    }
```

- [ ] **Step 7: Configure and build for the web**

```
cmake --preset web-release
cmake --build --preset web-release
```

Expected: `build/web-release/bumpy.html`, `bumpy.js`, `bumpy.wasm`, `bumpy.data`
exist, and `bumpy.data` is roughly 600 KB (not hundreds of MB — if it is huge,
the preload picked up the wrong directory).

Iterate on compile errors here: this is the step where any remaining `gl_`
reference or Windows-only include surfaces. Fix each by guarding, never by
deleting desktop code.

- [ ] **Step 8: Verify the desktop build is untouched**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS, all tests.

- [ ] **Step 9: Record the manifest decision in the spec**

The spec currently says the manifest check still runs in the web build. It does
not — the assets are baked in, so there is nothing to verify at runtime, and the
three non-runtime manifest entries are not shipped. In
`docs/superpowers/specs/2026-08-20-web-port-classic-design.md`, in the
"Assets and configuration" section, replace:

```
The
manifest check (`config/original-assets.sha256`) still runs, now on the portable
SHA-256.
```

with:

```
The manifest check does **not** run in the web build: assets baked in at build
time cannot drift, and the three non-runtime manifest entries (`BUMPY.EXE`,
`BUMP-Y.EXE`, `OLD-GAMES.NFO`, ~400 KB) are not shipped at all. The source tree
the staging directory copies from is verified on the desktop side by
`tests/cpp/asset_manifest_test.cpp`, which is a stronger guarantee at zero
runtime cost.
```

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt CMakePresets.json src/web/shell.html \
        src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp \
        docs/superpowers/specs/2026-08-20-web-port-classic-design.md
git commit -m "build(web): Emscripten target linking the flat SDL_Renderer path

Stage 1 compiles no GL: platform_gl3 is excluded and every gl_ use in SdlApp is
guarded, so the web build takes the SDL_Renderer fallback the desktop build
already carries. Assets are staged to a build directory and preloaded at /assets
-- only the 47 files the game opens, not the DOS executables the manifest also
lists. The loop still blocks; Asyncify wiring lands next."
```

---

### Task 3: Asyncify yield — first frame in a browser tab

**Files:**
- Modify: `src/platform_sdl3/sdl_app.cpp` (`wait_next_tick`)

**Interfaces:**
- Consumes: the `-sASYNCIFY` link option from Task 2.
- Produces: a run loop that returns control to the browser once per tick.

- [ ] **Step 1: Add the Emscripten include**

At the top of `src/platform_sdl3/sdl_app.cpp`, after the existing includes:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
```

- [ ] **Step 2: Replace the wait with a yielding wait**

Replace the body of the `wait_next_tick` lambda:

```cpp
    auto wait_next_tick = [&](Uint64 tick_period) {
        next_frame += tick_period;
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < next_frame) {
#ifdef __EMSCRIPTEN__
            // A blocking wait freezes the tab: the browser only composites, services
            // input, and runs audio when it has the thread back. Asyncify makes
            // emscripten_sleep a real unwind/rewind, so this yields instead of spinning.
            // The deadline arithmetic is deliberately unchanged -- the pace still comes
            // from the wall clock, not from the display refresh rate.
            for (;;) {
                const Uint64 tick = SDL_GetPerformanceCounter();
                if (tick >= next_frame) {
                    break;
                }
                const Uint64 remaining_ms = ((next_frame - tick) * 1000) / perf_freq;
                emscripten_sleep(remaining_ms > 1 ? static_cast<unsigned>(remaining_ms - 1) : 0);
            }
#else
            const Uint64 remaining = next_frame - now;
            const Uint64 remaining_ms = (remaining * 1000) / perf_freq;
            if (remaining_ms > 1) {
                SDL_Delay(static_cast<Uint32>(remaining_ms - 1));
            }
            while (SDL_GetPerformanceCounter() < next_frame) {
                // spin the last <=1ms
            }
#endif
        } else {
            next_frame = now;  // behind schedule -> resync
        }
    };
```

- [ ] **Step 3: Build and serve**

```
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

- [ ] **Step 4: Verify the first frame**

Open `http://localhost:8000/bumpy.html`. Because Task 2's placeholder shell has
no click gate and `-sINVOKE_RUN=0` is set, run `Module.callMain([])` from the
browser console to start the game.

Expected: the BUMPRESE.VEC splash screen renders, the tab stays responsive
(scrolling and devtools work), and no exception appears in the console. Sound
may be silent or glitchy at this point — Tasks 7 and 8 fix that.

- [ ] **Step 5: Verify the desktop build is untouched**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS, all tests.

- [ ] **Step 6: Commit**

```bash
git add src/platform_sdl3/sdl_app.cpp
git commit -m "feat(web): yield to the browser instead of blocking between ticks

wait_next_tick slept and then spun, which freezes a browser tab -- nothing
composites, no input arrives, no audio runs. Under Emscripten it now yields
through emscripten_sleep, which Asyncify turns into a real unwind/rewind. The
deadline arithmetic is untouched, so the pace still comes from the wall clock
rather than the display refresh rate; whether that survives browser timer
clamping is measured next."
```

---

### Task 4: Frame-pacing probe and gate

The one open technical risk in the spec. The game ticks at 70.086 Hz; browsers
clamp timers and composite at 60 Hz. This task measures the real rate rather than
assuming it.

**Files:**
- Modify: `src/platform_sdl3/sdl_app.cpp` (probe, behind a compile flag)
- Modify: `CMakeLists.txt` (opt-in `BUMPY_PACE_PROBE` option)

**Interfaces:**
- Consumes: the yielding wait from Task 3.
- Produces: a measurement, and a go/no-go decision on the Asyncify approach.

- [ ] **Step 1: Add the probe**

At the top of `src/platform_sdl3/sdl_app.cpp`, add `#include <cstdio>` to the
existing includes. Then, inside `run()`, immediately after the
`if (level_ticked) { wait_next_tick(level_period); }` / `else { ... }` block at
the end of the loop body, insert:

```cpp
#if defined(BUMPY_PACE_PROBE)
        // Pace probe (opt-in, never in a shipped build): report the wall-clock time for
        // a fixed number of in-level game ticks. Expected at 70.086 Hz (HARD): 4280.9 ms;
        // at 35.043 Hz (EASY): 8561.8 ms. Desktop and browser must agree within 2%.
        if (level_ticked) {
            static int probe_ticks = 0;
            static Uint64 probe_start = 0;
            if (probe_ticks == 0) {
                probe_start = SDL_GetPerformanceCounter();
            }
            if (++probe_ticks == 300) {
                const double ms = static_cast<double>(SDL_GetPerformanceCounter() - probe_start) *
                                  1000.0 / static_cast<double>(perf_freq);
                std::printf("[pace] 300 level ticks in %.1f ms (%.3f Hz)\n", ms, 300000.0 / ms);
                std::fflush(stdout);
                probe_ticks = 0;
            }
        }
#endif
```

- [ ] **Step 2: Add the opt-in build option**

In `CMakeLists.txt`, after the `project(...)` line:

```cmake
# Opt-in instrumentation for comparing in-level tick pacing between the desktop and
# web builds. Off by default: it prints to stdout every 300 ticks.
option(BUMPY_PACE_PROBE "Print in-level tick pacing measurements" OFF)
if(BUMPY_PACE_PROBE)
  target_compile_definitions(bumpy_platform_sdl3 PRIVATE BUMPY_PACE_PROBE)
endif()
```

Note: the `if(BUMPY_PACE_PROBE)` block must come *after*
`add_library(bumpy_platform_sdl3 ...)`, so put the `option(...)` line near the top
and the `if(...)` block right after the platform library is defined.

- [ ] **Step 3: Measure the desktop baseline**

```
cmake -S . -B build/pace-probe -G Ninja -DBUMPY_PACE_PROBE=ON -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
cmake --build build/pace-probe
```

Run `build/pace-probe/bumpy_port.exe`, start a game on HARD, and hold a direction
so ticks keep running. Record at least three `[pace]` lines.

Expected: close to `4280.9 ms (70.086 Hz)`.

- [ ] **Step 4: Measure the browser**

```
cmake --preset web-release -DBUMPY_PACE_PROBE=ON
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

Open the page, `Module.callMain([])`, start a game on HARD, hold a direction, and
read the `[pace]` lines from the browser console (`Module.print` routes stdout
there).

Record at least three lines.

- [ ] **Step 5: Apply the gate**

Compute `abs(browser_hz - desktop_hz) / desktop_hz`.

- **≤ 2%:** the Asyncify approach holds. Record both figures in the spec's
  "Open risk: frame pacing" section, change its heading from
  `#### Open risk` to `#### Resolved: frame pacing`, and state the measured
  numbers. Continue to Task 5.
- **> 2%:** stop and report to the user before writing more code. The fallback is
  the rAF + catch-up design already named in the spec, which is a large enough
  change to deserve its own decision rather than being applied silently.

- [ ] **Step 6: Commit**

```bash
git add src/platform_sdl3/sdl_app.cpp CMakeLists.txt \
        docs/superpowers/specs/2026-08-20-web-port-classic-design.md
git commit -m "test(web): opt-in pace probe, and the measured browser tick rate

The spec gated the Asyncify approach on the browser holding the original
70.086 Hz within 2% rather than drifting to the compositor's 60 Hz. This adds
the instrumentation that answers it -- 300 in-level ticks timed against the
performance counter, printed on both platforms -- and records the result."
```

---

### Task 5: Fixed 4:3 in the web build

**Files:**
- Modify: `src/platform_sdl3/sdl_app.cpp` (force `square_pixels`, drop `Alt+A`)
- Modify: `src/game/settings_overlay.h` (`kVideoRowCount`)
- Modify: `src/game/settings_overlay.cpp` (video-page row mapping)
- Modify: `src/video/settings_renderer.cpp` (video-page rows)

**Interfaces:**
- Consumes: nothing new.
- Produces: `kVideoRowCount == 2` under Emscripten (3D, FULLSCREEN); unchanged at
  3 elsewhere.

- [ ] **Step 1: Force 4:3 and remove the hotkey**

In `src/platform_sdl3/sdl_app.cpp`, in `run()`, immediately after `square_pixels`
is seeded from `config` (just before the `apply_aspect` lambda around line 285):

```cpp
#ifdef __EMSCRIPTEN__
    // Product decision: the web build is 4:3 only. 4:3 stays the base geometry, so any
    // future widescreen extends the 4:3 view rather than stretching a 16:10 image.
    square_pixels = false;
    config.square_pixels = false;
#endif
```

Then guard the `Alt+A` branch of the key handler. It is an `else if` in a chain,
so wrap exactly that branch:

```cpp
#ifndef __EMSCRIPTEN__
                } else if (event.key.key == SDLK_A && (event.key.mod & SDL_KMOD_ALT)) {
                    // Alt+A: flip the flat-path display aspect between 16:10 and
                    // 4:3 (CRT). The 3D scene is always 4:3-corrected; this only
                    // shows once back on a flat screen.
                    square_pixels = !square_pixels;
                    apply_aspect();
                    config.square_pixels = square_pixels;
                    persist();
#endif
```

Also guard the `SettingsEvent::toggle_aspect` case in the overlay-event handling
inside `run()` with `#ifndef __EMSCRIPTEN__` — the event can no longer be emitted
in the web build, and leaving the case would reference nothing.

- [ ] **Step 2: Shrink the video page**

In `src/game/settings_overlay.h`, replace:

```cpp
inline constexpr int kVideoRowCount = 3;  // 3D, ASPECT, FULLSCREEN
```

with:

```cpp
#ifdef __EMSCRIPTEN__
inline constexpr int kVideoRowCount = 2;  // 3D, FULLSCREEN (the web build is 4:3 only)
#else
inline constexpr int kVideoRowCount = 3;  // 3D, ASPECT, FULLSCREEN
#endif
```

In `src/game/settings_overlay.cpp`, replace the video case:

```cpp
        case SettingsPage::video:
            switch (cursor_row_) {
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
            }
            return SettingsEvent::none;
```

with:

```cpp
        case SettingsPage::video:
            switch (cursor_row_) {
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
#ifdef __EMSCRIPTEN__
            case 1: return SettingsEvent::toggle_fullscreen;
#else
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
#endif
            }
            return SettingsEvent::none;
```

- [ ] **Step 3: Drop the ASPECT row from the renderer**

In `src/video/settings_renderer.cpp`, replace:

```cpp
    case SettingsPage::video:
        title("VIDEO");
        row(0, "3D", view.render3d ? "ON" : "OFF");
        row(1, "ASPECT", view.square_pixels ? "16.10" : "4.3");
        row(2, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
        break;
```

with:

```cpp
    case SettingsPage::video:
        title("VIDEO");
        row(0, "3D", view.render3d ? "ON" : "OFF");
#ifdef __EMSCRIPTEN__
        row(1, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#else
        row(1, "ASPECT", view.square_pixels ? "16.10" : "4.3");
        row(2, "FULLSCREEN", view.fullscreen ? "ON" : "OFF");
#endif
        break;
```

- [ ] **Step 4: Verify the desktop build and tests are unaffected**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS. `settings_overlay_test.cpp:52` still asserts row 1 emits
`toggle_aspect`, and `settings_renderer_test.cpp` still measures the `"16.10"`
value column — both compile the `#else` branch, so both must be untouched. If
either fails, the guards are wrong.

- [ ] **Step 5: Verify the web build**

```
cmake --build --preset web-release
```

Then in the browser, open the Tab overlay → VIDEO. Expect exactly two rows: `3D`
(showing `OFF`, not selectable — no GL in stage 1) and `FULLSCREEN`. The picture
must be 4:3 letterboxed, and `Alt+A` must do nothing.

- [ ] **Step 6: Commit**

```bash
git add src/platform_sdl3/sdl_app.cpp src/game/settings_overlay.h \
        src/game/settings_overlay.cpp src/video/settings_renderer.cpp
git commit -m "feat(web): 4:3 only, with no ASPECT row or Alt+A

The 16:10 option exists on the desktop as the art-as-authored/DOSBox-X reference
look. The web build ships one geometry instead, and 4:3 is the one the game is
meant to be shown in -- so the row and the hotkey come out rather than sitting
there as a second way to get it wrong. 4:3 also stays the base geometry for any
later widescreen work, which extends the view rather than stretching the image."
```

---

### Task 6: Config persistence via localStorage

**Files:**
- Modify: `src/core/port_config.cpp`

**Interfaces:**
- Consumes: `serialize_port_config` / `parse_port_config` (existing, unchanged).
- Produces: `load_port_config` / `save_port_config` backed by `localStorage` under
  Emscripten; identical signatures, so no caller changes.

- [ ] **Step 1: Add the localStorage bridge**

In `src/core/port_config.cpp`, after the existing includes and **before**
`namespace bumpy {` (EM_JS declares a C function, so it must not sit inside a
namespace):

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

#include <array>
#include <string_view>

// The browser has no directory next to an executable, so the five flags live in one
// localStorage entry holding exactly the key=value text the desktop file format uses.
// Bytes are copied through HEAPU8 rather than the UTF-8 helpers: the config is ASCII by
// construction, and this needs no EM_JS_DEPS declaration to link.
EM_JS(int, bumpy_cfg_read, (char* out, int capacity), {
    var text;
    try {
        text = localStorage.getItem('bumpy_port_cfg');
    } catch (e) {
        return -1;  // storage disabled (private mode, blocked cookies) -> use defaults
    }
    if (text === null || text.length + 1 > capacity) {
        return -1;
    }
    for (var i = 0; i < text.length; ++i) {
        HEAPU8[out + i] = text.charCodeAt(i) & 0xff;
    }
    HEAPU8[out + text.length] = 0;
    return text.length;
});

EM_JS(void, bumpy_cfg_write, (const char* text, int length), {
    var s = '';
    for (var i = 0; i < length; ++i) {
        s += String.fromCharCode(HEAPU8[text + i]);
    }
    try {
        localStorage.setItem('bumpy_port_cfg', s);
    } catch (e) {
        // Storage full or disabled: settings just do not survive the reload.
    }
});
#endif
```

- [ ] **Step 2: Branch the load and save**

Replace `load_port_config`:

```cpp
PortConfig load_port_config(const std::filesystem::path& path) noexcept {
#ifdef __EMSCRIPTEN__
    (void)path;  // the web build persists to localStorage, not to a file
    std::array<char, 1024> buffer{};
    const int length = bumpy_cfg_read(buffer.data(), static_cast<int>(buffer.size()));
    if (length < 0) {
        return {};
    }
    return parse_port_config(std::string_view(buffer.data(), static_cast<std::size_t>(length)));
#else
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parse_port_config(text.str());
    } catch (...) {
        return {};
    }
#endif
}
```

Replace `save_port_config`:

```cpp
bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept {
#ifdef __EMSCRIPTEN__
    (void)path;
    try {
        const std::string text = serialize_port_config(config);
        bumpy_cfg_write(text.c_str(), static_cast<int>(text.size()));
        return true;
    } catch (...) {
        return false;
    }
#else
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << serialize_port_config(config);
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
#endif
}
```

- [ ] **Step 3: Verify the desktop build and tests**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS — `port_config_test.cpp` exercises the file path, which is
unchanged.

- [ ] **Step 4: Verify persistence in the browser**

```
cmake --build --preset web-release
```

Open the page, start the game, open the Tab overlay → AUDIO, turn `MUSIC` off,
close the overlay, then reload the page.

Expected: `MUSIC` is still `OFF`. Also check
`localStorage.getItem('bumpy_port_cfg')` in the console — it should show the
`key=value` text with `music=0`.

- [ ] **Step 5: Commit**

```bash
git add src/core/port_config.cpp
git commit -m "feat(web): persist port settings to localStorage

Same key=value text as bumpy_port.cfg, one entry, no IDBFS or syncfs -- the
payload is five booleans and the format already tolerates unknown keys. Storage
being unavailable (private mode, blocked cookies) degrades to defaults rather
than failing, matching how the desktop path treats an unreadable file."
```

---

### Task 7: HTML shell

**Files:**
- Modify: `src/web/shell.html` (replace the Task 2 placeholder)

**Interfaces:**
- Consumes: `-sINVOKE_RUN=0` and `-sEXPORTED_RUNTIME_METHODS=callMain` from Task 2.
- Produces: a click gate that starts `main()` inside a user gesture — the
  precondition Task 8's audio relies on.

- [ ] **Step 1: Write the shell**

Replace `src/web/shell.html` entirely:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bumpy3D</title>
<style>
  html, body { margin: 0; height: 100%; background: #000; color: #c8c8c8;
               font: 14px/1.4 "Courier New", monospace; overflow: hidden; }
  #frame { position: fixed; inset: 0; display: flex;
           align-items: center; justify-content: center; }
  canvas { background: #000; display: block; max-width: 100%; max-height: 100%;
           image-rendering: pixelated; outline: none; }
  #gate { position: fixed; inset: 0; z-index: 2; display: flex; gap: 1.5rem;
          flex-direction: column; align-items: center; justify-content: center;
          background: #000; cursor: pointer; }
  #gate h1 { margin: 0; font-size: 28px; letter-spacing: .3em; color: #e0c060; }
  #gate p  { margin: 0; letter-spacing: .2em; }
  #bar  { width: 240px; height: 6px; border: 1px solid #444; }
  #fill { height: 100%; width: 0; background: #e0c060; transition: width .2s; }
</style>
</head>
<body>
<div id="frame"><canvas id="canvas" tabindex="-1"></canvas></div>
<div id="gate">
  <h1>BUMPY3D</h1>
  <p id="status">LOADING</p>
  <div id="bar"><div id="fill"></div></div>
</div>
<script>
var canvas = document.getElementById('canvas');
var gate = document.getElementById('gate');
var statusLine = document.getElementById('status');
var fill = document.getElementById('fill');
var ready = false;

// Arrows, space and Tab must reach the game rather than scrolling the page or moving
// focus off the canvas -- Tab is the settings-overlay key.
var swallowed = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Tab'];
window.addEventListener('keydown', function (e) {
  if (swallowed.indexOf(e.key) !== -1) { e.preventDefault(); }
}, true);

var Module = {
  canvas: canvas,
  print: function (text) { console.log(text); },
  printErr: function (text) { console.error(text); },
  setStatus: function (text) {
    var progress = /([0-9.]+)\/([0-9.]+)/.exec(text);
    if (progress) {
      fill.style.width = (100 * parseFloat(progress[1]) / parseFloat(progress[2])) + '%';
    }
  },
  onRuntimeInitialized: function () {
    ready = true;
    fill.style.width = '100%';
    statusLine.textContent = 'CLICK TO PLAY';
  }
};

// main() is held back (-sINVOKE_RUN=0) until this click: browsers refuse to start audio
// outside a user gesture, and the audio device opens during SdlApp construction.
gate.addEventListener('click', function () {
  if (!ready) { return; }
  gate.style.display = 'none';
  canvas.focus();
  Module.callMain([]);
});
</script>
{{{ SCRIPT }}}
</body>
</html>
```

- [ ] **Step 2: Build and check the gate**

```
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

Open `http://localhost:8000/bumpy.html`.

Expected: the loading bar fills, the text changes to `CLICK TO PLAY`, and a click
starts the game with no console call needed. Arrow keys and space must not scroll
the page; Tab must open the settings overlay rather than moving browser focus.

- [ ] **Step 3: Commit**

```bash
git add src/web/shell.html
git commit -m "feat(web): page shell with a click-to-play gate

Holds main() until a click so the audio device opens inside a user gesture, and
swallows the keys the browser would otherwise steal -- arrows and space scroll
the page, and Tab (the settings-overlay key) moves focus off the canvas."
```

---

### Task 8: Audio push mode

Under Asyncify the main stack is routinely unwound mid-frame. SDL's audio thread
calling back into `engine_.render()` at that moment is a re-entrancy hazard, so
the web build drives the stream from the run loop instead.

**Files:**
- Modify: `src/platform_sdl3/sdl_audio.h` (add `pump()`)
- Modify: `src/platform_sdl3/sdl_audio.cpp` (callback-less stream + `pump()`)
- Modify: `src/platform_sdl3/sdl_app.h` (`set_audio_pump`)
- Modify: `src/platform_sdl3/sdl_app.cpp` (call `pump()` once per iteration)
- Modify: `src/app/main.cpp` (wire the pump)

**Interfaces:**
- Consumes: `AudioEngine::kSampleRate` (49715), `AudioEngine::render(float*, std::size_t)`.
- Produces:
  - `void bumpy::SdlAudio::pump()` — no-op on desktop.
  - `void bumpy::SdlApp::set_audio_pump(SdlAudio* audio) noexcept`.

- [ ] **Step 1: Declare pump()**

In `src/platform_sdl3/sdl_audio.h`, add to the public section after the deleted
copy operations:

```cpp
    // Web build: top up the audio queue from the run loop. A no-op on desktop, where
    // SDL's audio thread pulls through callback(). Safe (and cheap) to call every frame.
    void pump();
```

- [ ] **Step 2: Open the stream without a callback under Emscripten**

In `src/platform_sdl3/sdl_audio.cpp`, replace the `SDL_OpenAudioDeviceStream` line
in the constructor:

```cpp
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::callback, this);
```

with:

```cpp
#ifdef __EMSCRIPTEN__
    // Push mode: no callback. Asyncify routinely leaves the main stack unwound between
    // ticks, and a JS-driven audio callback re-entering engine_.render() at that moment
    // is a corruption hazard. The run loop calls pump() instead.
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
#else
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::callback, this);
#endif
```

- [ ] **Step 3: Implement pump()**

Add at the end of `src/platform_sdl3/sdl_audio.cpp`, before the closing
`}  // namespace bumpy`:

```cpp
void SdlAudio::pump() {
#ifdef __EMSCRIPTEN__
    if (!stream_) {
        return;
    }
    // Hold ~100 ms queued: long enough to ride out browser timer jitter (a yielded tick
    // can overshoot by several milliseconds), short enough that SFX stay in step with
    // what is on screen.
    constexpr int kTargetBytes =
        static_cast<int>(AudioEngine::kSampleRate / 10) * static_cast<int>(sizeof(float));
    const int queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0 || queued >= kTargetBytes) {
        return;
    }
    const std::size_t frames = static_cast<std::size_t>(kTargetBytes - queued) / sizeof(float);
    if (scratch_.size() < frames) {
        scratch_.resize(frames);
    }
    engine_.render(scratch_.data(), frames);
    SDL_PutAudioStreamData(stream_, scratch_.data(), static_cast<int>(frames * sizeof(float)));
#endif
}
```

- [ ] **Step 4: Let SdlApp drive the pump**

In `src/platform_sdl3/sdl_app.h`, add the include:

```cpp
#include "platform_sdl3/sdl_audio.h"
```

Add to the public section, after `gl_available()`:

```cpp
    // Optional audio pump, driven once per loop iteration. Null means desktop (SDL's
    // audio thread pulls instead) or a failed device open (the game runs muted).
    void set_audio_pump(SdlAudio* audio) noexcept { audio_pump_ = audio; }
```

Add to the private members:

```cpp
    SdlAudio* audio_pump_{};
```

In `src/platform_sdl3/sdl_app.cpp`, inside `run()`, immediately before the
`if (level_ticked)` / `wait_next_tick` block at the end of the loop body:

```cpp
        // Top up the audio queue before yielding: the wait below hands the thread back
        // to the browser for most of a tick, during which nothing else can feed it.
        if (audio_pump_) {
            audio_pump_->pump();
        }
```

- [ ] **Step 5: Wire it in main.cpp**

In `src/app/main.cpp`, after the `try { sdl_audio.emplace(audio_engine); } catch ...`
block and before the `return sdl.run(...)`:

```cpp
    if (sdl_audio) {
        sdl.set_audio_pump(&*sdl_audio);
    }
```

- [ ] **Step 6: Verify the desktop build is unaffected**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

Expected: PASS. Then run `build/windows-debug/Debug/bumpy_port.exe` and confirm
the intro music still plays on the splash screen — `pump()` compiles to nothing
on desktop, so the callback path must be untouched.

- [ ] **Step 7: Verify audio in the browser**

```
cmake --build --preset web-release
```

Expected: intro music plays on the splash screen, SFX fire on bumps and springs,
and the Tab overlay's MUSIC/SOUND toggles work. Listen for at least 30 seconds of
music for dropouts or crackle. If the audio breaks up, raise the buffer target
(`kSampleRate / 10` → `/ 5` for 200 ms) and re-test.

- [ ] **Step 8: Commit**

```bash
git add src/platform_sdl3/sdl_audio.h src/platform_sdl3/sdl_audio.cpp \
        src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp
git commit -m "feat(web): drive audio by pushing from the run loop

SDL's pull callback runs on the audio thread, which under Asyncify can re-enter
engine_.render() while the main stack is unwound. The web build opens a
callback-less stream and tops up ~100 ms of queue once per tick instead, which
also absorbs the timer jitter a yielded wait introduces. Desktop keeps the
callback: pump() compiles to nothing there."
```

---

### Task 9: Acceptance pass and documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/PROJECT_STATUS.md`

**Interfaces:**
- Consumes: everything above.
- Produces: a verified, documented web build.

- [ ] **Step 1: Build a clean release**

```
cmake --preset web-release -DBUMPY_PACE_PROBE=OFF
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

- [ ] **Step 2: Run the full acceptance list**

Work through every item and record the result. Any failure stops the task and is
reported before it is patched.

1. Page loads; the progress bar fills; `CLICK TO PLAY` appears.
2. Click starts the game; the BUMPRESE.VEC splash renders.
3. Intro music plays on the splash and stops when it is left.
4. Menu navigation works; difficulty (EASY/MEDIUM/HARD) can be selected.
5. World map: movement, the cloud jump, and entering a board.
6. In-level play: bounce, springs, tile bumps, the exit portal, death, lives.
7. SFX fire and match the desktop build.
8. Tab overlay opens on menu, map, and level; VIDEO shows exactly two rows.
9. MUSIC and SOUND toggles take effect immediately.
10. PASSWORDS page lists the codes for worlds 2-9.
11. The password screen accepts a code and enters that world.
12. High scores: name entry, caret blink, and the table render.
13. Game over and the DESSFIN.VEC outro render.
14. Settings survive a page reload.
15. `Alt+Enter` enters and leaves fullscreen.
16. All nine worlds load (enter each via its password).
17. The tab stays responsive throughout; no console errors.
18. Aspect is 4:3, letterboxed, with no stretching at any window size.

- [ ] **Step 3: Verify the desktop build one last time**

```
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
build\windows-debug\Debug\bumpy_port.exe
```

Expected: all tests pass, and the desktop game still launches fullscreen in 4:3
with 3D on — the shipped defaults must be exactly as before this branch.

- [ ] **Step 4: Document the web build**

Add to `README.md`, after the existing build instructions:

```markdown
## Browser build

The classic (flat, original-look) presentation also builds for the browser with
Emscripten. The 3D and HD modes are desktop-only for now.

Prerequisites: [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
(tested with 6.0.8). The `web-release` preset in `CMakePresets.json` hardcodes the
toolchain and Ninja paths for the author's machine — adjust them to yours.

```
cmake --preset web-release
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

Then open `http://localhost:8000/bumpy.html`.

The build bakes the original data files into `bumpy.data` (586 KB), so the output
directory is a self-contained static site. It needs no special server headers.

Differences from the desktop build:

- 4:3 only — no ASPECT row and no `Alt+A`.
- No 3D (`Alt+3`) or HD (`Alt+H`) mode.
- Settings persist to `localStorage` instead of `bumpy_port.cfg`.
```

In `docs/PROJECT_STATUS.md`, update the "Last updated" note at the top to name
this work (keeping the existing parenthetical structure — the newest item first,
the prior one after "Prior:"), and add a Roadmap entry in the established
`**Stage N — Name — DONE.**` style:

```markdown
- **Web port (browser), stage 1 — DONE.** The classic flat presentation builds
  for WebAssembly with Emscripten and runs from a static URL, reusing the
  `SDL_Renderer` fallback path (no GL compiled). Assets are baked into the build;
  settings persist to `localStorage`; 4:3 only. Stage 2 — WebGL2 context plus the
  five shaders ported to GLES 3.00, which brings back `Alt+3` and `Alt+H` — is
  not started. See `docs/superpowers/specs/2026-08-20-web-port-classic-design.md`.
```

Also add `platform_gl3` to the Architecture list's note that it is desktop-only
in the web build, if that list is still accurate at this point.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/PROJECT_STATUS.md
git commit -m "docs: browser build instructions and its differences from desktop"
```

- [ ] **Step 6: Report to the user**

Summarise: the acceptance results, the measured pacing figures from Task 4, the
output size, and anything that had to be worked around. Do **not** merge — the
user decides that, and the visual/feel check is theirs to make.

---

## Notes for the executor

- **Never claim a step passed without running it.** Every "Expected:" line is a
  command whose real output must be seen.
- **Guard, never delete.** Every GL and aspect path removed from the web build is
  a seam stage 2 reopens. `#ifdef` it; do not strip it.
- **The desktop suite is the contract.** If a shared-code edit turns a desktop
  test red, the edit is wrong — not the test.
- **Task 4 is a real gate.** If the pacing measurement misses 2%, stop and report
  rather than silently switching to the rAF fallback.
