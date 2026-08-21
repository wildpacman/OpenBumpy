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

## 3. Display — carried (Task 5, half-verified)

The 4:3 geometry itself was measured before the session was stopped: the
rendered content box came out at exactly 1.3333, pillarboxed. What was never
checked:

- [ ] Tab overlay → VIDEO shows **exactly two rows**: `3D` (showing OFF, not
      selectable — no GL in stage 1) and `FULLSCREEN`. No ASPECT row.
- [ ] **`Alt+A` does nothing** (on desktop it flips 16:10 ↔ 4:3; in the browser
      it must be inert)
- [ ] Picture stays 4:3 letterboxed at several window sizes, never stretched

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

## 6. The game

- [ ] Menu navigation; difficulty EASY / MEDIUM / HARD selectable
- [ ] World map: movement, cloud jump, entering a board
- [ ] In-level: bounce, springs, tile bumps, exit portal, death, lives
- [ ] Password screen accepts a code and enters that world
- [ ] High scores: name entry, blinking caret, the table
- [ ] Game over screen, and the DESSFIN.VEC outro
- [ ] **All nine worlds load** — enter each via its password (Tab → PASSWORDS
      lists the codes for worlds 2-9)
- [ ] `Alt+Enter` enters and leaves fullscreen
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

## What was verified without you

So you know where the floor is, independent of everything above:

- Desktop build and the full Catch2 suite green after every one of the nine
  tasks, with no test file edited anywhere on the branch
- Every source file force-rebuilt for both targets at the end: **zero warnings**
  on either platform
- `bumpy.data` = 600,148 bytes, independently confirmed as the manifest's 50
  entries minus the three DOS binaries nothing loads
- The splash screen rendering in a browser tab, with the tab staying responsive
  to scrolling and JS evaluation while the game loop ran
- The 4:3 content box measured at exactly 1.3333
- `callMain` confirmed exported in the built `bumpy.js`; the preload package
  confirmed to mount `/assets` before `onRuntimeInitialized` can fire
