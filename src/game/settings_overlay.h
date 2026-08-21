#pragma once

#include "game/menu.h"  // MenuInput

namespace bumpy {

enum class SettingsPage { root, video, audio, passwords };

enum class SettingsEvent {
    none,
    toggle_3d,
    toggle_aspect,
    toggle_fullscreen,
    toggle_music,
    toggle_sfx,
    quit,
    close,
};

// Selectable-row counts per page (passwords is read-only). Shared with the renderer.
inline constexpr int kRootRowCount = 4;   // VIDEO, AUDIO, PASSWORDS, QUIT
#ifdef __EMSCRIPTEN__
inline constexpr int kVideoRowCount = 2;  // 3D, FULLSCREEN (the web build is 4:3 only)
#else
inline constexpr int kVideoRowCount = 3;  // 3D, ASPECT, FULLSCREEN
#endif
inline constexpr int kAudioRowCount = 2;  // MUSIC, SOUND

// Per-frame snapshot the shell assembles from the overlay's nav state plus the live
// PortConfig/GL values, consumed by SettingsRenderer. The overlay stores no values.
struct SettingsView {
    SettingsPage page{SettingsPage::root};
    int cursor_row{};
    bool render3d{};
    bool square_pixels{};
    bool fullscreen{};
    bool music{};
    bool sfx{};
    bool render3d_available{};  // false -> the 3D row cannot be toggled
};

// SDL-independent navigation model for the Tab settings overlay. Owns only the current
// page + cursor (+ a press-debounce like Menu); update() emits events instead of
// mutating settings, so the shell's PortConfig stays the single source of truth.
class SettingsOverlay {
public:
    SettingsEvent update(const MenuInput& input, bool render3d_available) noexcept;
    void reset() noexcept;  // page=root, cursor=0 (called when Tab opens the overlay)

    [[nodiscard]] SettingsPage page() const noexcept { return page_; }
    [[nodiscard]] int cursor_row() const noexcept { return cursor_row_; }

private:
    [[nodiscard]] int row_count() const noexcept;

    SettingsPage page_{SettingsPage::root};
    int cursor_row_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
