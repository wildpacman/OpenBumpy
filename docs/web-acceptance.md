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
`bumpy.wasm` (~1.7 MB), `bumpy.data` (**600,148 bytes** exactly — that is the
byte sum of the 47 original data files, and a different number means the wrong
set got staged).

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

- [ ] Tab overlay → VIDEO shows **exactly two rows**: `3D` (showing OFF, not
      selectable — no GL in stage 1) and `FULLSCREEN`. No ASPECT row.
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

Worth a moment on cost, not correctness: the web build now compiles with
`-fexceptions` so that fallback (and every other `catch` in the port) actually
degrades instead of aborting the tab — a pre-existing stage 1 gap that stage 2
found only because it went looking for it. That bought correctness at
203,222 bytes of wasm, an 11.1% increase (final size **2,033,450 bytes**).
`-fwasm-exceptions` would be smaller and faster, but needs every linked
object — including SDL — built in agreement, and depends on browser support
for the Wasm exception-handling proposal. That is a download-size decision for
the owner to make, not a defect to fix.

---

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
- `bumpy.data` = 600,148 bytes, independently confirmed as the manifest's 50
  entries minus the two DOS binaries and one text file nothing loads
  (`BUMPY.EXE`, `BUMP-Y.EXE`, `OLD-GAMES.NFO`)
- The splash screen rendering in a browser tab, with the tab staying responsive
  to scrolling and JS evaluation while the game loop ran
- The 4:3 content box measured at exactly 1.3333
- `callMain` confirmed exported in the built `bumpy.js`; the preload package
  confirmed to mount `/assets` before `onRuntimeInitialized` can fire
