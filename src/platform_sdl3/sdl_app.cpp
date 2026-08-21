#include "platform_sdl3/sdl_app.h"

#include "game/level_game.h"
#include "game/settings_overlay.h"
#include "game/speed_pacer.h"
#ifndef __EMSCRIPTEN__
#include "platform_gl3/scene_renderer.h"
#endif
#include "resources/world_resources.h"
#include "video/board_renderer.h"
#include "video/high_score_renderer.h"
#include "video/hud.h"
#include "video/map_renderer.h"
#include "video/password_renderer.h"
#include "video/screen_image.h"
#include "video/screen_transition.h"
#include "video/viewport.h"
#include "video3d/scene3d.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {

void require(bool ok) {
    if (!ok) {
        throw std::runtime_error(SDL_GetError());
    }
}

void update_key_state(bumpy::MenuInput& input, SDL_Keycode key, bool pressed) {
    switch (key) {
    case SDLK_UP:
        input.up = pressed;
        break;
    case SDLK_DOWN:
        input.down = pressed;
        break;
    case SDLK_LEFT:
        input.left = pressed;
        break;
    case SDLK_RIGHT:
        input.right = pressed;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        input.confirm = pressed;
        break;
    case SDLK_ESCAPE:
        input.cancel = pressed;
        break;
    default:
        break;
    }
}

// The original paces every game step on the VGA vertical retrace -- a two-phase poll of
// port 0x3DA bit 3, reached via the per-video-mode dispatch at the tail of FUN_1ab9_0351
// (the `7bdd` wait); see analysis/specs/screen-flow.md ("Frame timing"). For VGA's
// 320x200 16-colour mode the vertical refresh is 70.086 Hz.
constexpr double kVgaRefreshHz = 70.086;

// The sequences driven by the {frame,dx,dy} script stepper FUN_1000_13df -- in-level
// gameplay and the world-map cloud-jump -- advance one step per *two* retraces, i.e.
// 35.043 Hz. Confirmed by side-by-side comparison with the original under DosBox: paced
// at the full 70 Hz the in-level ball ran a clean, stable 2x too fast (it bounced twice
// per original bounce), and the cloud-jump likewise matched only at the halved rate.
// World-map navigation (the FUN_1000_3ab2..3bc9 slide) and the menu instead step once
// per retrace -- at 35 Hz the node-to-node slide visibly dragged. The retrace handler
// sits behind a jump table Ghidra could not recover, so the /2 is pinned empirically
// rather than read from the disassembly. The run loop selects per phase (see half_rate).
constexpr double kGameTickHz = kVgaRefreshHz / 2.0;

// How many retraces each ring of the edge-to-centre darken (FUN_1000_3467) is held for.
// The original runs the fill un-paced (one CPU-bound burst); the port spreads the 10
// rings over frames so the wipe is visible. At 1 frame/ring (70 Hz) the close is ~0.14 s;
// holding each ring 2 frames (~35 Hz) gives ~0.29 s. This is the knob for the wipe speed.
constexpr int kDarkenFramesPerRing = 2;

}  // namespace

namespace bumpy {

SdlApp::SdlApp() {
    // SDL_INIT_AUDIO is needed here (not just implicitly by SdlAudio's
    // SDL_OpenAudioDeviceStream) because SDL_OpenAudioDeviceStream fails with
    // "Audio subsystem is not initialized" if the subsystem was never brought up.
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO));
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
        // Fallback: the original SDL_Renderer presentation, flat only.
        window_ = SDL_CreateWindow("Bumpy's Arcade Fantasy", 960, 600, SDL_WINDOW_RESIZABLE);
        if (!window_) {
            SDL_Quit();
            throw std::runtime_error(SDL_GetError());
        }
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            SDL_DestroyWindow(window_);
            SDL_Quit();
            throw std::runtime_error(SDL_GetError());
        }
        texture_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 320, 200);
        if (!texture_) {
            SDL_DestroyRenderer(renderer_);
            SDL_DestroyWindow(window_);
            SDL_Quit();
            throw std::runtime_error(SDL_GetError());
        }
        // Sharp integer-style upscale that stays uniform at non-integer sizes. Pure NEAREST
        // multiplies each source pixel into an NxN block, but at fractional scales (e.g. 320 -> 1728
        // for a letterboxed 1080p fullscreen = 5.4x) some source pixels land 5 device-px wide and
        // their neighbours 6, which shimmers on motion. SDL 3.4's PIXELART mode prescales by the
        // integer factor and applies a <=1px linear ramp only at pixel boundaries, so the interior
        // stays crisp (no blur) while every pixel reads the same size. It does NOT invent detail --
        // the assets are fixed low-res bitmaps -- it just scales the existing pixels cleanly.
        // (SDL_SCALEMODE_NEAREST is the bit-exact-to-DOSBox alternative if the edge ramp is ever
        // unwanted.)
        require(SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_PIXELART));
        // Scale the 320x200 framebuffer up to the window (or fullscreen), letterboxing to
        // preserve aspect so Alt+Enter fullscreen on a 16:9 monitor never stretches the picture
        // (and a manually resized window stays undistorted). The default logical size 320x200
        // gives *square* pixels (16:10) -- matching the DOSBox-X reference the port is validated
        // against (aspect=false). Alt+A switches to the authentic CRT 4:3 (320x240, the 200 VGA
        // lines stretched to 240) at runtime; see run(). RenderTexture with a null dst then fills
        // whichever logical size is active 1:1.
        require(SDL_SetRenderLogicalPresentation(
            renderer_, 320, 200, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    }
}

SdlApp::~SdlApp() {
    // gl_ must be torn down explicitly, here, before window_: GlPresenter's destructor
    // calls SDL_GL_MakeCurrent/SDL_GL_DestroyContext against window_, and SDL_DestroyWindow
    // additionally unloads the GL library once an OpenGL-flagged window is gone. A
    // destructor's own body always runs before its members' (here: gl_, texture_,
    // renderer_, window_ in reverse declaration order) are automatically destroyed, so
    // relying on gl_ being declared after window_ alone would tear down window_ (in the
    // explicit SDL_DestroyWindow call below) first and leave gl_'s later automatic
    // destruction touching an already-destroyed window -- hence the explicit reset here.
    // (The declaration order -- gl_ last -- still matters for the theoretical partial-
    // construction unwind path, where no destructor body runs at all.)
#ifndef __EMSCRIPTEN__
    gl_.reset();
#endif
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

int SdlApp::run(App& app, const MenuRenderer& menu_renderer,
                const SettingsRenderer& settings_renderer,
                const std::filesystem::path& asset_root,
                WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
                std::span<const std::uint8_t> splash_screen,
                std::span<const std::uint8_t> outro_screen,
                std::span<const std::uint8_t> score_screen, IndexedFramebuffer& frame,
                AudioEngine& audio, PortConfig config, std::filesystem::path config_path) {
    bool running = true;
    MenuInput input{};
    SettingsOverlay overlay;
    bool overlay_open = false;

    // FUN_1000_30dd: the intro tune loops for as long as the startup splash is showing.
    // Splash is the initial screen, so arm it before the loop's first iteration; the
    // screen-change tracking below (via `before`) stops it the moment the player leaves
    // for the menu.
    if (app.screen() == Screen::splash && config.music) {
        audio.start_music();
    }
    audio.set_sfx_enabled(config.sfx);

    // The in-level game state machine, created when the level screen is entered for a
    // board and destroyed when it is left. nullopt off the playfield.
    std::optional<LevelGame> game;
    // FUN_1000_328f: true while a freshly-created board is frozen waiting for the player's
    // first key/button press. The ball hangs at its entry position (12px above its start
    // cell); nothing advances until an input arrives. Set on board creation, cleared by the
    // first input. Peer of the screen-darken in FUN_1000_0c18's per-board setup (see
    // analysis/specs/game-loop.md), so it lives here in the shell, not in LevelGame::tick.
    bool level_awaiting_start = false;
    // The in-level frame pacer (FUN_1000_1349): the LEVEL menu difficulty selects an
    // 8-bit mask (App::level_pattern) that decides, per frame, whether the loop waits one
    // or two vertical retraces. Reset to the run's pattern when each board is created.
    SpeedPacer level_pacer;
    auto live_entities = [&]() {
        // Build a BumEntities view of LevelGame's live grid so collected collectibles
        // (cleared in plane C) stop being drawn.
        BumEntities live{};
        const auto& grid = game->grid();
        std::copy(grid.begin(), grid.begin() + BumEntities::record_size, live.bytes.begin());
        return live;
    };

    // Compose the playfield (board art + live entities + tile animations + ball) into
    // `frame`. Shared by the per-frame render and the terminal-frame capture below so the
    // edge-to-centre darken on a won/lost board freezes the *resolved* scene -- e.g. the
    // ball fully sunk into the exit pit (descent frame 0x20), not a half-descended frame
    // still showing the ball on top of the pit. This matches the original's frame order
    // (render at 1cb2 -> vsync -> player tick at 1d26 sets the win flag), where the last
    // playfield the map's FUN_1000_3467 darkens is the one in which the ball has vanished.
    auto render_level = [&]() {
        render_board(world.level(), app.board_index(), world.backdrop(), frame);
        if (game) {
            BumEntities live = live_entities();
            // Tile bump/spring animations: pull the live slots, blank the static tile
            // under each so only the moving spring sprite draws (matching the original's
            // background restore), then overlay the spring frames.
            std::array<ObjectAnimSprite, 7> anims{};
            const std::size_t anim_count = game->object_anims(anims);
            for (std::size_t k = 0; k < anim_count; ++k) {
                const std::size_t cell = anims[k].cell;
                const std::size_t off = anims[k].layer_b ? BumEntities::layer_b_offset
                                                         : BumEntities::layer_a_offset;
                live.bytes[cell + off] = 0;
            }
            draw_bum_entities(live, sprite_bank, frame);
            draw_object_anims({anims.data(), anim_count}, sprite_bank, frame);
            if (game->monster_present()) {
                draw_monster(sprite_bank, game->monster_frame(), game->monster_x(),
                             game->monster_y(), frame);
            }
            draw_ball(sprite_bank, game->ball_frame(), game->ball_x(), game->ball_y(), frame);
        } else {
            draw_bum_entities(world.level().bum_entities(app.board_index()), sprite_bank, frame);
        }
    };

    // Per-phase pacing. The engine has two frame loops with different retrace-wait
    // counts (see kGameTickHz): sequences driven by the {frame,dx,dy} script stepper
    // FUN_1000_13df -- in-level gameplay and the world-map cloud-jump -- step once per
    // *two* retraces (35.043 Hz), while world-map navigation (the FUN_1000_3ab2..3bc9
    // slide) and the menu step once per retrace (70.086 Hz). Confirmed by side-by-side
    // DosBox comparison: at a uniform 35 Hz the node-to-node slide dragged, while the
    // cloud-jump and gameplay matched. We pick the period each frame from the live phase.
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const Uint64 period_full = static_cast<Uint64>(static_cast<double>(perf_freq) / kVgaRefreshHz);
    const Uint64 period_half = static_cast<Uint64>(static_cast<double>(perf_freq) / kGameTickHz);
    Uint64 next_frame = SDL_GetPerformanceCounter();

    // The original darkens the screen from the edges to the centre on every screen change
    // (FUN_1000_3467; see analysis/specs/screen-flow.md). We snapshot the outgoing screen
    // and play the closing-box wipe over it before the incoming screen renders. Each ring
    // is held for kDarkenFramesPerRing retraces to pace the close.
    ScreenTransition transition;
    int darken_hold = 0;  // retraces the current ring has been shown

    // Display aspect, toggled live with Alt+A. The 320x200 framebuffer is presented either
    // at 16:10 (square pixels, logical 320x200) or at 4:3 (logical 320x240, the 200 VGA lines
    // stretched to 240 -- what a real VGA CRT physically showed, since mode 13h pixels are
    // ~1.2x taller than wide). The two aren't wrong-vs-right, they answer different questions:
    // 16:10 shows the art exactly as authored on the pixel grid (and matches the DOSBox-X
    // reference, aspect=false), while 4:3 is hardware-accurate. This game's art was drawn round
    // on the *square* grid -- the map nodes are true circles at 16:10 -- so 16:10 (the default)
    // keeps them round, whereas 4:3 stretches them ~1.2x taller. The artist did not pre-squash
    // to compensate for the CRT, so on real hardware those nodes were in fact slightly egg-
    // shaped. Letterboxed to the window/fullscreen either way. Starts on 16:10, matching the
    // constructor's logical presentation.
    // Presentation state, seeded from the persisted config. render3d only arms when
    // the GL presenter is live (Alt+3 needs shaders); the flag itself is kept so a
    // machine upgrade re-enables it.
    bool square_pixels = config.square_pixels;
#ifdef __EMSCRIPTEN__
    bool render3d = false;  // stage 1 of the web port compiles no GL
#else
    bool render3d = config.render3d && gl_ != nullptr;
#endif
#ifdef __EMSCRIPTEN__
    // Product decision: the web build is 4:3 only. 4:3 stays the base geometry, so any
    // future widescreen extends the 4:3 view rather than stretching a 16:10 image.
    square_pixels = false;
    config.square_pixels = false;
#endif
    auto apply_aspect = [&]() {
        if (!renderer_) {
            return;  // GL path: present_flat picks 200/240 from square_pixels directly
        }
        require(SDL_SetRenderLogicalPresentation(
            renderer_, 320, square_pixels ? 200 : 240, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    };
    apply_aspect();
    if (config.fullscreen) {
        SDL_SetWindowFullscreen(window_, true);
    }
    auto persist = [&]() {
        if (!save_port_config(config_path, config)) {
            std::cerr << "warning: could not write " << config_path.string() << '\n';
        }
    };

#ifndef __EMSCRIPTEN__
    // --- 3D diorama state (Alt+3). The flat 320x200 composition in `frame` still
    // runs every frame even in 3D mode: the screen-change darken snapshots it, and
    // it keeps the two paths trivially in sync. 3D only swaps the PRESENTATION.
    std::unique_ptr<SceneRenderer> scene_renderer;
    bool scene_renderer_failed = false;  // shader/setup failure: 3D disabled for the run
    SpriteCache sprite_cache(sprite_bank);
    int scene_world = -1;
    std::size_t scene_board = static_cast<std::size_t>(-1);
    // Level->map darken while 3D is on: presenting the flat wipe would pop the
    // diorama off for the whole close. Instead the resolved scene is stashed when
    // the board ends and present_3d_wipe replays it under the closing border.
    bool wipe_3d = false;
    std::vector<SceneQuad> wipe_quads;
    float wipe_light_x = 0.0f;
    float wipe_light_y = 0.0f;
    auto shader_dir = [&]() -> std::filesystem::path {
        if (const char* base = SDL_GetBasePath()) {
            const std::filesystem::path candidate = std::filesystem::path(base) / "shaders3d";
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) {
                return candidate;
            }
        }
        return asset_root / "shaders3d";
    };

    // Live quads: the exact same inputs the flat render_level composes. Shared by
    // the per-frame 3D present and the level-end stash for the 3D darken.
    auto collect_live_quads = [&]() -> std::vector<SceneQuad> {
        BumEntities live = live_entities();
        std::array<ObjectAnimSprite, 7> anims{};
        const std::size_t anim_count = game->object_anims(anims);
        for (std::size_t k = 0; k < anim_count; ++k) {
            const std::size_t off = anims[k].layer_b ? BumEntities::layer_b_offset
                                                     : BumEntities::layer_a_offset;
            live.bytes[anims[k].cell + off] = 0;
        }
        std::optional<MonsterPose> monster;
        if (game->monster_present()) {
            monster = MonsterPose{game->monster_frame(), game->monster_x(), game->monster_y()};
        }
        return build_live_quads(
            live, {anims.data(), anim_count}, monster,
            BallPose{game->ball_frame(), game->ball_x(), game->ball_y()}, sprite_cache);
    };

    // Present the level through the diorama; false = caller presents flat instead
    // (no GL, renderer failed, mode off, or no live board this frame).
    auto present_3d_level = [&]() -> bool {
        if (!render3d || !gl_ || scene_renderer_failed || !game) {
            return false;
        }
        if (!scene_renderer) {
            try {
                scene_renderer = std::make_unique<SceneRenderer>(gl_->gl(), shader_dir());
            } catch (const std::exception& error) {
                std::cerr << "warning: 3D mode disabled: " << error.what() << '\n';
                scene_renderer_failed = true;
                return false;
            }
        }
        if (scene_world != world.world() || scene_board != app.board_index()) {
            const Scene3d scene =
                build_scene3d(world.level(), app.board_index(), world.backdrop());
            scene_renderer->set_scene(scene, sprite_cache);
            scene_world = world.world();
            scene_board = app.board_index();
        }
        const auto quads = collect_live_quads();

        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSizeInPixels(window_, &win_w, &win_h);
        // 3D fills the whole window at any shape: scene_frustum keeps the 4:3-
        // corrected field whole and centred; spare window area shows mirrored
        // wall. Alt+A (square_pixels) only affects the flat path.
        const Viewport vp{0, 0, win_w, win_h};
        gl_->gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
        scene_renderer->render(quads, static_cast<float>(game->ball_x()),
                               static_cast<float>(game->ball_y()), vp);
        SDL_GL_SwapWindow(window_);
        return true;
    };

    // Present one darken ring over the stashed terminal 3D scene: re-render the
    // frozen diorama and close ScreenTransition's black border over it (scissored
    // clears -- no new GL objects), the 320x200 cell grid scaled to the window.
    // false = caller presents the flat wipe instead (mode off mid-wipe, no GL, or
    // the outgoing screen was not a 3D-presented level).
    auto present_3d_wipe = [&]() -> bool {
        if (!wipe_3d || !render3d || !gl_ || !scene_renderer) {
            return false;
        }
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSizeInPixels(window_, &win_w, &win_h);
        gl_->gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
        scene_renderer->render(wipe_quads, wipe_light_x, wipe_light_y,
                               Viewport{0, 0, win_w, win_h});
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        if (transition.step() >= ScreenTransition::kSteps) {
            // The last ring closes the full width (2*kCellW*kSteps == 320): clear
            // everything rather than trusting integer rounding to meet mid-screen.
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            const int bx = win_w * ScreenTransition::kCellW * transition.step() / 320;
            const int by = win_h * ScreenTransition::kCellH * transition.step() / 200;
            glEnable(GL_SCISSOR_TEST);
            // The four border bars are symmetric, so GL's bottom-left origin
            // needs no flip.
            glScissor(0, 0, bx, win_h);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(win_w - bx, 0, bx, win_h);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(0, 0, win_w, by);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(0, win_h - by, win_w, by);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
        SDL_GL_SwapWindow(window_);
        return true;
    };
#else
    // Stage 1 of the web port compiles no GL, so both 3D presentations are permanently
    // unavailable and every call site below falls through to the flat path unchanged.
    auto present_3d_level = []() { return false; };
    auto present_3d_wipe = []() { return false; };
#endif

    auto present_frame = [&]() {
#ifndef __EMSCRIPTEN__
        if (gl_) {
            gl_->present_flat(frame, square_pixels ? 200 : 240);
            return;
        }
#endif
        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));
    };

    // Wait until the next tick boundary: sleep the bulk (1ms granularity) then spin the
    // final sub-millisecond for an accurate cadence. If a frame ran long, resync instead
    // of accumulating debt.
    auto wait_next_tick = [&](Uint64 tick_period) {
#if defined(BUMPY_PACE_PROBE)
        // Pace probe (opt-in, never in a shipped build). wait_next_tick is the single
        // mechanism the web port's cadence depends on and it runs on every screen, so
        // instrumenting it -- rather than in-level ticks -- needs no gameplay and
        // measures desktop and browser with the same code.
        const Uint64 probe_entry = SDL_GetPerformanceCounter();
#endif
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
#if defined(BUMPY_PACE_PROBE)
        {
            // Report every 300 calls. The window is measured exit-to-exit so it spans
            // exactly 300 whole periods (frame work + wait), never a ragged half period.
            //   achieved  = 300 / window seconds
            //   requested = perf_freq / tick_period on the most recent call
            //   busy      = (window - time spent inside this wait) / window, i.e. the
            //               share of each period spent on frame work. Near or above 1.0
            //               means the loop is saturated and the rate is limited by the
            //               work, not by the waiting mechanism.
            const Uint64 probe_exit = SDL_GetPerformanceCounter();
            static Uint64 probe_window_start = 0;
            static Uint64 probe_wait_total = 0;
            static Uint64 probe_period_prev = 0;
            static bool probe_period_varied = false;
            static int probe_calls = 0;
            if (probe_window_start == 0) {
                probe_window_start = probe_exit;  // first call only opens the window
            } else {
                probe_wait_total += probe_exit - probe_entry;
                if (tick_period != probe_period_prev) {
                    probe_period_varied = true;
                }
                if (++probe_calls == 300) {
                    const double freq = static_cast<double>(perf_freq);
                    const double elapsed =
                        static_cast<double>(probe_exit - probe_window_start) / freq;
                    const double waited = static_cast<double>(probe_wait_total) / freq;
                    std::printf(
                        "[pace] 300 waits in %.1f ms -> %.3f Hz achieved, %.3f Hz "
                        "requested%s, busy %.3f\n",
                        elapsed * 1000.0, 300.0 / elapsed,
                        freq / static_cast<double>(tick_period),
                        probe_period_varied ? " (VARIED across window)" : "",
                        (elapsed - waited) / elapsed);
                    std::fflush(stdout);
                    probe_window_start = probe_exit;
                    probe_wait_total = 0;
                    probe_calls = 0;
                    probe_period_varied = false;
                }
            }
            probe_period_prev = tick_period;
        }
#endif
    };

    while (running) {
        // The App requested a different world (start, world-advance, or game-over reset):
        // swap the world's disk resources and tell App the new board count. This runs
        // before any render, and on a screen change the darken (begun the prior frame)
        // covers the swap. On failure, cancel the request and stay on the current world.
        if (app.pending_world() != 0) {
            const int requested = app.pending_world();
            try {
                world = WorldResources::load(asset_root, requested);
                app.enter_world(requested, world.board_count());
            } catch (const std::exception& error) {
                std::cerr << "could not load world " << requested << ": " << error.what()
                          << " -- staying on world " << world.world() << '\n';
                // Re-enter the current world to clear the pending request. NOTE: enter_world
                // zeroes cleared_, so a failed *advance* drops the player back to replay the
                // current world from scratch. This only fires on missing/corrupt assets (a
                // launch-time warning already covers that), never in normal play.
                app.enter_world(world.world(), world.board_count());
            }
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                // Alt+Enter toggles fullscreen (the DOS-era convention). Swallow this Enter
                // so it does not also register as a menu/fire confirm on the same frame.
                if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) &&
                    (event.key.mod & SDL_KMOD_ALT)) {
                    const bool fullscreen =
                        (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                    SDL_SetWindowFullscreen(window_, !fullscreen);
                    config.fullscreen = !fullscreen;
                    persist();
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
#ifndef __EMSCRIPTEN__
                } else if (event.key.key == SDLK_3 && (event.key.mod & SDL_KMOD_ALT)) {
                    // Alt+3: original <-> 3D diorama (hard cut, per the design spec).
                    if (gl_) {
                        render3d = !render3d;
                        config.render3d = render3d;
                        persist();
                    } else {
                        std::cerr << "3D mode unavailable: no OpenGL 3.3\n";
                    }
#ifndef NDEBUG
                } else if (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_ALT)) {
                    // Debug look-iteration: recompile the diorama shaders in place.
                    if (scene_renderer) {
                        std::cerr << (scene_renderer->reload_shaders()
                                          ? "shaders reloaded\n"
                                          : "shader reload failed; keeping previous\n");
                    }
#endif
#endif  // !__EMSCRIPTEN__
                } else if (event.key.key == SDLK_TAB) {
                    // Tab: open/close the settings overlay. Only openable on the play
                    // surfaces (menu / map / level); a no-op elsewhere.
                    if (overlay_open) {
                        overlay_open = false;
                    } else if (app.screen() == Screen::menu || app.screen() == Screen::map ||
                               app.screen() == Screen::level) {
                        overlay.reset();
                        overlay_open = true;
                    }
                    // Clear any held key so the first frame after this transition (overlay
                    // open OR close) doesn't leak a stale edge into the (soon-to-be-frozen
                    // or just-resumed) app/game update.
                    input = MenuInput{};
                } else {
                    update_key_state(input, event.key.key, true);
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                update_key_state(input, event.key.key, false);
            }
        }
        if (!running) {
            break;
        }

        // Modal settings overlay (Tab). While open, the world is frozen: no app.update,
        // no game tick, no transition stepping. Input drives the overlay; each event is
        // applied here (side effect + PortConfig write), reusing the hotkey side effects.
        if (overlay_open) {
#ifdef __EMSCRIPTEN__
            switch (overlay.update(input, false)) {
#else
            switch (overlay.update(input, gl_ != nullptr)) {
#endif
            case SettingsEvent::toggle_3d:
#ifndef __EMSCRIPTEN__
                if (gl_) {
                    render3d = !render3d;
                    config.render3d = render3d;
                    persist();
                }
#endif
                break;
#ifndef __EMSCRIPTEN__
            case SettingsEvent::toggle_aspect:
                square_pixels = !square_pixels;
                apply_aspect();
                config.square_pixels = square_pixels;
                persist();
                break;
#endif
            case SettingsEvent::toggle_fullscreen: {
                const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(window_, !fs);
                config.fullscreen = !fs;
                persist();
                break;
            }
            case SettingsEvent::toggle_music:
                config.music = !config.music;
                if (config.music) {
                    if (app.screen() == Screen::splash) audio.start_music();
                } else {
                    audio.stop_music();
                }
                persist();
                break;
            case SettingsEvent::toggle_sfx:
                config.sfx = !config.sfx;
                audio.set_sfx_enabled(config.sfx);
                persist();
                break;
            case SettingsEvent::quit:
                running = false;
                break;
            case SettingsEvent::close:
                overlay_open = false;
                // Esc/Left-close: that key is still physically held (no KEY_UP yet), and
                // App's waiting_for_release_ latch was frozen while the overlay was open,
                // so without this the held key reads as a fresh edge next frame (quit /
                // back_to_menu / life-loss). Clear it so the resumed screen sees no input.
                input = MenuInput{};
                break;
            case SettingsEvent::none:
                break;
            }
            if (!running) {
                break;
            }
            if (overlay_open) {
                SettingsView view{};
                view.page = overlay.page();
                view.cursor_row = overlay.cursor_row();
                view.render3d = render3d;
                view.square_pixels = square_pixels;
                view.fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                view.music = config.music;
                view.sfx = config.sfx;
#ifdef __EMSCRIPTEN__
                view.render3d_available = false;
#else
                view.render3d_available = gl_ != nullptr;
#endif
                settings_renderer.render(view, frame);
                present_frame();  // flat path, over both GL and SDL_Renderer back-ends
            }
            wait_next_tick(period_full);  // menu-rate; keeps cadence so close has no burst
            continue;                     // freeze the world under the overlay
        }

        // While the edge-to-centre darken is playing, freeze all game logic (the original
        // runs FUN_1000_3467 synchronously before the next screen loads) and just step the
        // wipe over the snapshotted outgoing screen. Each ring is shown kDarkenFramesPerRing
        // retraces before advancing inward.
        if (transition.active()) {
            if (!present_3d_wipe()) {
                transition.render(frame);
                present_frame();
            }
            wait_next_tick(period_full);
            if (++darken_hold >= kDarkenFramesPerRing) {
                darken_hold = 0;
                transition.advance();  // may deactivate after the final (fully black) ring
#ifndef __EMSCRIPTEN__
                if (!transition.active()) {
                    wipe_3d = false;  // the 3D close (if any) is done; next wipe re-arms
                }
#endif
            }
            continue;
        }

        // The App owns menu/map screen transitions (menu -> quit, map -> game over on
        // Escape). In-level Escape is owned by LevelGame (fed via LevelInput.cancel below:
        // FUN_1000_1d26 -> FUN_1000_22fc, lose a life), so the event loop no longer
        // special-cases Escape.
        const Screen before = app.screen();
        if (app.update(input) == AppOutcome::quit) {
            running = false;
        }
        // Drain the world-map's queued sound events (currently just the cloud-jump
        // launch, FUN_1000_3cf7) every frame -- cheap no-op when nothing was queued.
        for (std::uint8_t id : app.world_map().take_sfx_events()) {
            audio.play_sfx(id);
        }
        if (!running) {
            break;
        }
        bool screen_changed = app.screen() != before;
        if (screen_changed) {
            // Splash -> menu is the only transition into/out of the splash screen (it is
            // startup-only), so this is exactly FUN_1000_30dd's loop-until-keypress.
            if (before == Screen::splash) {
                audio.stop_music();
            } else if (app.screen() == Screen::splash && config.music) {
                audio.start_music();
            }
        }
        bool level_ticked = false;  // did an in-level game tick advance this frame?
        Uint64 level_period = 0;    // its FUN_1000_1349 pace (retrace count * period_full)

        // Drive the in-level game state machine on the playfield. Arrow keys move the
        // ball; confirm (Enter/Space) is the fire button. Skipped on the frame a screen
        // change starts so the board is not created/ticked under the darken (the original
        // loads the board only after FUN_1000_3467 finishes).
        if (!screen_changed) {
            const Screen pre_game = app.screen();
            if (app.screen() == Screen::level) {
                if (!game) {
                    if (app.board_index() < world.level().bum_board_count()) {
                        // Carry the run's lives/score into the board.
                        game.emplace(world.level().bum_entities(app.board_index()), app.lives(),
                                     app.score());
                        level_awaiting_start = true;  // FUN_1000_328f: hold until first input
                        level_pacer.reset(app.level_pattern());  // arm the difficulty pace (854f)
                    } else {
                        app.leave_level();  // no entity data for this board
                    }
                }
                if (game) {
                    const LevelInput li{input.left,  input.right,  input.up,
                                        input.down,   input.confirm, input.cancel};
                    // FUN_1000_328f: the original sets up the board (ball hanging 12px above
                    // its start cell), draws it, then spins reading input until any key/button
                    // is pressed -- only then does the frame loop run and play the drop in.
                    // While waiting, the whole board is frozen (no tick: ball, monster,
                    // springs, PRNG all held) and render_level() below draws the hanging ball.
                    // The first input begins play and ticks this same frame (328f returns the
                    // instant 1dde sees input, then the loop's first iteration runs) -- no
                    // release edge, matching the original (a held key starts immediately).
                    if (level_awaiting_start && (li.left || li.right || li.up || li.down || li.fire)) {
                        level_awaiting_start = false;
                    }
                    if (!level_awaiting_start) {
                        game->tick(li);
                        level_ticked = true;
                        // Drain this tick's queued sound events (every recovered FUN_1000_6e11
                        // site) into the audible engine. Never fires while level_awaiting_start
                        // holds -- the board-start pause already gates tick() above.
                        for (std::uint8_t id : game->take_sfx_events()) {
                            audio.play_sfx(id);
                        }
                        // FUN_1000_1349: this frame waits 1 or 2 retraces per the difficulty
                        // mask, so the board runs slower on EASY (2) and faster on HARD (1).
                        level_period = static_cast<Uint64>(level_pacer.step()) * period_full;
                    }
                    if (game->status() != LevelStatus::playing) {
                        // Draw the resolved terminal frame (ball sunk into the pit on a win,
                        // death pose on a loss) into `frame` *before* leaving the board, so the
                        // edge-to-centre darken started below freezes that frame -- the original
                        // renders the win-setting frame, then the map darkens it. Without this the
                        // darken would freeze the previous (still-descending) frame, leaving the
                        // ball visibly on top of the pit.
                        render_level();
#ifndef __EMSCRIPTEN__
                        // In 3D mode the darken must keep the diorama on screen:
                        // stash the same resolved scene for present_3d_wipe to
                        // replay under the closing border (the flat `frame`
                        // snapshot stays as the fallback).
                        wipe_3d = render3d && scene_renderer && !scene_renderer_failed &&
                                  scene_world == world.world() &&
                                  scene_board == app.board_index();
                        if (wipe_3d) {
                            wipe_quads = collect_live_quads();
                            wipe_light_x = static_cast<float>(game->ball_x());
                            wipe_light_y = static_cast<float>(game->ball_y());
                        }
#endif
                        // Win/lose/game-over: carry lives+score back and update the run.
                        app.finish_level(game->status(), game->lives(), game->score());
                        game.reset();
                    }
                }
            } else {
                game.reset();
            }
            // A board that won/lost/quit flips level -> map here, not via app.update().
            screen_changed = app.screen() != pre_game;
        }

        // On any screen change, `frame` still holds the previous iteration's render of the
        // outgoing screen: snapshot it and start the darken instead of rendering anew. The
        // outermost ring shows this frame; the active()-block above paces the rest.
        if (screen_changed) {
            transition.begin(frame);
            if (!present_3d_wipe()) {
                transition.render(frame);
                present_frame();
            }
            wait_next_tick(period_full);
            darken_hold = 1;  // ring 1 shown once this frame
            continue;
        }

        if (app.screen() == Screen::splash) {
            // Startup splash (FUN_1000_2fac): BUMPRESE.VEC drawn once before the menu.
            if (is_screen_image(splash_screen)) {
                apply_screen_image_palette(splash_screen, frame);
                draw_screen_image(splash_screen, frame);
            }
        } else if (app.screen() == Screen::menu) {
            menu_renderer.render(app.menu().view(), frame);
            draw_tab_hint(font, frame);  // port hint: press Tab for the settings overlay
        } else if (app.screen() == Screen::map) {
            render_map(world.backdrop(), app.world_map().view(), sprite_bank, frame,
                       app.cleared_boards());
            draw_lives(sprite_bank, app.lives(), frame);  // lives row HUD (FUN_1000_6130)
            draw_score(font, app.score(), kMapScoreX, kMapScoreBaselineY, kScoreColor, frame);
        } else if (app.screen() == Screen::outro) {
            // The DESSFIN.VEC ending screen (FUN_1000_3ed4): a full-screen image drawn from
            // its own embedded palette. The screen-change darken above already wiped in the
            // outgoing board (the original's FUN_1000_3467 call inside 3ed4).
            if (is_screen_image(outro_screen)) {
                apply_screen_image_palette(outro_screen, frame);
                draw_screen_image(outro_screen, frame);
            }
        } else if (app.screen() == Screen::game_over) {
            // FUN_1000_11eb: SCORE.VEC + "GAME OVER". The level->game_over darken already
            // wiped in via the screen-change transition above.
            render_game_over(score_screen, sprite_bank, frame);
        } else if (app.screen() == Screen::password_display) {
            // FUN_1000_0d9d: between-world password display on black.
            render_password_display(score_screen, sprite_bank,
                                    password_code_for_world(app.password_display_world()), frame);
        } else if (app.screen() == Screen::high_scores) {
            // FUN_1000_5681/57e1: the high-score table (+ blinking caret during name entry).
            render_high_scores(score_screen, app.high_scores(), sprite_bank,
                               app.high_score_screen().view(), frame);
        } else if (app.screen() == Screen::password) {
            // FUN_1000_0f7a: the code-entry screen over the SCORE.VEC backdrop.
            render_password(score_screen, sprite_bank, app.password_screen().view(), frame);
        } else {
            render_level();
        }

        if (app.screen() != Screen::level || !present_3d_level()) {
            present_frame();
        }

        // Top up the audio queue before yielding: the wait below hands the thread back
        // to the browser for most of a tick, during which nothing else can feed it.
        if (audio_pump_) {
            audio_pump_->pump();
        }

        // Pick this frame's period from the live phase. An in-level game tick is paced by
        // the difficulty mask (FUN_1000_1349): EASY = 2 retraces (35.043 Hz, the historical
        // pace), HARD = 1 (70.086 Hz), MEDIUM alternates. Non-ticking level frames (the
        // board-start hang) and the world-map cloud-jump use the fixed half rate; the menu
        // and world-map navigation/slide use the full retrace rate.
        if (level_ticked) {
            wait_next_tick(level_period);
        } else {
            const bool half_rate = app.screen() == Screen::level ||
                                   (app.screen() == Screen::map && app.world_map().is_jumping());
            wait_next_tick(half_rate ? period_half : period_full);
        }
    }
    return 0;
}

}  // namespace bumpy
