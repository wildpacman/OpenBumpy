# Bumpy web port — acceptance checklist

Branch `feat/web-port`. Everything here needs a human at a browser, which is why
none of it is done: no subagent drove your browser after you stopped it twice.

Below, **carried** marks a check that was deferred from a specific task rather
than merely being part of the routine list — those are the ones where nobody has
looked yet and where a defect would be least expected.

---

## 1. Build and serve

```
cmake --preset web-release -DBUMPY_PACE_PROBE=OFF
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

Then open <http://localhost:8000/bumpy.html>.

Expected artifacts in `build/web-release/`: `bumpy.html`, `bumpy.js`,
`bumpy.wasm` (**~2.0 MB**, 2,033,725 bytes — stage 1 measured ~1.78 MB; most of
the growth is `-fexceptions`, which stage 2 needed to make the WebGL2-absent
fallback and every other `catch` in the port actually degrade instead of
aborting the tab, see section 8), `bumpy.data` (**604,662 bytes** exactly —
600,148 bytes from the 47 original data files plus 4,514 bytes of GLSL shaders
that stage 2 added to the image, and a different number means the wrong set
got staged).

## 2. The page itself — carried (Task 7, never opened by anyone)

- [ ] Loading bar fills, text changes to **CLICK TO PLAY**
- [ ] A click starts the game — the BUMPRESE.VEC splash appears
      *(if the click does nothing, `callMain` failed to export; the review
      confirmed `Module["callMain"]=callMain` is present in `bumpy.js`, so this
      should hold — but it has never actually been clicked)*
- [ ] Arrow keys and space do **not** scroll the page
- [ ] **Tab opens the in-game settings overlay** rather than moving browser focus
- [ ] The page does **not** go fullscreen by itself. `PortConfig::fullscreen`
      defaults to `true`, but the web build deliberately does not apply it at
      startup — a page seizing the screen before being asked is hostile, and the
      request would be refused outside a user gesture anyway. Worth testing with
      a cleared `localStorage` (`localStorage.removeItem('bumpy_port_cfg')`),
      since that is what a first-time visitor gets

## 3. Display — carried (Task 5, half-verified)

The 4:3 geometry itself was measured before the session was stopped: on the
wider-than-4:3 window tested, the rendered content box came out at exactly
1.3333 with pillarboxing (bars on the left/right — SDL's letterbox
presentation mode produces side bars when the window is wider than the 4:3
content and top/bottom bars when it is narrower; "letterbox" names the SDL
mode, not the bar orientation you'll actually see). What was never checked:

- [ ] Tab overlay → VIDEO shows **exactly two rows**: **DIORAMA** (selectable,
      toggles the diorama — no longer the OFF-only, unselectable stage 1
      placeholder) and **FULLSCREEN**. No ASPECT row.
- [ ] **`Alt+A` does nothing** (on desktop it flips 16:10 ↔ 4:3; in the browser
      it must be inert)
- [ ] Picture stays 4:3 at several window sizes, never stretched — pillarboxed
      (side bars) when the window is wider than 4:3, letterboxed (top/bottom
      bars) when it is narrower

## 4. Settings persistence — carried (Task 6, never round-tripped)

- [ ] Tab → AUDIO → turn **MUSIC** off, close the overlay
- [ ] In the browser console: `localStorage.getItem('bumpy_port_cfg')` shows
      text containing `music=0`
- [ ] **Reload the page**, start again, open Tab → AUDIO: MUSIC still reads OFF

Worth a moment: this is the only state the web build persists. High scores are
session-only, in the browser and on desktop alike, exactly as the original was.

## 5. Audio — carried (Task 8, never heard by anyone)

The web build pushes samples from the run loop instead of using SDL's audio
callback, because a JS-driven callback re-entering the mixer while Asyncify has
the main stack unwound is a corruption hazard. The buffer targets ~100 ms.

- [ ] Intro music plays on the splash screen and stops when you leave it
- [ ] SFX fire on bumps and springs
- [ ] Tab → AUDIO toggles for MUSIC and SOUND both take effect immediately
- [ ] **Listen for at least 30 seconds** for dropouts or crackle. If it breaks
      up, the buffer target is the knob: `kSampleRate / 10` → `/ 5` in
      `src/platform_sdl3/sdl_audio.cpp` doubles it to 200 ms.
- [ ] **Music survives an open overlay.** On the splash screen, open Tab and
      leave it open for ~15 seconds: the music must keep playing. The pump used
      to sit at the bottom of the loop body, which the overlay path skips, so
      the queue emptied and stayed empty for as long as the overlay was up
- [ ] **Music survives a screen change.** Enter a board from the world map and
      listen across the edge-to-centre darken: no gap or click. That darken is
      ~285 ms of loop time against a 100 ms queue, and it too used to skip the
      pump

## 6. The game

- [ ] Menu navigation; difficulty EASY / MEDIUM / HARD selectable
- [ ] World map: movement, cloud jump, entering a board
- [ ] In-level: bounce, springs, tile bumps, exit portal, death, lives
- [ ] Password screen accepts a code and enters that world
- [ ] High scores: name entry, blinking caret, the table
- [ ] Game over screen, and the DESSFIN.VEC outro
- [ ] **All nine worlds load** — enter each via its password (Tab → PASSWORDS
      lists the codes for worlds 2-9)
- [ ] `Alt+Enter` enters and leaves fullscreen — **undock DevTools into its own
      window before testing this.** Docked DevTools takes its width out of the
      page, so the fullscreen viewport comes back narrower than the screen and
      the 4:3 picture looks stranded and off-centre. That is the measuring tool
      changing what it measures, not a bug: with DevTools undocked the picture
      fills the screen height and the black bars sit on the sides, which is
      correct 4:3 letterboxing on a 16:10 monitor.
- [ ] **Quitting signs off instead of dying.** Tab → QUIT (and separately,
      Escape from the menu) must bring back the gate reading **THANKS FOR
      PLAYING / CLICK TO RESTART**. Clicking it reloads into a fresh game with
      settings intact. Before this, the run loop simply ended and left a frozen
      canvas with no message and no way back but a manual reload
- [ ] The tab stays responsive throughout; no errors in the console

## 7. Frame pacing in a level — carried, and the one I promised not to hand-wave

Measured pacing on the **splash screen** came out at 70.0866 Hz in the browser
against 70.086 Hz on desktop — a 0.0008 % difference against a 2 % gate. But the
splash is a cheap frame. The browser spent 0.375 of each period working where
desktop spent 0.026, roughly fourteen times the share.

I have an argument for why that should not grow on a busy in-level frame — most
of the gap is per-yield Asyncify overhead, which is fixed per tick rather than
proportional to frame cost. **That is reasoning, not a measurement.** So:

```
cmake --preset web-release -DBUMPY_PACE_PROBE=ON
cmake --build --preset web-release
python -m http.server 8000 --directory build/web-release
```

Open the page, start a game on **HARD**, and play (or just hold a direction) for
a minute with the tab **in the foreground** — a backgrounded tab is timer-
throttled and the numbers become meaningless. Watch the browser console for:

```
[pace] 300 waits in ... ms -> ... Hz achieved, ... Hz requested, busy ...
```

- [ ] `achieved` stays near 70.086 Hz (HARD = one VGA retrace)
- [ ] **`busy` stays below 1.0** — at or above 1.0 means the frame work no
      longer fits the tick budget and the rate is limited by the work rather
      than by the waiting mechanism

If `busy` on a level frame lands anywhere near 1.0, tell me: the fallback the
spec holds in reserve is a `requestAnimationFrame`-driven yield with catch-up
ticks, and that is a real design change rather than a tweak.

Remember to rebuild with `-DBUMPY_PACE_PROBE=OFF` afterwards.

---

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
- [ ] **The WebGL2-absent fallback.** With WebGL2 disabled in the browser, the
      game should still start — in the flat presentation, with a console
      warning — rather than aborting the tab. This path was dead code until
      Task 3's fix round turned on exception handling (see below), so it has
      never actually been exercised.

If the diorama does not appear at all, the console will say why: a failed
context or a shader that would not compile is reported as
`warning: no usable GL context` or a GL info log, and the game falls back to the
flat presentation rather than showing a black screen.

**One narrow failure will not fall back, and leaves a dead tab instead of a
warning.** `SdlApp`'s constructor (`src/platform_sdl3/sdl_app.cpp`) creates an
`SDL_WINDOW_OPENGL` window, and if `GlPresenter` throws it destroys that window
and creates a plain one for `SDL_CreateRenderer`. On desktop that is correct.
In a browser a canvas hands out exactly one context type for its lifetime, so
the second `getContext` returns null and the fallback cannot succeed.

The two cases anyone is actually likely to hit are both fine. No WebGL2 at all
fails before any context is attached, so the canvas is still clean and the flat
fallback works — that is the checklist item above. A *diorama shader* failure
happens later, inside the frame loop, and falls back to the GL flat path
without touching the window. What is exposed is narrower than either:
`load_gl33` failing to resolve one entry point, or the flat
`kFlatVert`/`kFlatFrag` failing to compile. Both throw from the constructor
*after* a GL context has been attached to the canvas.

So if the tab dies rather than falling back, that is the likely cause — a known
narrow gap, not a mystery. The eventual fix, recorded so it need not be
re-derived: under Emscripten, skip the destroy/recreate entirely and hand the
existing `SDL_WINDOW_OPENGL` window straight to `SDL_CreateRenderer`. It was
left undone on purpose — the change is small, but it rewrites a fallback path
no test can reach, at the very end of the stage. A narrow documented failure
beats an unverified rewrite.

Worth a moment on cost, not correctness: the web build now compiles with
`-fexceptions` so that fallback (and every other `catch` in the port) actually
degrades instead of aborting the tab — a pre-existing stage 1 gap that stage 2
found only because it went looking for it. That bought correctness at roughly
203 KB of wasm, an ~11% increase over the pre-`-fexceptions` build; the
current build measures **2,033,725 bytes** total (see section 1).

Worth knowing, but there is nothing here to decide: the smaller, faster
alternative — `-fwasm-exceptions`, native Wasm exception handling — is not
available to this port. It is incompatible with Asyncify, and emcc 6.0.8 says
so itself when you ask for both:

```
em++: warning: ASYNCIFY=1 is not compatible with -fwasm-exceptions.
Parts of the program that mix ASYNCIFY and exceptions will not compile.
```

This build is `-sASYNCIFY`, and it mixes the two by construction: `main()`
wraps the whole game in a `try`, and the run loop inside it yields to the
browser through `emscripten_sleep` — which *is* Asyncify — on every frame
(`src/platform_sdl3/sdl_app.cpp`). So `-fexceptions` costs what it costs for as
long as the port needs Asyncify to give the browser its thread back. (An
earlier revision of this note offered `-fwasm-exceptions` as a download-size
decision for the owner to make. It is not one; the option does not exist here.)

---

---

## 9. Open right now — the fullscreen viewport fix, unverified

**Status: root cause found and fixed in `27c6177`; the fix has never been seen
working in a browser.** This is the one thing to do first in a new session.

### What was wrong

In fullscreen the picture sat in the bottom-left at 80% size. Measured, not guessed:

```
sdl 1646x1029 | px 1646x1029 | canvas 2058x1286 | screen 1646x1029 | sdlfs 1
```

SDL believed the window was 1646x1029 while the canvas backing store was
2058x1286. `glViewport` was sized from SDL's number, so it covered 80% of each
axis of the framebuffer, and GL's bottom-left origin is why the picture went
*down and left* rather than merely small.

SDL3's Emscripten backend sets the window size on fullscreen entry from
`emscripten_get_element_css_size` — the canvas's CSS *inline style*, which the
fullscreen strategy fills in from `screen.width/height`. Those ignore page zoom;
the backing store follows the zoomed layout viewport. At 80% browser zoom they
differ by exactly 1.25x.

### Why it looked like a ghost

Opening DevTools made it disappear. A DevTools window triggers a UI resize, and
`Emscripten_HandleResize` takes a *different* source for fullscreen windows —
`uiEvent->windowInnerWidth`, which is correct — so the act of observing the bug
repaired it. Two earlier attempts at this bug were derailed by that: the first
concluded "docked DevTools was shrinking the viewport, no defect", which was
wrong.

### How to verify the fix

The diagnostic in `6b91283` writes to a `localStorage` ring, precisely so the
evidence survives a reproduction made with DevTools closed.

1. Rebuild and serve, hard-reload the page, click to start.
2. In the console: `localStorage.removeItem('bumpy_diag')`
3. **Close DevTools completely.** Enter fullscreen, stay a few seconds, exit.
   Repeat twice.
4. Reopen the console: `console.log(localStorage.getItem('bumpy_diag'))`

**Pass criterion:** in lines with `sdlfs 1`, the `draw` field matches `canvas`
(both 2058x1286 on the machine where this was found). `px` will still read
1646x1029 — SDL still computes it wrongly, and that divergence in the same line
is the proof the fix is doing its job rather than the bug having wandered off.

By eye: the picture fills the screen with no offset.

### When it passes

`git revert 6b91283` to remove the diagnostic. Leave `27c6177`.

### If it fails

The numbers in the ring say which hypothesis died. `draw` == `px` and both wrong
means the canvas query is returning SDL's number after all; `draw` == `canvas`
but the picture still offset means the viewport is not the thing displacing it,
and the next place to look is `scene_frustum` in `src/video3d/`.

## What was verified without you

So you know where the floor is, independent of everything above:

- Desktop build and the full Catch2 suite green after every one of the nine
  tasks. Two test files did change, and it is worth knowing exactly how:
  `tests/cpp/sha256_test.cpp` is new (it covers the portable SHA-256 that
  replaced the BCrypt dependency), and `tests/cpp/vec_test.cpp` lost its own
  second, independent BCrypt SHA-256 implementation — it only linked at all
  because `bumpy_core` pulled in `bcrypt`, so dropping that link forced the
  change. It now calls the implementation under test. The digests it compares
  against are hardcoded constants recorded under BCrypt, so the new code still
  has to reproduce them bit-for-bit: the test remains an independent check, not
  a self-consistent one. No other test file on the branch was touched, and no
  existing assertion was weakened or removed.
- Every source file force-rebuilt for both targets at the end: **zero warnings**
  on either platform
- `bumpy.data` = 604,662 bytes: 600,148 bytes independently confirmed as the
  manifest's 50 entries minus the two DOS binaries and one text file nothing
  loads (`BUMPY.EXE`, `BUMP-Y.EXE`, `OLD-GAMES.NFO`), plus 4,514 bytes of
  GLSL shaders that stage 2 added to the asset image
- The splash screen rendering in a browser tab, with the tab staying responsive
  to scrolling and JS evaluation while the game loop ran
- The 4:3 content box measured at exactly 1.3333
- `callMain` confirmed exported in the built `bumpy.js`; the preload package
  confirmed to mount `/assets` before `onRuntimeInitialized` can fire
