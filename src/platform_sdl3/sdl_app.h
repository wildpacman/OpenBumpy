#pragma once

#include <SDL3/SDL.h>

#include "audio/audio_engine.h"
#include "core/indexed_framebuffer.h"
#include "core/port_config.h"
#include "game/app.h"
#include "platform_gl3/gl_presenter.h"
#include "platform_sdl3/sdl_audio.h"
#include "resources/font.h"
#include "resources/world_resources.h"
#include "video/menu_renderer.h"
#include "video/settings_renderer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace bumpy {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();
    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;

    // True when the GL 3.3 presenter is live (constructor succeeded); false means the
    // SDL_Renderer fallback is in use (flat presentation only, no 3D mode). Consumed by
    // later tasks that gate 3D-mode input/UI on GL availability.
    [[nodiscard]] bool gl_available() const noexcept { return gl_ != nullptr; }

    // Optional audio pump, driven once per loop iteration. Null means desktop (SDL's
    // audio thread pulls instead) or a failed device open (the game runs muted).
    void set_audio_pump(SdlAudio* audio) noexcept { audio_pump_ = audio; }

    // Drive the top-level App. Owns the current world's resources (`world`, by value) and
    // reloads them from `asset_root` whenever App requests a new world (pending_world).
    // sprite_bank is the whole BUMSPJEU.BIN; font is DDFNT2.CAR; splash_screen is the decoded
    // BUMPRESE.VEC startup image; outro_screen is the decoded DESSFIN.VEC ending image;
    // score_screen is the raw SCORE.VEC high-score/GAME-OVER backdrop (all world-independent,
    // must outlive run()). `audio` drives the looping intro-music player (FUN_1000_30dd):
    // started when Screen::splash is entered (including the initial frame), stopped when it
    // is left; menu/gameplay stay silent, matching the original. Must outlive run().
    // config/config_path seed the persisted presentation state (square pixels, fullscreen,
    // 3D mode) and are best-effort re-saved to config_path whenever Alt+Enter/Alt+A/Alt+3
    // change it, so the choice survives to the next launch.
    int run(App& app, const MenuRenderer& menu_renderer,
            const SettingsRenderer& settings_renderer,
            const std::filesystem::path& asset_root,
            WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
            std::span<const std::uint8_t> splash_screen, std::span<const std::uint8_t> outro_screen,
            std::span<const std::uint8_t> score_screen,
            IndexedFramebuffer& frame, AudioEngine& audio, PortConfig config,
            std::filesystem::path config_path);

private:
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    // Declared AFTER window_ so member-destruction order (reverse of declaration) tears
    // gl_ down BEFORE window_ is destroyed -- the GL context it owns must go while the
    // window that hosts it still exists.
    std::unique_ptr<GlPresenter> gl_;
    SdlAudio* audio_pump_{};
};

}  // namespace bumpy
