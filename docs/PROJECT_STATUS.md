# Bumpy Port — Project Status

Source of truth for new sessions. Last updated: 2026-08-22 (**web port
(browser), stage 2 done** — the diorama now runs in the browser on a WebGL2
(GLES 3.0) context; the mode is called DIORAMA in the UI on both platforms.
See the Roadmap entry below and `docs/web-acceptance.md`. Prior: web port
(browser), stage 1 implemented).

## Goal

A **playable** native Windows 11 port of *Bumpy's Arcade Fantasy* in C++/SDL3
that reads the original game's resources directly. Faithful to the original's
look, timing, and behavior — not a bit-for-bit preservation project.

First slice (definition of done): the original menu and the first level are
playable from launch through win/loss and back to the menu, reading the supplied
original files.

## Approach

1. **Pragmatic.** Optimize for reaching a playable game. Recover formats and
   logic accurately enough to be correct; do not add reproducibility ceremony
   that does not move the port forward.
2. **Binary-only sources.** Recover behavior from `BUMPY.EXE` and the original
   files — not from community docs. Screenshots/video are for visual comparison
   only.

Game logic stays independent of SDL3, refresh rate, and floating point. SDL3 is
only a platform adapter.

## Architecture

- `core` — fixed tick, integer math, RNG, indexed framebuffer.
- `game` — menu, level, physics, collision, objects, state transitions.
- `resources` — direct readers/decoders for the original formats.
- `video` — palette and frame composition over an indexed 320×200 buffer.
- `audio` — music, instrument bank, effects.
- `video3d` — CPU-side 3D diorama scene model (wall/slab/billboard geometry, blur).
- `platform_gl3` — the GL presenter + the diorama's GL programs/textures,
  built for both platforms: OpenGL 3.3 core on desktop, GLES 3.0 through
  WebGL2 in the browser. `SDL_Renderer` is the fallback when no usable GL
  context exists on either platform (see the web-port Roadmap entry below).
- `platform_sdl3` — window, input, timing, presentation.

## Roadmap

- **Stage 1 — `.VEC` container + title screen — DONE.** `.VEC` decoder and the
  title bitmap render natively.
- **Stage 2 — Menu — DONE (renders + interactive).** The full 320×200 menu
  screen deplanes with its embedded VGA palette, and the selection cursor (the
  `FLECHE.BIN` arrow sprite) draws at the active row and tracks `cursor_row`.
  Resource bundle, menu state machine, and SDL3 shell build and run.
- **Stage 3 — First level + all 9 worlds — DONE (all worlds playable).** Level data formats
  recovered and visually verified, the composed board renders natively, the **BUM
  entity sprites** draw from the uncompressed bank, the **world-map screen** is
  wired in, and the **in-level gameplay loop is live** — the ball state machine
  (move/jump/roll/fall/warp), collect/score/lives/win, the **tile bump/spring
  animations** (pegs/platforms recoiling), and the **moving entity (enemy AI +
  collision death)** all run in-window (see the in-level-loop sections below).
  The map's score + lives HUD is drawn (`draw_lives`/`draw_score` after
  `render_map`); the only HUD not shown is the **in-level** one, which is
  intentionally absent (the original's in-level `FUN_1000_0816` is gated on an
  event flag, so normal play shows none — see "Next step"). The world-map screen is now
  wired in: confirming "start" on the menu shows world 1's map
  (`MONDE1.VEC` + the Bumpy avatar on node 1); the arrows move between linked nodes;
  fire enters that node's board; Escape returns to the menu (see "Stage 3 world-map
  screen" below). The flow now matches the original's **menu → world map →
  playfield**; the temporary ←/→ board paging is retired. The **in-level palette is
  now correct** — the board uses its own per-board palette from the DEC header
  (dark blue), not the brown MONDE map palette. Physics, collision, win/loss and
  the map's score/lives HUD are all implemented. The sprite-frame decoder from
  Stage 2 is the reusable foundation for gameplay sprites.
- **Web port (browser), stage 1 — DONE.** The classic flat presentation builds
  for WebAssembly with Emscripten and runs from a static URL, reusing the
  `SDL_Renderer` fallback path (no GL compiled). Assets are baked into the
  build; settings persist to `localStorage`; 4:3 only. Stage 2 — a WebGL2
  context, `-sGL_ENABLE_GET_PROC_ADDRESS`, and the seven GLSL sources ported
  to GLES 3.00, which would restore the 3D diorama (`Alt+3`) — is now done
  (see the entry below); the port's GL surface is 56 distinct functions in
  total, all core GLES 3.0: 39 loaded per-context through
  `SDL_GL_GetProcAddress` (the `BUMPY_GL33_FUNCS` X-macro in `gl33.h`) plus
  17 GL 1.1 entry points called directly — 16 in `src/platform_gl3/*.cpp` and
  `glScissor` in `src/platform_sdl3/sdl_app.cpp`, which clears the diorama's
  letterbox bars (see the spec's "What stage 2 has to do"). Verified: the desktop build + full Catch2 suite
  are unaffected and the web build itself compiles and runs from a static
  URL; the in-browser playthrough is still pending (see
  `docs/web-acceptance.md`). See
  `docs/superpowers/specs/2026-08-20-web-port-classic-design.md`.
- **Web port (browser), stage 2 — DONE.** The diorama runs in the browser on a
  WebGL2 (GLES 3.0) context. The GL sources needed no porting; the GLSL version
  line moved into `compile_shader` so one shader body serves both dialects. The
  mode is now called DIORAMA in the UI on both platforms; `Alt+3` and the
  `render3d` config key are unchanged. Verified: both builds compile warning-free
  and the desktop suite is green; the in-browser look is still pending (see
  `docs/web-acceptance.md`).

## Current state

**Foundation (done, reusable):** SHA-256 asset manifest (50 files), reproducible
LZEXE unpack → `analysis/generated/BUMPY.UNPACKED.EXE`, Ghidra import (509
functions), DOSBox-X reference harness.

**Menu/resource pipeline (recovered, builds and runs on master):**
- `.VEC` decoder (`src/resources/vec`): 12-byte big-endian layer header with XOR
  checksum, multi-layer, **method 4** (marker-RLE) and **method 12**
  (marker-mask). All 13 supplied `.VEC` decode to the expected bytes (verified by
  SHA in `tests/cpp/vec_test.cpp`).
- `TITRE.VEC` → full 320×200 screen: 99-byte header (16-colour VGA palette at
  offset `0x33`) + four 8000-byte plane-sequential bit-planes, deplaned and
  rendered through `IndexedFramebuffer` (`src/video/menu_renderer`:
  `deplane_screen` / `apply_screen_palette`). Format spec:
  `analysis/specs/menu-resource-formats.md`.
- **Sprite-frame decoder (`src/resources/sprite_frame`)** — recovered by
  disassembling the overlay blitter (`1cec:31b7` → `1cec:2ded`/`2d6d`). Format:
  a BE32 pointer table over a `0x800`-based data region; each frame = 12-byte
  header (six BE16 words: mask, flags, x/y origin, width-units, height) + 4-plane
  row-interleaved VGA pixels (colour 0 = transparent). Width = `width_units*4`.
  Compressed frames (flags `0x40`/`0x20`) are NOT yet handled. Spec:
  `analysis/specs/menu-resource-formats.md`. **This decoder + format is the
  reusable foundation for all sprites** (BUMSPJEU bumpers, level sprites).
- **Menu cursor**: the arrow is `FLECHE.BIN` frame 0 (16×16, uncompressed),
  loaded in `MenuResources` and drawn by `MenuRenderer` at x 48, y 112+row·16,
  tracking `cursor_row`. (BUMSPJEU.BIN itself is the gameplay bumper sprites —
  see `analysis/generated/sprite_sheet.png`; same frame format, reuse in Stage 3.)
- Menu state machine (`src/game/menu`) and SDL3 shell (`src/platform_sdl3`).
- `OpenBumpy.exe` renders the authentic title + cursor and runs the menu window;
  31 C++ test cases pass. `--render-title [out.bmp]` dumps the menu;
  `--dump-title-raw out.bin` dumps the raw decoded title bytes.

**Stage 3 progress (level formats recovered, visually verified):**
- All level blobs are **layered-VEC containers** decoded by the existing
  `src/resources/vec` decoder. Per-level init+draw is `FUN_1000_2d14`, which
  patches the level digit into `?.PAV`/`?.DEC`/`?.BUM` (level table `203b:0090`,
  indices 0/1/8) and decodes each via `FUN_1000_7b5a` (= the VEC decoder).
  Decoded sizes are fixed per type; full spec: `analysis/specs/level-formats.md`.
- **D?.PAV** = 6-byte header + **320×192 plane-sequential 4-plane VGA image** =
  the per-world object/sprite sheet (bear, balloon, candies, presents…),
  colour 0 transparent. Confirmed by eye. Drawn by `FUN_1000_0a90` from
  `PAV_buffer + 6`.
- **MONDE?.VEC** (9 files) = 32099-byte **screen-format** backgrounds (same as
  `TITRE.VEC`): per-world art + a 4×5 grid of node rings. Render successfully.
- **D?.DEC** = static tile grid: 2-byte header + **N×812-byte boards**
  (N=15 or 12). Each board = 32-byte header + **20 cols × 13 rows × 3 bytes/cell**
  (`6bca = DEC+2+board*0x32c`, read by `FUN_1000_2a0a`). Cell[0] = PAV object
  index (0=empty, 1..0xf0=single, ≥0xf1=stacked using cell[1..2]).
- **D?.BUM** = dynamic per-board entities ("bumpers"): 2-byte header +
  **N×194-byte boards** (matching N), each = 3×48-byte entity tables + 6 params
  (`75d0 = BUM+2+board*0xc2`, copied by `FUN_1000_32b0`). `D6.BUM`/`D9.BUM` ship
  **raw/pre-decoded**. Board counts verified: (12182−2)/812 = (2912−2)/194 = 15.
- New dev/inspection flags on `OpenBumpy.exe`: `--decode-vec`, `--render-screen`,
  `--render-pav` (see spec).

**Stage 3 board module + renderer (builds and runs on master):**
- **`src/resources/level_resources`** — `LevelResources::load(root, n)` decodes
  `D{n}.PAV/DEC/BUM` via the VEC decoder with the raw-BUM fallback (D6/D9), exposes
  the board count, per-board 20×13×3 cells (column-major `0x20 + col*0x27 + row*3`),
  and the deplaned 320×192 PAV object sheet. Validated on levels 1/3/4/6/7/9
  (`tests/cpp/level_resources_test.cpp`), including the **D7 DEC/BUM count mismatch**
  (12 tile-boards vs 15 entity-boards) and level 3's empty PAV.
- **`src/video/board_renderer`** — `render_board` composes a static board: the
  base-tile pass as a flat colour-index-0 clear (the real overlay behavior, see
  below), then the DEC-placed PAV objects — single (`1..0xf0`) and stacked
  (`≥0xf1`) — on the per-world MONDE palette. `--render-board <level> <MONDE.VEC>
  <board> <out.bmp> [map]` dumps it (`analysis/generated/board_L1_B0.bmp`, verified
  by eye against the original world-1 art).
- **Blit geometry confirmed** (overlay `1ab9` planar path, recovered statically by
  Codex): width-unit = 16 px, height-unit = 8 px → playfield tiles are 16×16. The
  per-cell "base tile" (`FUN_1000_0b88`) reads no bitmap; it fills from command
  bytes `+0x22..+0x25`, which are zeroed → a flat colour-0 clear, **not** a floor
  sprite. The visible floor cross-hatch is the dense `0x63` PAV objects. Full
  recovery in `analysis/specs/level-formats.md` ("Base-Tile Blit Recovery").
- 38 C++ tests pass; originals verify clean.

**Stage 3 in-window wiring (builds and runs on master):**
- **`src/game/app`** — an SDL-independent top-level state machine (`App`) tying
  the menu to the in-level board view. Transitions: menu `--confirm "start"-->`
  level (board 0); menu `--cancel-->` quit; level `--cancel-->` menu; level
  `--left/right-->` page boards (wrap within `[0, board_count)`). The Menu keeps
  its own up/down/confirm debounce; `App` adds edge detection for the keys it owns
  (cancel, left/right) so a held key cannot bounce across a transition. 8 unit
  tests in `tests/cpp/app_test.cpp`.
- **`src/platform_sdl3/sdl_app`** now drives the `App`: it renders the menu via
  `MenuRenderer` on the menu screen and the static board via `render_board(level,
  board_index, MONDE1 backdrop)` on the level screen, and the event loop no longer
  special-cases Escape (the `App` owns cancel). `src/app/main.cpp` loads level 1 +
  `MONDE1.VEC` up front and constructs the `App` with the level's board count.
- Launching → menu; confirm "start" → level 1's board fills the window (the
  by-eye-verified world-1 art, `analysis/generated/board_L1_B0.png`); Escape →
  menu. The level screen is **display only** — no entities/physics yet.
- 46 C++ tests pass; originals verify clean.

**Stage 3 BUM entities (recovered + decoded):**
- **`D?.BUM` is three 8×6 entity layers + 6 params**, recovered from the spawn
  routine `FUN_1000_2a78` (iterates `cell = row*8 + col`, three independent
  per-layer draw paths) and the activation copy `FUN_1000_32b0`. Layer A =
  pegs/bumpers, layer B = second layer (col 7 unused), layer C = collectibles
  (sprite = `value + 0x179`). The cell→pixel coordinate table was extracted from
  the data segment (`DS:0x274`): columns 0–6 at `x = 8 + col*40`, rows at
  `y = 8 + row*32`. Full spec: `analysis/specs/level-formats.md` ("D?.BUM").
- **`src/resources/level_resources`** decodes a board into `BumEntities` (three
  8×6 layers + params + `bum_cell_position`), validated against the real `D1`
  board-0 values (peg pattern, collectible codes, params 41/44/6/0/9/0) in
  `tests/cpp/level_resources_test.cpp`.
- **`overlay_bum_entities`** (`src/video/board_renderer`) draws a marker per
  occupied cell at its faithful position for by-eye validation:
  `--render-board 1 MONDE1.VEC 0 out.bmp entities`
  (`analysis/generated/board_L1_B0_entities.png`, 27 pegs + 6 collectibles,
  landing correctly on the world-1 art). These are inspection markers, **not** the
  original entity sprites.
- 50 C++ tests pass; originals verify clean.

**Stage 3 real entity sprites (recovered + implemented):**
- The BUM entity sprites are drawn from the **uncompressed** `BUMSPJEU.BIN` bank.
  The selection chains were recovered from `FUN_1000_2a78`/`FUN_1000_165e` and the
  static descriptor tables in `BUMPY.UNPACKED.EXE`: layer A `value → 0x3d3a →
  *DS:0x3d6a[idx] {y_offset, frame}` (a near-pointer table — peg → frame `0x40`,
  the level-exit pit → frame `0xbe`), layer C `frame = value + 0x179`.
  The master frame table is addressed directly by frame index — the existing
  `decode_sprite_frame(bumspjeu, idx)` already does `be32(idx*4)+0x800`.
- **`src/resources/entity_sprites`** holds the recovered tables + resolution;
  **`draw_bum_entities`** (`src/video/board_renderer`) blits the real frames at
  their faithful positions (`--render-board <level> <MONDE.VEC> <board> out.bmp
  sprites`, and live in the SDL window). Validated on `D1` board 0 (27 pegs + 6
  collectibles, 0 skipped) by test and by eye
  (`analysis/generated/board_L1_B0_sprites.png`). 53 C++ tests pass; originals
  verify clean. Full spec: `analysis/specs/level-formats.md` ("Entity sprite bank").
- **Sprite decoder plane-layout fix (`src/resources/sprite_frame`):** the decoder
  laid out a sprite row plane-major over the full width; the real format stores
  **16px groups** (`[g0:P0 P1 P2 P3][g1:…]`). The two coincide for 16px frames, so
  faces/collectibles were always correct, but every **32px** frame (bumper pegs, the
  node marker, the Bumpy-on-cloud avatar) decoded scrambled. Fixed to the group
  layout (the `1cec:0c77` reshuffle); the board's bumper sprites and the map avatar
  now render correctly. Spec: `analysis/specs/menu-resource-formats.md`.

**Stage 3 world-map screen (builds and runs on master):**
- The original's missing **menu → world map → playfield** flow is now in the port
  for world 1. **`src/game/world_map`** is a pure, SDL-free state machine over the
  baked world-1 node graph + positions (extracted from `BUMPY.UNPACKED.EXE`: graph
  `DS:0x09e6`, positions `DS:0x0a80`, `base + N*9` records; all 15 nodes verified
  against `analysis/specs/screen-flow.md`). Pressing a direction **glides** the
  avatar 4px/tick along the connecting line to the linked neighbour (input ignored
  mid-slide), fire selects board `node−1`, Escape returns to the menu. Recovered from
  `FUN_1000_3852` / `3a88` / `3ab2…3bc9` (the `dist>>2`-steps-of-4px animation) /
  `1cb2`. **Directions move continuously**: the original's map loop re-polls the held
  keys every iteration (`FUN_1000_1dde` → `FUN_1000_75a2`, reading the live key-state
  table) with no debounce, so **holding a direction walks node to node** — the slide is
  the only pacing, and the first 4px step lands on the same tick the move begins
  (matching `FUN_1000_3ab2`). Only fire/cancel keep a release guard.
- **`src/video/map_renderer`** composes the map: the `MONDE1.VEC` backdrop (via the
  factored **`src/video/screen_image`** deplane helper, shared with `board_renderer`)
  plus the **Bumpy-on-cloud avatar** — `BUMSPJEU.BIN` **frame `0x21`**
  (`FUN_1000_1cb2`/`DAT_824a`) — centred on the current node's ring. This frame
  matches `screenshots/bumpy_001.png` pixel-for-pixel (orange face + blue cloud). It
  only looked like garbage at first because the **sprite decoder used the wrong plane
  layout for 32px frames** — see the decoder fix below.
- **`src/game/app`** gains `Screen::map` between menu and level; the temporary ←/→
  board paging is **retired** (node selection replaces it). Held-key guards stop a
  held fire/cancel bouncing across a transition. **`--render-map <world> <MONDE.VEC>
  <out.bmp>`** dumps the map headlessly. 67 C++ tests pass; originals verify clean.
  Design + plan: `docs/superpowers/specs/2026-06-21-world-map-screen-design.md`,
  `docs/superpowers/plans/2026-06-21-world-map-screen.md`.

**Stage 3 in-level palette (recovered + implemented):**
- The playfield uses its **own per-board palette**, baked into the 32-byte DEC board
  header as **16 big-endian packed-RGB words** — NOT the brown MONDE map palette an
  earlier note assumed. Recovered from the board setup in `FUN_1000_0c18`:
  `FUN_1000_0604` → `FUN_1000_063b` byteswaps the 16 header words into `DS:0x578`,
  and `FUN_1000_08d1`'s VGA branch decodes each word to a 6-bit DAC triplet
  (R = high byte, G = low byte bits 4..7, B = bits 0..3, each `<< 3`). Each board
  rebuilds its own palette on entry (`DAT_75cf` guard; word `0x1c` ≠ 0).
- Implemented in **`LevelBoard::palette()`** (`src/resources/level_resources`) and
  applied by **`render_board`** (`src/video/board_renderer`), replacing the MONDE
  palette for the gameplay path (the world-map debug overlay still uses MONDE). The
  live SDL board and `--render-board` now render world 1 **dark blue**, matching
  `screenshots/bumpy_002.png` by eye (`analysis/generated/board_L1_B0_paletted.png`).
  70 C++ tests pass; originals verify clean. Spec:
  `analysis/specs/level-formats.md` ("D?.DEC board palette").

**Stage 3 world-map cloud-jump animation (builds and runs on master):**
- Fire on a node now plays the original's **fire-to-enter cloud-jump** before the board
  loads, recovered from `FUN_1000_3cf7` + the 22-record script at **DS:0x1114** (each
  record `{frame, dx, dy}`, applied one per tick by `FUN_1000_13df`). Bumpy squashes on
  his cloud, bounces up ~8px, arcs down ~8px, then vanishes (frame `100`). dx is 0 for
  every record → purely vertical. Full recovery: `analysis/specs/screen-flow.md`
  ("Selecting a node").
- **`src/game/world_map`** owns the jump state machine (`is_jumping()`, the baked
  24-step table incl. the two pre-loop frames); confirm starts it, `select_board`
  returns only when it finishes, `enter()` resets it. Input is ignored mid-jump (like a
  slide). New tests lock the frame/offset sequence and the input-ignored invariant.
- **`src/video/map_renderer`** draws `view.avatar_frame` + the launch cloud
  (`view.cloud_visible`). Placement was the subtle part: the resting avatar (frame
  `0x21`, 32×21) is a **ball + cloud composite**, and the jump reuses its parts
  standalone — frame `0` is pixel-identical to `0x21`'s top (content offset `(8,0)`),
  frame `0xcb` to its bottom (`(0,10)`). Frames are **centred horizontally on the node**
  in the `0x21` box (ball top-aligned + bounced, cloud bottom-aligned) so the jump
  starts exactly where the resting pose sits and the cloud stays put. (The frame-header
  `origin` words are **not** the composite anchor — origin-aligning lurches the parts
  ~(8,8), which the original does not.)
- **`--render-jump <node> <MONDE.VEC> <out-prefix>`** dumps the animation as
  `<prefix>NN.bmp` for by-eye verification. 69 C++ tests pass; originals verify clean.

**Stage 3 screen-change darken (builds and runs on master):**
- The original darkens the screen **from the edges inward to the centre** on every
  screen change — recovered as **`FUN_1000_3467`** (an earlier note here mislabeled it
  "draws the frame/border"). It paints concentric black rings over the *outgoing*
  screen, outermost first, in **20×25 character cells of 16×8 px**: 10 rings, each a
  top/bottom/left/right black bar (fill colour = index 0), together covering the whole
  screen. The fill is committed by `FUN_1000_9864` (a per-mode latch-flush, **not** a
  retrace wait), so on the original it is one fast un-paced burst. Called at the start
  of the menu (`35a5`), world map (`3852`) and GAME-OVER (`11eb`) screens and before a
  board loads (`0c18`) — i.e. on menu↔map, map→level and level→menu.
- **`src/video/screen_transition`** is pure geometry (one ring per `advance()`): it
  snapshots the outgoing frame and, after step `s`, leaves the centre rectangle
  `[16s, 320−16s) × [8s, 200−8s)` visible (fully black at `s = 10`). The SDL run loop
  snapshots the last-rendered outgoing screen on a screen change, freezes game logic
  while the wipe plays (the original runs `3467` synchronously before the next screen
  loads), and **holds each ring `kDarkenFramesPerRing` retraces** (default **2** →
  ~0.29 s for the close) as the speed knob.
- **`--render-transition <MONDE.VEC> <out-prefix>`** dumps the wipe over the world-map
  art as `<prefix>NN.bmp` (step 00 = un-darkened, then one per ring) for by-eye check.
  108 C++ tests pass (incl. the wipe geometry + continuous-map-nav tests); originals
  verify clean. Spec: `analysis/specs/screen-flow.md` ("Screen-change darken").

**Stage 3 in-level gameplay loop (builds and runs on master):**
- **`src/game/level_game`** is the platform-independent in-level loop transcribed
  from `FUN_1000_0c18`'s playfield body + the `FUN_1000_1d26` player tick: a mutable
  3-plane grid + a `BallMotion`, one `tick()` == one frame = `13df` (advance the
  active move script) → `1d26` (sample tile `236f` / read input `1dde` /
  `824d==0 ? decide 1e02 : animate 238e`). The whole board-0 handler set is ported
  1:1: idle/hops/fall/roll/land/warp/hole/clear, the `4437` diamond input tree, the
  special bumpers `1fbe/207d`, and the `DS:0x43c0` per-step micro-ops, plus
  collect/score/lives/win. Dispatch + neighbour/routing tables are baked from the
  binary (`tools/re/dump_player_dispatch.py`, `move_scripts.gen`, `tile_reactions.gen`).
- **Motion is keyframe-scripted, not physics**: a state decides an action, `4263`
  arms a `{count,mirror,ptr}` record, `13df` steps a `{frame,dx,dy}` script (cell
  spacing 40×32). Loop exit flags: `928d` quit / **`9d30` board cleared (won, via the
  exit portal → `1e3d`)** / **`856d` lost a life (`22fc`: enemy death / deadly pit /
  F2)** — the last two were transposed in the original notes; `22fc` decrements a life
  and never marks the node, `1e3d` marks it cleared.
- **Exit portal**: taking the last collectible does NOT end the board — it opens a
  pit (`6c14`→`69aa(0x59)` writes tile `0x20` + the `233a` pulse). The ball must roll
  to the pit and fall in (tile `0x20` → state `0x30` descent → `1e3d`). The pit
  renders as frame `0xbe` (hole + animated down-arrow), pulsing `0xbd`↔`0xbe`.
- **Frame pacing is per-phase**: the `13df`-driven loops (in-level + the world-map
  cloud-jump) run at **35.043 Hz** (one step per two retraces), menu/map navigation
  at **70.086 Hz** (`src/platform_sdl3/sdl_app`, `half_rate`). Pinned by side-by-side
  DOSBox comparison.
- Wired into `App` + SDL: arrows move the ball, Enter/Space = fire; the ball sprite
  and live collectibles render; `App::leave_level()` returns to the map on
  win/lose. Full spec: `analysis/specs/game-loop.md`. Verified by tests + by eye
  (`--render-play 1 MONDE1.VEC 0 <dir>` dumps gameplay frames to BMP).

**Stage 3 tile bump/spring animations (recovered + implemented):**
- The pegs/platforms now **recoil** when the ball bumps or rests on them. Recovered
  system: 3 layer-A + 4 layer-B animation slots stepped by `FUN_1000_14e4`/`15a1`,
  each playing a sprite-index **byte stream** (`0xff`=end, `0x00`=hold) whose bytes
  resolve through the same near-pointer record tables the static draw uses
  (`DS:0x3d6a` layer A, `DS:0x40a6`→`0x3ad2` layer B `+0xf1`) → `{y_offset, frame}`;
  the records are non-sequential (the exit pit is why a flat `0x37be` read was wrong).
  The squash is the frame art
  + its Y anchor. Armed by `FUN_1000_69aa`/`6a89`, which also write a "settle tile"
  into the grid (for the `0x01` lane the settle is `0x01`, so no control-flow change).
- Triggers (all 1:1): rest/idle-blink (`6648`), **roll-start `6699/66d8→6d6a`** (the
  lane recoiling left/right as it deflects the ball — `6d6a` looks like a ball-sprite
  call but springs the tile), held-bump (`654e→695e`), `0x02`-lane (`6587`), hop
  entries (`6748/6789→6d94`), fall routing (`2810`, the 2nd byte of the `0x76a`
  pairs), chute/warp (`0x24/0x27`), layer-B neighbour bump (`0x35be..0x369e[8551]`),
  and the **structure trigger `6717→6d26→6d94`** (`DS:0x4396[7921]`) — the recoil of
  the structure the ball sits on, keyed by its plane-A value. This is what makes the
  **special bumpers** (`0x14`/`0x15` → events `0x2d`/`0x2e`) recoil, e.g. world-1
  **node 14**'s row of left/right-flinging springs (**fixed 2026-06-24**: `f_6d26`
  baked the table but had dropped the `f_6d94` call, so the bumpers threw the ball
  yet never sprang).
- Extracted by **`tools/re/dump_object_anim.py`** → `src/game/object_anim.{h,gen.cpp}`,
  ported into `LevelGame` (slots + `f_14e4/15a1/69aa/6a89/6d6a`) and rendered by
  **`draw_object_anims`** (`src/video/board_renderer`): suppress the static tile under
  an active slot, overlay the slot's current frame. World 1 is all plane-A lanes, so
  its springs are entirely layer A. **102 C++ tests pass**; verified by eye
  (`--render-play 1 MONDE1.VEC 0 leftfire`): the lane bends into a U and recoils, and
  rolling off a platform tilts it in the push direction — matching the original.
  Node 14's special bumpers recoil too (`--render-play 1 MONDE1.VEC 13 right`).
  Follow-up: the static **layer-B** draw still lacks the `+0xf1` bias (no world-1
  impact — no blocks there). Bonus find: the `+0xf1` is the "bank region not yet
  pinned" note from `entity_sprites.h`.

**Stage 3 game-loop close + HUD (builds and runs on master):**
- The **world map is now the persistent hub**: `src/game/app` (`App`) carries score,
  lives, and per-board completion across boards; the SDL shell constructs each
  `LevelGame` with the run's lives/score (`game.emplace(..., app.lives(), app.score())`)
  and, on a terminal status, calls **`App::finish_level(status, lives, score)`** instead
  of the bare `leave_level()`. Rules (recovered in `analysis/specs/screen-flow.md`
  "Game-loop close"): **won** (`FUN_1000_1e3d`) marks the board cleared and returns to the
  map (or, when every board is cleared = `FUN_1000_3e8a`, the world is complete → menu, a
  stub for worlds 2-9); **dead** returns to the map with the node unmarked (replayable),
  the life already decremented inside `LevelGame`; **quit** = out of lives
  (`FUN_1000_22fc` set `928d=0xff`) → game over → reset the run → menu. Starting a game
  from the menu reloads the world (`reset_run()`: score 0, lives 5, no boards cleared).
- **Completed-node markers** (`FUN_1000_3c4f`): `render_map` draws sprite **frame `0x1da`**
  on every cleared node (`App::cleared_boards()` → `render_map`'s new `cleared_boards`
  span), beneath the avatar. The overlay blitter centres a frame on its descriptor by half
  its dimensions (verified: the resting avatar frame `0x21` is `32x21`, so descriptor − (16,
  10) = the bbox-centred placement); the marker sits at descriptor `(node.x − 1, node.y)`.
- **Lives HUD** (`FUN_1000_6130`): new `src/video/hud` `draw_lives` blits the life icon
  (frame `0x1aa`) once per remaining life at descriptor `(i*8 + 0x50, 0)` along the top,
  drawn on the world map. Verified by eye against `screenshots/bumpy_001.png` (the red
  Bumpy-head row, top-centre).
- **Score HUD** (`FUN_1000_0816` → the `1ab9` text overlay): the 7 zero-padded digits are
  drawn from the **DDFNT2.CAR** bitmap font (NOT a BUMSPJEU/EXE font — `FUN_1000_808e` is
  `malloc`, `0x7c3` is the buffer size; the font is LEVEL-table index 4). New
  `src/resources/font` decodes the variable-width MSB-first glyph format (header
  `[0]first [1]last [2]ascent [3]metric [4]spacing`, BE16 offset table, per-glyph
  `[0]w [1]h [2]yoff [3..]bitmap`); `src/video/hud` `draw_score` renders it at the
  map cursor `(1, 8)` (raw pixels, `Y` = baseline) in palette index **14** — matching
  `screenshots/bumpy_001.png` exactly (font glyphs + the olive-gray colour). The
  **in-level HUD** is deliberately not drawn: the original's in-level `0816` call is gated
  on an event flag, so normal play shows no persistent in-level score (matches `bumpy_002`).
  Full recovery: `analysis/specs/screen-flow.md` ("HUD score font"),
  `tools/re/dump_hud_font.py`. **118 C++ tests pass** (`app_test` finish-level cases,
  `map_renderer_test` markers/centring, `hud_test`, `font_test`); originals verify clean.

**Stage 3 moving entity — enemy AI + collision (builds and runs on master):**
- The per-board **monster** is ported. Of all 15 world-1 boards only **board index 2
  (node 3)** carries one (header byte `0x93 != 0`); it spawns at cell 39 with sprite
  base `0x1f7`. Recovered from `FUN_1000_2a78`/`48a9` (spawn + pixel anchor from the
  `DS:0x274` table `+7,+7`), the movement-script table `DS:0x2520` (`4bc6`/`4c14` — a
  keyframe `{frame,dx,dy}` script per direction, cell spacing 40×32, stepped every
  other frame via the `8243` toggle), and the maze AI `4c99` (4-direction free-flags
  over the live plane-A/B grid → the `DS:0x870` on-arrival dispatch `4dbf`/`4e44`/
  `4ec9`/`4f4e` + `DS:0x85c` mid-step cell move `5025`/`503f`/`5059`/`506f`; all-blocked
  → `4fd3` random bob). The AI keeps its heading when free and turns by a fixed
  preference; the leaf routines add a random detour **only when `79b3 < 7920`**, so a
  sub-type-0 board (board 2) is fully deterministic.
- **Collision** is the AABB pair `FUN_1000_5085` (ball box) × `50c0` (entity box) →
  `50fb` overlap → `a1aa=1`, consumed by `1d26` the next frame → `f_228d`, which arms
  the **already-ported state-`0x2e` fly-around death cascade** (shared with the spike
  death: `22d2`×3 → `22fc`, −1 life, node left unmarked = replayable).
- Ported to **`src/game/level_game`** (entity state + `f_48a9`/`4bc6`/`4c14`/`4c99`/
  `5003`/`4fd3`/`5085`/`50c0`/`50fb` + the AI/leaf/cell methods), tables baked by
  **`tools/re/dump_entity_ai.py`** (inline `kEntityScripts`/`kEntityKeyframes`/
  `kEntityAnimBase`), and rendered by **`draw_monster`** (`src/video/board_renderer`,
  wired into the SDL run loop and `--render-play`). **123 C++ tests pass** (5 new:
  spawn/no-spawn, maze-walk-in-bounds, half-rate step, death-on-contact −1 life,
  cross-row safety); originals verify clean. Verified by eye on D1 board 2 — the
  orange creature spawns, animates (`0x1f7..0x1fa`), walks its row, and kills on
  contact. Full spec: `analysis/specs/game-loop.md` ("Moving entity").

**Placeholders / remaining work:**
- Compressed sprite frames (flags `0x40`/`0x20`, `1cec:2ded`) are **fully recovered
  from disassembly but unused by the supplied assets** — `BUMSPJEU` is all
  uncompressed and the compressed `BUMPYSPR.BIN`/`SPRITE.BIN` are not shipped. The
  decoder is documented (`analysis/specs/menu-resource-formats.md`) but not
  transcribed into the port (nothing to decode/validate).
- The **HIGH-SCORE** and **PASSWORD** menu sub-screens are both **implemented** (see
  "Stage 3 high-score screen" and "Stage 3 password screen"). The between-worlds
  password display (`FUN_1000_0d9d`) is implemented too.
- The in-window level screen is **live** (ball state machine + collect/score/win +
  the tile bump/spring animations + the **moving entity / enemy AI + death** — see the
  in-level-loop and "moving entity" sections above). Still missing in-level: the
  in-level score/lives HUD. The world map navigates (with
  the gliding Bumpy-on-cloud avatar), **plays the fire-to-enter cloud-jump animation**,
  and selects boards, but its score/lives HUD overlays and the per-completed-node
  markers (frame `0x1da`) are not drawn yet.

## How to run

```powershell
cmake --build --preset windows-debug
& build/windows-debug/Debug/OpenBumpy.exe                       # menu window
& build/windows-debug/Debug/OpenBumpy.exe --render-title out.bmp [0|1|2]  # headless menu dump (optional LEVEL difficulty)
& build/windows-debug/Debug/OpenBumpy.exe --decode-vec D1.PAV out.bin          # decode any VEC/PAV/DEC/BUM
& build/windows-debug/Debug/OpenBumpy.exe --render-screen MONDE1.VEC out.bmp   # 320x200 screen-format VEC
& build/windows-debug/Debug/OpenBumpy.exe --render-pav D1.PAV MONDE1.VEC out.bmp planeseq 320 192 6
& build/windows-debug/Debug/OpenBumpy.exe --render-board 1 MONDE1.VEC 0 board.bmp        # static board (add 'map' to overlay the world-select screen)
& build/windows-debug/Debug/OpenBumpy.exe --render-map 1 MONDE1.VEC map.bmp              # world-map screen (MONDE1 + Bumpy avatar on node 1)
& build/windows-debug/Debug/OpenBumpy.exe --render-play 1 MONDE1.VEC 0 leftfire play     # drive board 0 with an input (none/up/down/left/right/fire/leftfire), dump playNN.bmp + ball/spring log
& build/windows-debug/Debug/OpenBumpy.exe --render-jump 6 MONDE1.VEC jump_              # fire-to-enter cloud-jump animation on a node, dump jump_NN.bmp
& build/windows-debug/Debug/OpenBumpy.exe --render-transition MONDE1.VEC trans_         # edge-to-centre screen-change darken, dump trans_NN.bmp (00 = un-darkened)
& build/windows-debug/Debug/OpenBumpy.exe --render-outro DESSFIN.VEC outro.bmp          # post-world-9 ending screen (DESSFIN.VEC) via the shell render path
& build/windows-debug/Debug/OpenBumpy.exe --render-password SCORE.VEC pw.bmp [CODE]     # PASSWORD entry screen; [CODE] shows its OK/ERROR flash
& build/windows-debug/Debug/OpenBumpy.exe --dump-music BUMPY.MID BUMPY.BNK music.wav 20 # offline-render N s of the intro music (OPL2+BNK) to a mono 49715 Hz WAV
& build/windows-debug/Debug/OpenBumpy.exe --dump-sfx 1 sfx.wav                          # render one SFX preset (id 1..0x15) via the speaker sweep engine to a WAV
& build/windows-debug/Debug/OpenBumpy.exe --render3d                                    # launch straight into 3D diorama mode
& build/windows-debug/Debug/OpenBumpy.exe --render-3d 1 MONDE1.VEC 0 out.bmp            # headless diorama dump for one board
& build/windows-debug/Debug/OpenBumpy.exe --present-parity                              # flat-path GL-vs-CPU parity gate (menu/board/map x2/x4, exit 0 = all PASS)
```

In the window: the startup splash now **plays the intro FM music** (looped until a key
is pressed), and gameplay/menu emit the **PC-speaker SFX** (collect, bumps, deaths,
cloud-jump, …). A missing/locked audio device degrades to **muted**, never fatal.

In the window: confirm "start" on the menu → world map; arrows move Bumpy between
linked nodes (**hold a direction to walk node to node continuously**); Enter/Space
enters that node's board. **Escape matches the original** (`FUN_1000_0c18`): in a level
it **loses a life** and returns to the world map (node replayable), or triggers GAME
OVER on the last life; on the world map it is a **GAME OVER → menu** (drops the run, no
high-score table); on the menu it quits. Every screen change plays the edge-to-centre
darken. **Alt+Enter** toggles fullscreen, **Alt+A** toggles the display aspect of the flat presentation
(16:10 square pixels / 4:3; the 3D diorama is always 4:3-corrected),
and **Alt+3** toggles the 3D diorama presentation
in-level (see "3D render mode" above) — all three persist to `bumpy_port.cfg` next
to the exe. Debug builds add **Alt+R** to hot-reload the diorama shaders in place.

## Reverse-engineering workflow

1. Regenerate the decompilation if needed (`tools/re/decompile_loader.ps1` →
   `analysis/generated/decomp/all_functions.c`).
2. Read the relevant functions; correlate with strings, the resource table, and
   the raw bytes of the original files.
3. Record recovered behavior in `analysis/` (catalog + specs) with addresses.
4. Transcribe confirmed behavior into C++ behind a platform-independent boundary.
5. Test decoders/logic on the real files; compare visuals to the original by eye.

## Key recovered facts

- Built with **Turbo C++ 1990 (Borland)**, 16-bit real mode, large model. DOS
  calls appear as `swi(0x21)` in the decompilation.
- **Frame timing is vertical-retrace paced** (one logic tick per displayed frame,
  `7bdd` → `FUN_1ab9_0351` → the `0x3DA`-bit-3 retrace poll at file `0x11405`). For
  VGA 320×200 that is **70.086 Hz**. The PIT's ~19.2 kHz reprogramming is the
  PC-speaker sample timer, **not** the frame clock. The port paces the loop at
  70.086 Hz (`src/platform_sdl3/sdl_app`); see `screen-flow.md` ("Frame timing").
- Data segment is the load-relative `0x103b` (Ghidra shows `0x203b`, `+0x1000`).
- Resource/loader pipeline: `analysis/RESOURCE_PIPELINE.md`.
- `.VEC` and sprite formats: `analysis/specs/menu-resource-formats.md`.
- Level formats (PAV/DEC/BUM, MONDE): `analysis/specs/level-formats.md`.
- Screen flow + world map + palette: `analysis/specs/screen-flow.md`.

## Safety rules

- Never modify the original game files; treat root-level game files as read-only
  inputs (`python tools/assets/manifest.py verify`).
- Generated artifacts and downloaded tools live under ignored
  `analysis/generated/` and `tools/vendor/`. Do not commit them.

## Stage 3 — sprite-positioning + bounce polish (2026-06-27)

Correctness fixes on already-live features, verified against the original (a DOSBox ZMBV
capture was decoded frame-by-frame to nail the bounce). Commits `88900b1`/`957ef4d`/`c75fa12`:

- **World-map completed-node cross** (`0x1da`): anchored by the header origin/hotspot,
  centred on the node ring (was 1px left). `decode_sprite_frame` now exposes `origin_x/y`.
- **Exit-pit descent**: the sinking ball clips into the pit at the right line — ball **Y is
  anchored by `min(origin_y, height/2)`** — and the SDL loop renders the resolved terminal
  frame before the screen-change darken, so the wipe freezes the ball already in the pit.
- **Head-bump**: the bounce-apex / jump frames no longer fling the ball sideways or punch
  it up through the platform/top edge.
- **Held-UP bounce**: wired the missing `0x647e` anim-step handler (a bump sound + the
  `FUN_654e` held-bump latch) for bounce states `0x06/0x07/0x2b`. Without it a held UP
  re-armed only every other cycle, so the floor lane recoiled on alternate landings; now
  the ball bounces continuously and recoils **both** bars every cycle.
- **Ball centring**: ball/monster **X is anchored on the visible content centre**, so the
  resting ball sits dead-centre on its lane tile (was 1px left) with no horizontal jump as
  the animation cycles between the 16px and 32px frames. See `video/board_renderer.cpp`.

## Stage 3 worlds 2–9 (linear advance + per-world reload) (2026-06-27)

All 9 worlds are now playable in sequence. The work spanned five tasks:

- **Task 1 — `world_graphs`**: Per-world node graph + positions baked from
  `BUMPY.UNPACKED.EXE` (graph far-ptr table `DS:0x10c8`, positions `DS:0x10ec`,
  world W pointer at `table + W*4`). `world_node(world, node)` and
  `world_node_count(world)` serve 9 worlds; `kWorldCount = 9`.
- **Task 2 — `WorldMap` parameterized**: `WorldMap(int world)` switches to any
  world's node graph via `load_world(world)`. The map state machine (slide/jump)
  is unchanged; `WorldMapView::world` drives the renderer's marker positions.
- **Task 3 — per-world map markers**: `render_map` uses `world_node(view.world, n)`
  to place the completed-node marker (`0x1da`) at each world's actual pixel positions.
- **Task 4 — `App` linear advance + handshake**: `App(board_count, start_world)`
  tracks the current world; on a world-complete `enter_world` callback it advances
  `world_+1` (world 10 / after world 9 → menu stub). `pending_world()` /
  `enter_world(world, board_count)` are the shell handshake.
- **Task 5 — `WorldResources` + shell reload + main wiring**:
  `WorldResources::load(root, world)` bundles `D{n}.PAV/DEC/BUM` +
  `MONDE{n}.VEC` per world. The SDL shell owns one `WorldResources` (by value)
  and reloads it at the top of the run loop whenever `app.pending_world() != 0`
  (load → `enter_world(n, board_count)`; on failure, cancel via
  `enter_world(current, current_count)`). World-independent assets (`BUMSPJEU.BIN`,
  `DDFNT2.CAR`) are loaded once and outlive `run()`. `--start-world N` lets dev
  sessions start directly on any world (1..9). The `--render-map` dumper is now
  fully world-aware (uses `world_node_count(world)` and `WorldMap(world_number)`).
  Cross-check `board_count() == world_node_count(w)` PASSES for all 9 worlds
  (132 tests, 72725 assertions).

**World 10 (`FUN_1000_3ed4` outro) is now implemented** — see "Stage 3 world-9
outro" below.

## Stage 3 worlds 2–9 element parity (2026-07-03)

A systematic audit (decode all 9 `D?.BUM`, enumerate plane-A/B/C codes + header
fields per world, model the reaction tables, diff against the port's dispatch
mapping) found four systems that are **unreachable in world 1** and were never
ported. All recovered from the disassembly (`FUN_1000_6183`/`629c`/`31de` from
raw bytes — Ghidra fails on them) and ported to `LevelGame`; spec:
`analysis/specs/game-loop.md` ("Worlds 2+ elements"). 136 tests pass; a smoke
run of every board × 9 worlds × 3 input patterns (`--render-play`) is clean.

- **Board-entry drop (`31de`)**: the ball materializes 12px above its start
  cell and settles via the raw 10-step DS:0x1394 script. Applies to world 1 too.
- **Nests (tile `0x16`)**: park/spin (`4305`/`4361`, frame cycle DS:0x1b70 via
  `495c`), fire+direction hops out, and the **dig** — in the vertical-hop states
  `0x1d..0x20`, no-direction calls `6d94(0x2f)` which writes a fresh nest tile
  under the ball (the climb mechanic). The port's `440c` had treated this as a
  settle-sfx no-op.
- **Block-top riding (states `0x21..0x2a`, `0x32/0x33`)**: hop onto plane-B
  `0x08` slab → walk along block tops (`2138`/`21e7` + tables DS:0x42d6/0x42f6,
  `21bb`/`2261` plane-A `0x0b` checks); onto `0x0d` cushion → sit bobbing
  (`1ec2`/`1f3e`, frames DS:0x1ca4/0x1cba), DOWN rolls off (`1f03`/`1f7f`, raw
  scripts DS:0x140c/0x1460, springing the seat block, event `0x16`); fire|down
  smashes down through (states `0x32/0x33`).
- **Picture-block match puzzle (plane-B `0x0e..0x11`)**: bumps cycle the art;
  when all remaining pictures match, `6183` lists every plane-B `0x05` block
  (buffer DS:0x886) and `629c` (new tick step before `f_1d26`) pops them open
  one per 11 frames (event `0x18`).
- Confirmed no-ops: anim steps `6305`/`64c1`/`645d` are sound-only, `673a` is
  empty (`6e11`/`6e30` is the sfx synth; `DAT_689c` is the sound-device type).
  Static sprite coverage for all worlds' plane-A/B/C codes checked — complete
  (plane-B `0x15` has no sprite in the original either).

Two follow-up fixes from user testing on world 2 (the cloud board 11):

- **Cloud duplication**: `45a0` (the cloud-chain move commit) arms `6d94(0x30)`
  — event `0x30` ERASES the departure tile (dissolve `b9 b9 ff`) — and
  `450c`/`457a` dig (`0x2f`) when leaving a non-nest tile. The port had kept the
  `856f=` side-effects but dropped all three `6d94` calls, so every chain move
  left a stale cloud behind. Erase-at-departure + dig-at-arrival = the ridden
  cloud *moves*. Exhaustive sweep: no other `6d94`/`69aa`/`6a89`/`6987` call
  site is missing.
- **Sprite header origin is stored Y-FIRST** (`word[2]=origin_Y`,
  `word[3]=origin_X`): the decoder read the pair swapped, and the earlier
  content-centre-X / `min(origin_y, h/2)`-Y ball anchors were per-case fits
  around that swap (all symmetric frames coincide). Pinned by the wide frames
  (every 32px frame has word[3]=15 = centre; word[2] tracks content height) and
  by the flying cloud riding 3px high vs its parked tile (frame `0x21`, 32×21,
  (7,15)). `draw_ball`/`draw_monster` now anchor faithfully at
  `pos − (origin_X, origin_Y)`; `content_centred_x` is deleted; the
  "sprite-positioning polish" notes below describe the superseded heuristics.
  Verified: parked and flying cloud tops both at y=187 on world-2 board 11
  (`--render-play` pixel check); the map-cross/marker path is unaffected
  (symmetric (15,15)).

## Stage 3 world-9 outro / ending screen (2026-07-05)

Clearing world 9 now plays the original's **ending screen** instead of dropping
straight to the menu. Recovered from **`FUN_1000_3ed4`** (the `DAT_79b2 == 10` branch of
the main loop): it loads **`DESSFIN.VEC`** (resource index `0x11`, a screen-format VEC —
the "Bumpy sits" credits page: AUTEUR JF STREIFE / PROGRAMMATION M SPADA / MUSIQUE
M WINOGRADOFF / GRAPHISME C PERROTIN, I MAURY, P JARRY), plays the edge-to-centre darken
over the won board, blits the full-screen image, **waits for a keypress** (`FUN_1000_328f`
= clear latch + spin until any key), then `DAT_79b2 = 1` + `DAT_928d = 1` returns to the
menu. The image renders from its **own embedded VGA palette** (offset 51); the `DAT_541d`
`DS:0x72e` 16-byte patch is a non-VGA remap the port ignores. Full recovery:
`analysis/specs/screen-flow.md` ("Outro — FUN_1000_3ed4").

- **`src/game/app`** gains `Screen::outro`. `finish_level` routes the last cleared board
  of world `kWorldCount` (9) there instead of the menu; the outro branch in `update()`
  mirrors `328f` (release-then-fresh-press), then `reset_run()` + menu.
- **`src/platform_sdl3/sdl_app`** draws the outro via the shared `screen_image` helpers
  (`apply_screen_image_palette` + `draw_screen_image`); the level→outro screen change plays
  the existing edge-to-centre darken (the `3467` call inside `3ed4`) for free. `main.cpp`
  decodes `DESSFIN.VEC` once (world-independent) and passes it into `run()`.
- **`--render-outro DESSFIN.VEC out.bmp`** dumps the ending screen through the exact shell
  render path. **138 C++ tests pass** (3 outro cases: entry, release-then-dismiss, the
  Escape-no-bounce guard); originals verify clean; verified by eye.

## Stage 3 high-score screen (2026-07-05)

The original's **HIGH-SCORE screen** is now implemented, both entry points, fully
faithful. Recovered from **`FUN_1000_5681` → `FUN_1000_57e1`** (table draw + insert
test) + **`FUN_1000_59d3`** (name entry) + **`FUN_1000_11eb`** (GAME OVER). NOTE:
earlier status notes mislabeled `0d9d/0f7a/11eb` as "HIGH-SCORE" — those are the
PASSWORD-display / PASSWORD-entry / GAME-OVER screens; the high-score table is the
`5681/57e1/59d3` set. Design + plan:
`docs/superpowers/specs/2026-07-05-high-score-screen-design.md`,
`docs/superpowers/plans/2026-07-05-high-score-screen.md`.

- **Background = `SCORE.VEC`** (MENU resource index 3). It ships as a **raw** 320×200
  screen image (99-byte header + 4×8000 planes = 32099 bytes), **not** a compressed VEC
  container — so it loads via a new shared `read_binary_file` (`resources/binary_reader`)
  and renders through the existing `screen_image` helpers. The "HALL OF FAME" title +
  dotted entry-guide frame are baked into the image.
- **Table = 7 baked entries, no persistence** (`src/game/high_scores`): `BIG JIM. 5000000`
  … `MIKE.... 500`, held in memory for the session and reset each launch (the original's
  data-segment table at `DS:0x8f0` — there is no disk I/O anywhere in the game).
  `qualifies` (strict `>`) / `insert` (shift down, seed `AAAAAAAA`, drop the last).
- **Text = BUMSPJEU sprite glyphs** (`src/video/high_score_renderer`): `'0'-'9'` = frames
  `0x1ac..0x1b5`, `'A'-'Z'` = `0x1b6..0x1cf`, `'['` caret = `0x1d0`, `'.'`/space = blank;
  glyphs are top-left anchored (origin 0,0). Names at `(col*16, row*16+65)`, 7-digit
  zero-padded scores at `(176+i*16, …)`, `GAME OVER` at `(96, 96)`.
- **Two entry points** (`src/game/high_score_screen` state machine + `App`): **menu row 1**
  → view-only, any key → menu; **out of lives** (`LevelStatus::quit`) → `Screen::game_over`
  ("GAME OVER" on **black**, a timed ~0.5 s flash, no keypress — `11eb` uses a fixed delay;
  it loads SCORE.VEC's palette but never deplanes the image, unlike `5681`)
  → `Screen::high_scores` (name editor if the score qualifies: held-repeat up/down cycle the
  glyph, left/right move the caret over all 8 columns, fire commits — `59d3`) → reset run →
  menu. The faithful **two-darken** flow (level→game_over→table) falls out of the existing
  darken-on-screen-change mechanism for free. Victory still goes outro → menu (the original
  does **not** show high-scores after winning).
- **`--render-highscores SCORE.VEC out.bmp [insert_score]`** and **`--render-gameover
  SCORE.VEC out.bmp`** dump the screens headlessly. **154 C++ tests pass** (high_scores,
  high_score_screen, high_score_renderer, menu row-1, App game-over/high-score cases);
  originals verify clean; all three screens verified by eye
  (`analysis/generated/highscores*.png`, `gameover.png`). Two pacing constants
  (`kGameOverFrames`, the editor repeat/blink cadence) are tuned by eye.

## Stage 3 board-start pause (2026-07-05)

The original's **"ready?" start pause** is now ported: on entering a board the ball
hangs at its entry position (12px above its start cell) and the whole playfield is
frozen — no ball/monster/spring/PRNG advance — until the player presses any
key/button, then the entry-drop plays in and gameplay begins. Recovered from
**`FUN_1000_328f`** (`DAT_8244 = 0; while (8244 == 0) FUN_1000_1dde();` — clear the
input latch, spin reading input until any bit is set; no release edge, so a held key
starts immediately). It is called at line 1198 of `FUN_1000_0c18`'s per-board setup,
between the first-frame draw and the frame loop — a **peer of the screen-darken
(`3467`)**, both of which the port already handles in the SDL shell.

- Implemented in **`src/platform_sdl3/sdl_app`**: a `level_awaiting_start` flag set
  when the board is created (`game.emplace`, = `0bf9`/`5181`) and cleared by the first
  level-input bit; while set, `game->tick()` is skipped and `render_level()` draws the
  hanging ball. The first input clears the flag and ticks that same frame (mirroring
  `328f`'s instant return → the loop's first iteration). `LevelGame`, its 154 tests, and
  the `--render-play` dumper are untouched (the dumper's frame 00 is exactly the frozen
  hang frame). Verified: 154 tests pass; the pre-tick frame renders the ball hanging
  above its cell on the composed world-1 board.
- Spec fix: `analysis/specs/game-loop.md` had mislabeled `328f` as "wait for the
  screen-reveal curtain"; corrected to the wait-for-keypress start pause (same
  spin-until-key as the outro `328f`, which `screen-flow.md` already labels correctly).

## Stage 3 Escape / exit flow — match the original (2026-07-05)

Fixed the port's Escape handling to match `FUN_1000_0c18` exactly. The port previously
made Escape a jump straight to the menu on **every** screen, which threw away the whole
run. The original is a two-step exit, and disassembling `FUN_1000_1d26`/`FUN_1000_3852`
(the decompiler had dropped the `7ab4` scancode args, so the F-key attribution in the
old specs was wrong) pins it down:

- **In-level** (`1d26` polls scancode `0x01` = Escape → `FUN_1000_22fc`): Escape **loses
  a life** (like a spike/enemy death but with no fly-around — `22fc` just spins `236f`
  1000×, sets `856d=1`, `791a--`) and the board ends → back to the **world map** with the
  node unmarked (replayable), the run's score/cleared-nodes intact. On the **last life**
  (`791a==0`) `22fc` sets `928d=0xff` → GAME OVER. (The old specs mislabeled this "F2
  skip"; F2 is actually a debug-palette key. F10 `0x44`, not F7, is the hard quit.)
- **World map** (`3852` polls scancode `0x01` → `928d=0xff`): a **GAME OVER** — `0c18`
  runs `FUN_1000_11eb` (the timed flash) then `goto LAB_0c2c` (menu), **without** the
  high-score table `FUN_1000_5681`. So map-Escape drops the run to the menu. This differs
  from running **out of lives in play**, which runs `11eb` **and** `5681` (high scores).

Ported: `LevelInput` gains `cancel` (Escape), handled in `LevelGame::f_1d26` → `f_22fc`
(fires once per board — the shell tears the board down on the terminal status). `App`
no longer treats in-level cancel as a menu jump; `finish_level(dead)` → map,
`finish_level(quit)` → `game_over` → `high_scores` → menu (unchanged, correct). World-map
cancel now routes `map → game_over → menu` with a new `game_over_to_menu_` flag that
skips the high-score table for that path. `sdl_app` feeds `input.cancel` into the level.
**156 C++ tests pass** (2 new `level_game` Escape cases; `app_test` map-cancel/level-
cancel/held-cancel cases updated); originals verify clean. Specs corrected:
`game-loop.md` (flags table + `1d26` key poll + lose-a-life triggers), `screen-flow.md`
(map-Escape + the two-GAME-OVER asymmetry).

## Stage 3 difficulty selection — the `LEVEL` menu item (2026-07-05)

The menu's **`LEVEL`** item is now a working **difficulty / game-speed selector**, both
the on-screen indicator and the actual gameplay effect, matching the original. Recovered
from `FUN_1000_35a5` (menu loop), `FUN_1000_51d8`/`FUN_1000_2ef8` (label prep),
`FUN_1000_1349`/`FUN_1000_05e7` (the in-level speed effect), and the data tables
`DS:0x11b2` / `DS:0x75e`. Full recovery: `analysis/specs/menu-behavior.md`
("Difficulty selection").

- **Selection.** Confirm on row 2 cycles `DAT_79b5` `0→1→2→0` (EASY/MEDIUM/HARD), stays in
  the menu; default EASY; resets to EASY on menu re-entry (`LAB_0c2c`). The port's
  `Menu::cycle_value_` already cycled; now `MenuView::level_value` mirrors it and
  `Menu::reset_selection()` clears it on a fresh run (`App::reset_run`).
- **Indicator.** The `EASY`/`MEDIUM`/`HARD` label is baked into **`MASKBUMP.VEC`** (menu
  resource 1) at char `(0, 13/17/21)` = pixel `(0, 104/136/168)`, 96×16; `MenuRenderer`
  now opaque-blits the selected one to `(176, 144)` — the LEVEL row. It renders through
  TITRE's palette (menu green), background seamless. Verified by eye at all three settings
  (`analysis/generated/menu_easy/medium/hard.png`); the EASY blit aligns pixel-for-pixel
  with the baked "LEVEL: EASY".
- **Speed effect.** On run start `App` latches `difficulty_` from the menu; `level_pattern()`
  maps it through the `DS:0x11b2` table **`{0xff, 0xaa, 0x00}`** to the `DAT_854f` mask. The
  new pure `src/game/speed_pacer.h` (`SpeedPacer`) transcribes `FUN_1000_1349` 1:1: each
  in-level frame it waits `(mask low bit ? 2 : 1)` retraces and rotates the mask. The SDL
  shell resets the pacer per board (`level_pacer.reset(app.level_pattern())`) and paces each
  in-level tick by `pacer.step() * period_full`: **EASY = 2 retraces (35.043 Hz, unchanged
  from the historical pace), MEDIUM = alternating (~46.7 Hz), HARD = 1 (70.086 Hz, 2×
  faster)**. The "second retrace the disassembly never showed" **is** `1349`→`9864` — so
  EASY is byte-identical to the prior verified 35 Hz (no regression).
- **Scope.** `854f` only paces the in-level loop (the map forces it to 0, the cloud-jump to
  `0xaa`); the port already runs those at full/half rate, unchanged. In-level F1–F5 stay a
  debug override (noted in `level_game.cpp`).
- New: `--render-title out.bmp [0|1|2]` previews the indicator per difficulty. **164 C++
  tests pass** (`speed_pacer_test` ×5, `menu_test` indicator/reset, `app_test` latch/reset);
  originals verify clean.

## Stage 3 password screen — the `PASSWORD` menu item (2026-07-05)

The last menu sub-screen is done: **`PASSWORD`** (row 3) opens the code-entry screen; a
valid code jumps the run to that world. Recovered from `FUN_1000_0f7a` (entry screen),
`FUN_1000_5c87` (6-char editor + validation), `FUN_1000_2d14` (world start), and the tables
`DS:0x135c` / message pointers `DS:0x11a2..0x11aa` — confirmed against the **raw
disassembly** (Ghidra dropped the `736f`/`a9f5` args; recovered with capstone). Full
recovery: `analysis/specs/menu-behavior.md` ("Password screen").

- **8 codes → worlds 2–9**: `ACCESS BUTTON ISLAND PRETTY WINNER ZOMBIE LOVELY SYSTEM`
  (`DS:0x135c`, all clean ASCII, 6 letters). `src/game/password_screen` bakes them +
  `password_world(code)` → world (2–9) or 0.
- **Background = BLACK** (not the HALL OF FAME art). `0f7a` loads SCORE.VEC (menu resource 3)
  only for its palette; it never deplanes/blits the image (`FUN_1000_7b5a` + `FUN_1000_80bc`
  are absent, unlike the real high-score screen `FUN_1000_5681`), and the `3467` darken leaves
  the page black — identical to GAME OVER (`11eb`). An earlier note wrongly claimed the framed
  backdrop shows; corrected after verifying against the disassembly (the same fix GAME OVER got).
- **`PasswordScreen`** (mirror of `HighScoreScreen`): seed `AAAAAA`; the glyph cycle is the
  contiguous frame run `0x1ac..0x1d0` = `'0'-'9','A'-'Z','.'`, **clamped** — UP steps toward
  `'0'` (9,8,…,0, floors at `'0'`), DOWN toward `'.'` (B,…,Z,., ceils at `'.'`) (verified by
  dumping the bank; earlier the direction was inverted and the run mis-read). ←/→ move the caret
  over 6 columns, fire commits, then a brief `PASSWORD OK` / `PASSWORD ERROR` flash.
  `src/video/password_renderer` draws it on black (SCORE.VEC palette only + prompt y16 + entry
  field y160 + result y96); the cursor cell blinks a **solid block** (not the `0x1d0` '.' glyph
  the earlier port drew as a caret). Glyph drawing is the shared `draw_glyph_string` /
  `draw_editor_glyphs` (the latter shows a '.' cell; display rows keep '.' blank per 57e1).
- **Integration.** Menu row 3 was mis-mapped to `quit` (a port placeholder — the original
  menu has no quit row; Escape quits); it now emits `MenuAction::password`. `App` gains
  `Screen::password` and `selected_world_` (DAT_79b2): a valid code sets it, an invalid one
  resets it to world 1, and `reset_run` starts the next PLAY at that world (via the existing
  `pending_world` reload) then consumes it back to the default. `sdl_app` renders the screen
  and `--render-password SCORE.VEC out.bmp [CODE]` dumps it headlessly.
- `password_screen_test`, menu/app row-3, and valid-code-start cases are covered; originals
  verify clean; all three states were verified by eye
  (`analysis/generated/pw_entry/pw_ok/pw_err.png`).

## Stage 3 between-world password display (2026-07-06)

The post-world screen is implemented: after `FUN_1000_3e8a` clears a non-final
world, the port now shows `FUN_1000_0d9d` before loading the next world.

- **Flow.** Clearing all boards in worlds 1-8 moves `App` to `Screen::password_display`
  with the next world number (`world + 1`). The shell does not load the next world's
  resources until the player presses fire; only then does `pending_world` request the
  next world and return to the map.
- **Content.** The renderer matches the recovered `0d9d` layout: black page using
  `SCORE.VEC` only as the palette source, `YOUR PASSWORD` at y=80, columns 4-16, and
  the next world's 6-letter code at y=112, columns 7-12. Codes reuse the confirmed
  password table: `ACCESS BUTTON ISLAND PRETTY WINNER ZOMBIE LOVELY SYSTEM`.
- **Coverage.** `app_test` covers the deferred world-load handshake and
  `password_renderer_test` covers the black background plus prompt/code bands.

## Stage 3 audio / sound system (2026-07-07)

The port had **no sound** at all; the whole audio subsystem is now built, targeting the
original's **AdLib profile** (the definitive 1993 experience = OPL2 FM music + PC-speaker
beeper SFX). Design + plan: `docs/superpowers/specs/2026-07-06-audio-sound-system-design.md`,
`docs/superpowers/plans/2026-07-06-audio-sound-system.md`. Built on branch
`feat/audio-opl2`; game logic stays SDL-free (emits sound-event ids; the shell drains them).

- **Sound-device model recovered** (capstone; Ghidra fails on the timer ISR + sweep
  handlers). The DOS setup writes `DAT_203b_689c`: `0x8000`=off, `0`=PC speaker, `1`=AdLib,
  `4`=**MT-32/MPU-401**. **Correction to earlier notes:** `689c==4` is MT-32, *not* AdLib
  (AdLib=1); the `0x27ae` table is MT-32 percussion notes, not AdLib registers. The SFX
  sweep engine writes **only PIT ch2 + the 0x61 gate — never OPL** → **SFX are always
  PC-speaker** (except under MT-32). (`game-loop.md`'s old "689c==4 = AdLib" note is
  superseded by the audio design spec.)
- **Music = `BUMPY.MID` on OPL2** (intro/credits screen only, `FUN_1000_30dd` =
  `Screen::splash`, looped until keypress). `BUMPY.MID` is a real SMF (format 1, 7 tracks,
  192 tpqn, 75 BPM); `BUMPY.BNK` is the AdLib `.BNK` instrument bank ("ADLIB-", 160
  patches `rol000…`). Decoders: **`src/resources/midi_song`** (SMF parse → tick-sorted
  events + tempo map) and **`src/resources/adlib_bank`** (header + 12-byte name index +
  30-byte instrument records). Rendered by **`src/audio/midi_opl_player`** — a MIDI→OPL2
  driver (9-voice allocation with oldest-steal, per-operator register load from the BNK
  patch, note→block/F-number, tempo clock, loop) over **`src/audio/opl2`**, a pimpl wrapper
  around the vendored **ymfm** YM3812 core (FetchContent, pinned commit). Native rate =
  **49715 Hz** (clock/72).
- **SFX = the PC-speaker sweep engine** (`FUN_1000_6e30` presets `1..0x15` → ISR handlers
  `0x9631` swept-tone / `0x96c4` noise+glide / `0x95b5` noise), transcribed as an
  audio-rate simulation in **`src/audio/speaker_sfx`** (`SpeakerVoice`: signed divisor
  sweep, ISR-rate glide, square/noise gen, termination). Presets + the six per-tile map
  tables are baked by **`tools/re/dump_sfx.py`** → `src/resources/sfx_tables.gen.cpp`.
- **Mixer + platform.** **`src/audio/audio_engine`** owns the looping music player + a
  **single** PC-speaker SFX voice (the original engine is monophonic: one global sound
  slot overwritten per play, so a new sound preempts the one in flight — `kVoiceCount=1`),
  mixes to mono float, and is thread-safe (one mutex over all shared state). **`src/platform_sdl3/sdl_audio`** opens an SDL3 stream (F32/mono/49715) whose
  callback pulls `AudioEngine::render`. A missing/locked device degrades to **muted**
  (never fatal). `SdlApp` starts/stops music on the `splash`↔`menu` transition.
- **In-game triggers.** All **27 recovered `6e11` sites** re-inserted in `LevelGame`
  (+ `WorldMap` cloud-jump) as `emit_sfx(id)` using the recovered speaker ids: collect (2
  variants), monster/spike death, block-top land/hop/smash, warp/chute/fall routing (tile
  maps), hop entries, cloud-jump launch, etc. Placement was verified against the decompiled
  source (a first pass had 4 mis-placements — `f_63be`/`f_2810`/`f_6c14`/`f_1e5e`+`f_1e90` —
  all corrected).
- **189 C++ tests pass** (decoders on the real files, synth non-silence/termination,
  engine mixing, in-game SFX emission, program→patch mapping); originals verify clean.
- **Fidelity pass (2026-07-07, commit `4a93e20`):** the first pass's placeholders were
  replaced with values recovered exactly from the binary (capstone), verified by ear vs
  DOSBox and by WAV render (`tools/audio_render`):
  - SFX timing is the real fixed `1193182/2385 = 500.286 Hz` base tick driving a
    Bresenham/DDS divider (not `kSfxIsrBaseHz=1193182/64`, which ran every SFX 6–10× too
    long and made the world→level whoosh never stop). Presets now 0x03≈999 ms / 0x02≈160 ms.
  - Noise (`0x96c4`) is the authentic 16-bit shift register clocked one bit/fire to the
    speaker line, not a square at an invented LFSR divisor.
  - OPL2 patches resolve through the BNK **name index** (program→record→slot) with the
    reg-0xC0 **connection bit inverted** — the fix for the thin "calculator" timbre — plus
    KSL packing and `program=channel+1` defaults; WSE stays off (matches the chip/DOSBox).
  - Wired the missing exit-pit fall SFX (`0x03`, `FUN_1000_28f8 @ 0x292d`).
  - Added a one-pole speaker low-pass (`kSpeakerLowpassHz`, 5 kHz) modelling the beeper cone.
- **Known residual (may revisit):** the audio still does **not exactly match DOSBox** — the
  remaining gap is the PC-speaker filter shape / high-sweep aliasing, not the recovered
  logic. Judged **acceptable for now**; a future pass could model DOSBox's speaker filter
  precisely instead of the single low-pass. Deferred code minors: `adlib_bank` version-byte
  check, decoder negative-path tests, a MIDI same-pitch-retrigger voice.

## Stage 3 3D render mode (2026-07-08)

An optional **"diorama" 3D presentation** of the in-level playfield is implemented.
It was always independent of the earlier `feat/hd-render-mode` xBRZ experiment,
which has since been **abandoned and deleted** (2026-08-22) -- see the note at the
end of this file. The same original assets render pixel-for-pixel, arranged in a real 3D
scene with depth, light, and shader effects — no upscaling, no new art. Design +
plan: `docs/superpowers/specs/2026-07-08-3d-render-mode-design.md`,
`docs/superpowers/plans/2026-07-08-3d-render-mode.md`.

- **Presentation moved to an in-house OpenGL 3.3 core presenter** (`src/platform_gl3`):
  the window now opens a GL context and every screen — not just the 3D diorama —
  draws through it. The **flat path** (menu, map, level, password, scores, outro:
  the existing 320x200 `IndexedFramebuffer` composition, unchanged pixel-for-pixel)
  uploads the frame to a GL texture and draws one quad with a sharp-bilinear
  pixel-art scaling shader; Alt+A (16:10/4:3) and Alt+Enter (fullscreen) become
  viewport math, behavior unchanged. This is the highest-risk point for the
  faithful path, so it is **parity-gated**: `--present-parity` renders three
  representative composed screens (menu/board/map) through the GL flat path at
  x2/x4 integer scales and compares byte-for-byte against the CPU nearest-neighbor
  reference (6/6 PASS, exit 0). **Fallback:** if the window can't get an OpenGL 3.3
  core context, the port silently drops back to the original `SDL_Renderer` path
  (never fatal); the diorama is simply unavailable (`Alt+3` logs "diorama
  unavailable: no usable GL context" — deliberately platform-neutral, since in a
  browser the missing thing is WebGL2, not OpenGL 3.3).
- **Alt+3 diorama** (in-level only): the board's DEC mural becomes a blurred back
  wall (baked gaussian DOF, `kWallBlurSigma`), BUM-plane sprites are classified by
  their own opaque silhouette — solid rectangles (lanes, blocks) become **extruded
  slabs** (front face = original pixels, side/top/bottom faces = the sprite's own
  edge pixels stretched to the extrusion depth, each shaded per face), irregular
  silhouettes (spiky bumpers, deflectors, collectibles, ball, monster) stay crisp
  **billboards**. A narrow-FOV camera sits fixed on the board's central axis; the stage always
  presents **4:3-corrected** (`kCrtPixelAspect`, matching the flat path's Alt+A
  4:3) and `scene_frustum` widens the frustum to fill any window shape — the
  field stays whole and centred while spare width/height reveals the wall,
  continued past its 320x200 edges as a **mirrored** copy (`GL_MIRRORED_REPEAT`).
  Effects: a soft spotlight that follows the ball, a vignette,
  and soft blurred shadows of platform/ball silhouettes projected onto the wall
  with a small offset. All textures sample `GL_NEAREST` — no filtering of the
  source pixels themselves. Scene building lives in `src/video3d/` (CPU-side scene
  model: wall texture + blur, slab/billboard geometry, live quad list from the same
  `LevelGame` state that feeds the flat `render_level()`) and `src/platform_gl3`
  (`SceneRenderer`, the GL programs/textures/draw calls). Every look-tuning knob
  (wall Z, extrusion depth, ambient/spot/vignette strength,
  shadow offset/blur/alpha) is a named constant in `src/video3d/scene3d.h` /
  `slab_mesh.h`, meant to be adjusted by eye rather than re-derived.
- **Switching, three ways, always in sync:** **Alt+3** toggles instantly (hard cut,
  no transition animation) whenever a GL context exists; **`--render3d`** on the
  command line starts a session already in 3D; and the choice **persists across
  restarts** in **`bumpy_port.cfg`** (a plain `key=value` file written next to the
  exe) — the port's **first on-disk persistence** of any kind (high scores stay
  session-only, matching the original). The same file also carries the
  aspect/fullscreen state from the earlier Alt+A/Alt+Enter work. Unknown keys are
  ignored and bad values fall back to defaults, so older/newer builds tolerate each
  other's config files.
- **Tools:** **`--render-3d <level> <MONDE.VEC> <board> <out.bmp>`** dumps one
  diorama frame headlessly for by-eye inspection (`analysis/generated/render3d_final.bmp`);
  **`--present-parity`** runs the flat-path parity gate above. Debug builds only:
  **Alt+R** recompiles the diorama shaders in place from `shaders3d/` without
  restarting — reload failures keep the previous (working) programs and print
  `shader reload failed; keeping previous` instead of crashing or going black, so a
  broken shader edit never loses the running session.
- **Scope / phase 2:** 3D dressing covers the **in-level playfield only** — menu,
  world map, password/high-score/outro screens still render flat even with Alt+3
  on (dressing those is a separate future phase, not started). Game logic, timing,
  and positions are completely untouched by any of this; the flat 320x200
  composition still runs every frame (even in 3D mode) so the screen-change darken
  and the two presentation paths stay trivially in sync.
- **254 C++ tests pass** (82783 assertions) covering the GL helpers, mat4/blur math,
  scene decomposition (slab-vs-billboard classification, live quad building),
  slab/billboard face geometry, the renderer's shader reload path, and the config
  parser/serializer; originals verify clean. Verified by eye: the parity dump, the
  live diorama in-window (springs, monster, collectibles, exit portal, win/lose
  transitions), and the headless `--render-3d` dump.

## Next step

**All 9 worlds are playable end-to-end** — launch, navigate the map, clear boards,
advance worlds 1→2→…→9, then the **ending screen** and back to the menu. Use
`--start-world N` to jump to any world for testing.

Remaining Stage 3 milestones and optional polish:

- **Live DOSBox side-by-side for worlds 2–9**: the new element systems (nests,
  block-top riding, picture puzzle, entry drop) are tested + smoke-verified in
  the port; a by-eye comparison against the original playing the same boards
  (passwords or a save in DOSBox-X) is the remaining confirmation.
- **Pause key** (`FUN_1000_7ab4(0x19)` → `49d7`): the main loop's P-pause is
  not ported. Minor.
- **In-level score/lives HUD**: the in-level HUD is intentionally absent (the
  original's `0816` call is gated on an event flag; normal play shows no
  persistent in-level score). Low priority.
- **Compressed sprite frames** (flags `0x40`/`0x20`, `1cec:2ded`): fully recovered
  from disassembly but unused by the supplied assets — `BUMSPJEU` is all
  uncompressed. Deferred until needed.
- **Menu/password screens — all done.** HIGH-SCORE, LEVEL (difficulty), PASSWORD, and
  the between-worlds password display (`FUN_1000_0d9d`) are implemented.

## Placeholders / remaining work

- Compressed sprite frames (flags `0x40`/`0x20`, `1cec:2ded`) are **fully recovered
  from disassembly but unused by the supplied assets** — `BUMSPJEU` is all
  uncompressed and the compressed `BUMPYSPR.BIN`/`SPRITE.BIN` are not shipped. The
  decoder is documented (`analysis/specs/menu-resource-formats.md`) but not
  transcribed into the port (nothing to decode/validate).
- The **HIGH-SCORE** and **PASSWORD** menu sub-screens are both **implemented** (see
  "Stage 3 high-score screen" and "Stage 3 password screen"). All menu items are done,
  and the between-worlds password display (`FUN_1000_0d9d`) is implemented.
- The in-window level screen is **live** (ball state machine + collect/score/win +
  the tile bump/spring animations + the **moving entity / enemy AI + death** — see the
  in-level-loop and "moving entity" sections above). Still missing in-level: the
  in-level score/lives HUD (low priority, see above).
- World-9 outro (`FUN_1000_3ed4`, `DESSFIN.VEC` ending screen) is **implemented** —
  see "Stage 3 world-9 outro" above.

The menu is complete: **PLAY**, **HIGH-SCORE** (see "Stage 3 high-score screen"),
**LEVEL** difficulty (see "Stage 3 difficulty selection"), and **PASSWORD** (see
"Stage 3 password screen") are all implemented. The between-worlds password display
(`FUN_1000_0d9d`) is implemented too.

## Abandoned: HD render mode (`feat/hd-render-mode`, deleted 2026-08-22)

An `Alt+H` layered-xBRZ upscaling mode, developed to a merge-ready state in July
2026 and never merged, because nobody ever looked at it on screen. Dropped at the
owner's decision -- the diorama covers what he wanted from a "better-looking" mode,
and upscaled sprites were never the point of a port whose premise is the original
1993 art unchanged.

Deleting it also closed a licensing problem that merging it would have forced:
the branch vendored **xBRZ, which is GPLv3**, into an MIT-licensed project. That
code never reached master.

The 12 commits are unreferenced but not yet collected. Until git prunes them,
`git branch feat/hd-render-mode 0af459cfcd23f0dd34f2e686164b21f068e60e7a`
brings the branch back. Recorded because a deleted local-only branch has no other
trace: it was never pushed anywhere.
