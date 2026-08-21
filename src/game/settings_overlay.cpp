#include "game/settings_overlay.h"

namespace bumpy {

int SettingsOverlay::row_count() const noexcept {
    switch (page_) {
    case SettingsPage::root:
        return kRootRowCount;
    case SettingsPage::video:
        return kVideoRowCount;
    case SettingsPage::audio:
        return kAudioRowCount;
    case SettingsPage::passwords:
        return 0;  // read-only: no selectable rows
    }
    return 0;
}

void SettingsOverlay::reset() noexcept {
    page_ = SettingsPage::root;
    cursor_row_ = 0;
    waiting_for_release_ = false;
}

SettingsEvent SettingsOverlay::update(const MenuInput& input, bool render3d_available) noexcept {
    // Press-debounce: one action per key press (matches Menu). A frame with no
    // navigation key clears the latch.
    const bool any = input.up || input.down || input.left || input.right ||
                     input.confirm || input.cancel;
    if (!any) {
        waiting_for_release_ = false;
        return SettingsEvent::none;
    }
    if (waiting_for_release_) {
        return SettingsEvent::none;
    }
    waiting_for_release_ = true;

    const int rows = row_count();
    if (input.up && rows > 0) {
        cursor_row_ = (cursor_row_ + rows - 1) % rows;
        return SettingsEvent::none;
    }
    if (input.down && rows > 0) {
        cursor_row_ = (cursor_row_ + 1) % rows;
        return SettingsEvent::none;
    }

    // Back-out: cancel (Esc) or left. Sub-page -> root; root -> close.
    if (input.cancel || input.left) {
        if (page_ == SettingsPage::root) {
            return SettingsEvent::close;
        }
        page_ = SettingsPage::root;
        cursor_row_ = 0;
        return SettingsEvent::none;
    }

    // Activate: confirm (Enter/Space) or right.
    if (input.confirm || input.right) {
        switch (page_) {
        case SettingsPage::root:
            switch (cursor_row_) {
            case 0: page_ = SettingsPage::video; cursor_row_ = 0; return SettingsEvent::none;
            case 1: page_ = SettingsPage::audio; cursor_row_ = 0; return SettingsEvent::none;
            case 2: page_ = SettingsPage::passwords; cursor_row_ = 0; return SettingsEvent::none;
            case 3: return SettingsEvent::quit;
            }
            return SettingsEvent::none;
        case SettingsPage::video:
            switch (cursor_row_) {
#ifdef __EMSCRIPTEN__
            case 0: return SettingsEvent::toggle_fullscreen;
#else
            case 0: return render3d_available ? SettingsEvent::toggle_3d : SettingsEvent::none;
            case 1: return SettingsEvent::toggle_aspect;
            case 2: return SettingsEvent::toggle_fullscreen;
#endif
            }
            return SettingsEvent::none;
        case SettingsPage::audio:
            switch (cursor_row_) {
            case 0: return SettingsEvent::toggle_music;
            case 1: return SettingsEvent::toggle_sfx;
            }
            return SettingsEvent::none;
        case SettingsPage::passwords:
            return SettingsEvent::none;
        }
    }
    return SettingsEvent::none;
}

}  // namespace bumpy
