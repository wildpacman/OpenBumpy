# Bumpy web port — acceptance checklist

Branch `feat/web-port`. Everything here needs a human at a browser, which is why
almost none of it is done: no subagent drove your browser after you stopped it
twice. The exception is section 9, which was run and passed on 2026-08-22.

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
`bumpy.wasm` (**~2.0 MB**, 2,033,757 bytes — stage 1 measured ~1.78 MB; most of
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

## 5. Audio — heard and working (2026-08-22); the stress checks below are still open

The web build pushes samples from the run loop instead of using SDL's audio
callback, because a JS-driven callback re-entering the mixer while Asyncify has
the main stack unwound is a corruption hazard.

**The queue depth is a latency floor, not a comfort setting** — see section 10.
It was 100 ms and SFX audibly trailed the picture; it is now **60 ms**, set by
listening. Below 60 the queue runs dry and crackles.

- [ ] Intro music plays on the splash screen and stops when you leave it
- [ ] SFX fire on bumps and springs
- [ ] Tab → AUDIO toggles for MUSIC and SOUND both take effect immediately
- [ ] **Listen for at least 30 seconds** for dropouts or crackle, and especially
      across a world change, which is the longest stretch of loop time that could
      outrun the queue. If it breaks up, **do not raise the target** — that trades
      the lag straight back. Call `pump()` again after each wake inside the yield
      loop in `src/platform_sdl3/sdl_app.cpp` instead: it halves the longest
      unfed gap and costs no latency at all.
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
- [x] `Alt+Enter` enters and leaves fullscreen, and the picture fills the screen
      height with black bars on the left and right only — correct 4:3
      letterboxing on a 16:10 monitor. **Done 2026-08-22**, at 80% browser zoom
      with DevTools closed, which is the only condition under which the bug this
      finally caught was visible at all. Re-test it the same way: at 100% zoom
      every version of this code looks right, including the broken ones. See
      section 9. (An earlier revision of this line blamed docked DevTools for
      shrinking the viewport and called it "not a bug". That was wrong twice
      over, and is what let the defect sit unfixed through two attempts.)
- [ ] **Quitting signs off instead of dying.** Tab → QUIT (and separately,
      Escape from the menu) must bring back the gate reading **THANKS FOR
      PLAYING / CLICK TO RESTART**. Clicking it reloads into a fresh game with
      settings intact. Before this, the run loop simply ended and left a frozen
      canvas with no message and no way back but a manual reload
- [ ] The tab stays responsive throughout; no errors in the console

## 7. Frame pacing in a level — measured 2026-08-22, and it is fine

Splash-screen pacing had measured 70.0866 Hz in the browser against 70.086 Hz on
desktop, but the splash is a cheap frame, and the browser spent 0.375 of each
period working where desktop spent 0.026. Whether that held up on a busy in-level
frame was the open question. It does. Measured **in a level on EASY**:

```
pace 35.04 Hz achieved / 35.04 asked | busy 0.20 | worst tick 34.0 ms | late 0 of 140
```

- `achieved` equals `asked` to the reported precision — the game is **not** running
  slow. EASY is 2 retraces per step, 35.043 Hz, the same number the desktop build
  paces to from the same code.
- `busy 0.20` — a fifth of each period spent working. Ample headroom.
- **`late 0 of 140`** is the decisive one: not a single tick arrived past its own
  deadline, so the loop never hit the `next_frame = now` branch that abandons the
  deficit instead of catching up. That branch is the only way this design can
  drift slow, and it never fired.
- `worst tick 34.0 ms` against a 28.5 ms nominal period: real jitter, ~19% on the
  worst tick in 140. It does not accumulate — the deadlines are absolute, so a
  late tick is followed by a short one. This is the number to watch if motion
  ever looks uneven, and it is unrelated to the rate.

Still inferred rather than measured: **HARD**, which ticks at 70.086 Hz, halving
the budget. `busy` should land near 0.4 there (it was 0.375 on the splash at the
same rate), which is still comfortable — but nobody has run it.

- [ ] Play a level on **HARD** and confirm `busy` stays below 1.0

Reinstate the panel from `95c3a66` to measure it; `-DBUMPY_PACE_PROBE=ON` prints
the same pace numbers to the console instead. A backgrounded tab is timer-
throttled and its numbers are meaningless, so keep it in the foreground.

If `busy` on a HARD level frame ever lands near 1.0, the fallback the spec holds
in reserve is a `requestAnimationFrame`-driven yield with catch-up ticks — a real
design change rather than a tweak.

---

## 8. The diorama — seen in a browser and working (2026-08-22); details below still open

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
current build measures **2,033,757 bytes** total (see section 1).

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

## 9. Fullscreen geometry — FIXED and verified in a browser (2026-08-22)

Kept because it took three attempts and the failure mode is a trap anyone would
fall into again.

### What was wrong

Entering fullscreen through `SDL_SetWindowFullscreen` left **three different
answers to "how big is the screen"** live at the same time:

| number | comes from | measured |
|---|---|---|
| canvas element + its inline CSS | `screen.width/height`, via Emscripten's fullscreen strategy | 1646x1029 |
| canvas drawing buffer | `innerWidth/innerHeight`, via `Emscripten_HandleResize` | 2058x1286 |
| SDL's window size | the display mode, via `SDL_UpdateFullscreenMode` | 1646x1029 |

`screen.*` is expressed in zoom-independent pixels; the layout viewport a
fullscreen page gets follows the page zoom. **At 100% zoom all three agree and
nothing looks wrong. At 80% they differ by exactly 1.25x.** That is the whole
bug, and it is why it read as a ghost:

- invisible at the one zoom level most people never leave, and
- opening DevTools fires the resize that repairs one of the three, so the act of
  looking at it fixed it. Two attempts were derailed by exactly that — one of
  them concluded "docked DevTools shrinks the viewport, no defect", which was
  wrong and cost a round.

`27c6177` pointed `glViewport` at the canvas instead of at SDL's number. That was
a real correction to one consumer of the bad number, and it could not finish the
job, because the divergence itself was untouched — as were the canvas's own DOM
dimensions and the `SDL_Renderer` fallback path, which takes both its logical
presentation and its GLES viewport from SDL's window size.

### The fix

The web build asks the **DOM** for fullscreen, on the document element, and never
lets Emscripten's fullscreen strategy run (`platform_set_fullscreen` /
`platform_fullscreen`, `src/platform_sdl3/sdl_app.cpp`). Then:

- the canvas keeps its stylesheet size — 100% of a fixed, inset-0 frame — so
  nothing writes an inline pixel size onto it,
- the browser resizes that frame to the fullscreen viewport, and
- SDL's ordinary resizable path re-measures the canvas's **bounding client
  rect** — the one number that is true by definition — and carries both the
  window size *and* the drawing buffer to it.

One source, no zoom term. The shell dispatches one extra `resize` on the frame
after a `fullscreenchange`, because SDL re-measures on resize events and nothing
else, and the one the browser fires is not dependably after the new layout has
settled. `gl_drawable_size` from `27c6177` stays: correct by construction, free,
and a cheap guard against the class of bug recurring.

### The evidence it works

Measured in Chrome at 80% page zoom on a 175%-scaled display (`dpr 1.4`), which
is the condition that reproduced the bug — one recorded fullscreen frame:

```
sdl 2058x1286 | px 2058x1286 | draw 2058x1286 | canvas 2058x1286 |
rect 2058x1286 @0,0 | inner 2058x1286 | screen 1646x1029 | domfs 1
```

Every number equal, `screen` still disagreeing by the zoom factor and no longer
reaching anything. Confirmed by eye at the same time: the picture fills the
screen, diorama on.

### If fullscreen ever misbehaves again

Reproduce at **80% browser zoom with DevTools closed** — at 100% every version of
this code looks right, including the broken ones. Then check, in order: does the
canvas have an inline `style.width` (something re-enabled SDL's strategy); does
`getBoundingClientRect()` match `canvas.width/height` (SDL did not re-measure —
suspect the resize nudge in `src/web/shell.html`); does the drawn viewport match
the canvas (`gl_drawable_size` fell back to SDL).

Two loose ends, recorded rather than chased, because neither changes anything now:

- Which of the two mechanisms actually produced the *observed* bottom-left
  displacement was never pinned down. `emscripten_set_canvas_element_size` does
  **not** clear the inline CSS size in this emsdk (the comment in SDL's own
  source saying "set_canvas_size unsets this" is stale), so the canvas box may
  have stayed at 80% — but the Fullscreen API's UA stylesheet forces
  `width/height: 100% !important` on the fullscreen element, which should
  override it, leaving GL's bottom-left origin as the cause. Both are gone.
- `emscripten_request_fullscreen("html", true)` would be an alternative to the
  direct DOM call, keeping Emscripten's defer-until-next-eligible-input
  machinery. It is not needed here: the loop handles the keypress well inside the
  browser's transient activation window, which is the same thing the SDL path
  relied on. Worth remembering if a browser ever tightens that.

---

## 10. Audio latency — where the third of a second went

Reported from a browser session: SFX trailed the picture by roughly a third of a
second, which the desktop build does not do. Measured rather than argued:

| term | ms | ours? |
|---|---|---|
| queue of already-rendered audio (`pump()` target) | 100 | **yes** |
| `AudioContext.baseLatency` | 10 | no |
| `AudioContext.outputLatency` (device) | 40 | no |

The queue is the part that matters and the part desktop does not have. `pump()`
renders *ahead* of the device, so an effect triggered this frame is mixed behind
everything already queued and cannot play until that drains. Desktop's callback
pulls just in time and has no such floor — which is the whole reason one sounded
late and the other did not.

Set by listening, not by arithmetic: at 100 ms the lag was obvious, at **60 ms it
was gone**, and below 60 the queue began to run dry and crackle. The floor makes
sense — `pump()` runs once per game tick, so the queue must outlast one tick with
margin, and a tick is 28.5 ms at the half rate against a worst tick measured at
34 ms. 35 ms is barely one and a quarter ticks; 60 is a bit over two.

Worth knowing before touching it again: **raising the target trades the lag
straight back**. If crackle ever appears, call `pump()` again after each wake
inside the yield loop in `src/platform_sdl3/sdl_app.cpp` — that halves the longest
unfed gap and costs no latency. And if someone reports far worse lag than the
50 ms of browser overhead above, ask what they are listening through before
touching any code: Bluetooth output alone runs 150-300 ms and no change here can
reach it.

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
