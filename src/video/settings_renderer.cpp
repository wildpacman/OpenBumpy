#include "video/settings_renderer.h"

#include "game/password_screen.h"       // password_code_for_world
#include "resources/sprite_frame.h"     // decode_sprite_frame, sprite_transparent_index
#include "video/high_score_renderer.h"  // draw_glyph_string
#include "video/menu_renderer.h"        // MenuImage
#include "video/screen_image.h"         // is_screen_image, apply_screen_image_palette

#include <array>
#include <cstddef>
#include <exception>
#include <string>  // std::char_traits

namespace bumpy {
namespace {

constexpr int kGlyphStepX = 16;
constexpr int kTitleY = 16;
constexpr int kRowY0 = 64;
constexpr int kRowStepY = 24;
// Horizontal budget on the 320px page: the longest label "FULLSCREEN" (10 glyph
// cells = 160px) plus the longest value "16.10" (5 cells = 80px) must both fit with
// a gap between them and a margin at the right edge. cursor 16..~36, labels 48..208,
// values left-aligned at 224 (16px gap after FULLSCREEN); "16.10" ends at 304, a 16px
// right margin -- so no value ever touches the 320px edge (the earlier 240 put "16.10"
// flush against it).
constexpr int kCursorX = 16;
constexpr int kLabelX = 48;
constexpr int kValueX = 224;
// Passwords page (two columns of four).
constexpr int kPwRowY0 = 56;
constexpr int kPwRowStep = 24;
constexpr int kPwLeftX = 8;
constexpr int kPwRightX = 168;

// Blit one decoded sprite frame with its top-left at (x, y); the shared decoder
// normalizes transparency to sprite_transparent_index, which is skipped.
void blit_frame(std::span<const std::uint8_t> bank, int frame_index, int x, int y,
                IndexedFramebuffer& target) {
    try {
        const MenuImage g = decode_sprite_frame(bank, frame_index);
        for (int py = 0; py < g.height; ++py) {
            const int ty = y + py;
            if (ty < 0 || ty >= target.height()) continue;
            for (int px = 0; px < g.width; ++px) {
                const int tx = x + px;
                if (tx < 0 || tx >= target.width()) continue;
                const auto c = g.pixels[static_cast<std::size_t>(py) * g.width + px];
                if (c != sprite_transparent_index) target.pixel(tx, ty) = c;
            }
        }
    } catch (const std::exception&) {
        // an undecodable frame is skipped
    }
}

}  // namespace

SettingsRenderer::SettingsRenderer(std::span<const std::uint8_t> score_vec,
                                   std::span<const std::uint8_t> sprite_bank,
                                   std::span<const std::uint8_t> cursor_sprite)
    : score_vec_(score_vec), sprite_bank_(sprite_bank), cursor_sprite_(cursor_sprite) {}

void SettingsRenderer::render(const SettingsView& view, IndexedFramebuffer& target) const {
    if (is_screen_image(score_vec_)) {
        apply_screen_image_palette(score_vec_, target);
    }
    target.clear(0);

    auto title = [&](const char* t) {
        const auto len = std::char_traits<char>::length(t);
        const int tx = (target.width() - static_cast<int>(len) * kGlyphStepX) / 2;
        draw_glyph_string(t, len, tx, kTitleY, sprite_bank_, target);
    };
    auto row = [&](int i, const char* label, const char* value) {
        const int y = kRowY0 + i * kRowStepY;
        draw_glyph_string(label, std::char_traits<char>::length(label), kLabelX, y,
                          sprite_bank_, target);
        if (value) {
            draw_editor_glyphs(value, std::char_traits<char>::length(value), kValueX, y,
                               sprite_bank_, target);
        }
    };

    switch (view.page) {
    case SettingsPage::root:
        title("OPTIONS");
        row(0, "VIDEO", nullptr);
        row(1, "AUDIO", nullptr);
        row(2, "PASSWORDS", nullptr);
        row(3, "QUIT", nullptr);
        break;
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
    case SettingsPage::audio:
        title("AUDIO");
        row(0, "MUSIC", view.music ? "ON" : "OFF");
        row(1, "SOUND", view.sfx ? "ON" : "OFF");
        break;
    case SettingsPage::passwords:
        title("PASSWORDS");
        for (int i = 0; i < 8; ++i) {
            const int world = 2 + i;
            const std::array<char, 6> code = password_code_for_world(world);
            char buf[8];
            buf[0] = static_cast<char>('0' + world);
            buf[1] = ' ';
            for (int k = 0; k < 6; ++k) buf[2 + k] = code[static_cast<std::size_t>(k)];
            const int x = (i / 4 == 0) ? kPwLeftX : kPwRightX;
            const int y = kPwRowY0 + (i % 4) * kPwRowStep;
            draw_glyph_string(buf, 8, x, y, sprite_bank_, target);
        }
        break;
    }

    if (view.page == SettingsPage::root || view.page == SettingsPage::video ||
        view.page == SettingsPage::audio) {
        blit_frame(cursor_sprite_, 0, kCursorX, kRowY0 + view.cursor_row * kRowStepY, target);
    }
}

}  // namespace bumpy
