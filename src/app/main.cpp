#include <SDL3/SDL_main.h>

#include "audio/audio_engine.h"
#include "audio/midi_opl_player.h"
#include "audio/speaker_sfx.h"
#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "core/port_config.h"
#include "game/app.h"
#include "game/high_score_screen.h"
#include "game/high_scores.h"
#include "game/level_game.h"
#include "game/menu.h"
#include "game/password_screen.h"
#include "platform_sdl3/sdl_app.h"
#include "platform_sdl3/sdl_audio.h"
#include "resources/adlib_bank.h"
#include "resources/binary_reader.h"
#include "resources/font.h"
#include "resources/midi_song.h"
#include "resources/level_resources.h"
#include "resources/sprite_frame.h"
#include "resources/sfx_tables.h"
#include "resources/menu_resources.h"
#include "resources/vec.h"
#include "resources/world_resources.h"
#include "game/world_graphs.h"
#include "game/world_map.h"
// Stage 1 of the web port compiles no GL, so the offline GL dev tools below
// (--present-parity, --render-3d) are excluded from the web build with them.
#ifndef __EMSCRIPTEN__
#include "platform_gl3/gl_presenter.h"
#include "platform_gl3/gl_util.h"
#include "platform_gl3/scene_renderer.h"
#endif
#include "video/board_renderer.h"
#include "video/high_score_renderer.h"
#include "video/hud.h"
#include "video/map_renderer.h"
#include "video/menu_renderer.h"
#include "video/password_renderer.h"
#include "video/screen_image.h"
#include "video/settings_renderer.h"
#include "video/screen_transition.h"
#include "video3d/scene3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

// Quitting -- the Tab overlay's QUIT row, or Escape out of the menu -- ends the run loop
// and returns from main(). EXIT_RUNTIME defaults to 0, so the page stays alive while the
// canvas goes dead and the click-to-play gate is already hidden: the player is left with a
// frozen picture, no message, and no way back short of reloading by hand. Hand the shell
// back its gate as a sign-off instead. Only notifies; the wording and the reload live in
// src/web/shell.html, which is where the page's DOM is described.
EM_JS(void, bumpy_web_game_exited, (), {
    if (Module.onGameExit) {
        Module.onGameExit();
    }
});
#endif

namespace {

#ifdef __EMSCRIPTEN__

// The web build's assets are baked into the .data image and mounted at /assets by
// --preload-file, so there is nothing to search for and nothing that can drift.
std::filesystem::path find_asset_root(std::string_view) {
    return "/assets";
}

// The browser has no directory next to an executable; PortConfig persists to
// localStorage instead (see src/core/port_config.cpp), so this path is never opened.
std::filesystem::path config_file_path(std::string_view) {
    return "/bumpy_port.cfg";
}

// Nothing to verify: the assets ship inside the build, so they cannot differ from
// the manifest. The source tree they are staged from is verified on the desktop
// side by tests/cpp/asset_manifest_test.cpp.
void warn_if_assets_changed(const std::filesystem::path&) {}

#else

bool has_asset_manifest(const std::filesystem::path& root) {
    return std::filesystem::is_regular_file(root / "config/original-assets.sha256");
}

void add_root_candidates(std::vector<std::filesystem::path>& candidates, std::filesystem::path path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error) {
        path = std::filesystem::absolute(path, error);
    }
    if (error) {
        return;
    }
    if (std::filesystem::is_regular_file(path, error)) {
        path = path.parent_path();
    }
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        if (std::find(candidates.begin(), candidates.end(), current) == candidates.end()) {
            candidates.push_back(current);
        }
        if (current == current.root_path()) {
            break;
        }
    }
}

std::filesystem::path find_asset_root(std::string_view executable_path) {
    std::vector<std::filesystem::path> candidates;
    add_root_candidates(candidates, std::filesystem::current_path());
    add_root_candidates(candidates, std::filesystem::path(executable_path));
    for (const auto& candidate : candidates) {
        if (has_asset_manifest(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(
        "cannot find original Bumpy assets; run from the project root or keep the executable below it");
}

// The config sits next to the exe ("the port's first on-disk persistence"); if the
// exe path is unusable, fall back to the current directory.
std::filesystem::path config_file_path(std::string_view executable_path) {
    std::error_code error;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(executable_path), error);
    if (!error && p.has_parent_path()) {
        return p.parent_path() / "bumpy_port.cfg";
    }
    return std::filesystem::current_path() / "bumpy_port.cfg";
}

// A faithful port reads the original files, so it is worth warning when they are
// missing or altered -- but it must not refuse to launch over a hash mismatch.
void warn_if_assets_changed(const std::filesystem::path& asset_root) {
    try {
        const auto verification =
            bumpy::AssetManifest::load(asset_root / "config/original-assets.sha256").verify(asset_root);
        if (!verification.missing.empty() || !verification.changed.empty()) {
            std::cerr << "warning: some original Bumpy assets are missing or changed\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "warning: could not verify original assets: " << error.what() << '\n';
    }
}

#endif  // __EMSCRIPTEN__

void write_u16(std::ostream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void write_u32(std::ostream& output, std::uint32_t value) {
    write_u16(output, static_cast<std::uint16_t>(value & 0xffffU));
    write_u16(output, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

// The 54-byte BMP file+info header for a 24-bit image (shared by the indexed and
// RGBA dump writers so the two never drift).
void write_bmp_header(std::ostream& output, std::uint32_t width, std::uint32_t height,
                      std::uint32_t pixel_bytes) {
    output.write("BM", 2);
    write_u32(output, 14U + 40U + pixel_bytes);
    write_u16(output, 0);
    write_u16(output, 0);
    write_u32(output, 54);
    write_u32(output, 40);
    write_u32(output, width);
    write_u32(output, height);
    write_u16(output, 1);
    write_u16(output, 24);
    write_u32(output, 0);
    write_u32(output, pixel_bytes);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);
}

// Minimal 24-bit BMP writer for headless inspection of a rendered frame.
void write_24bit_bmp(const std::filesystem::path& path, const bumpy::IndexedFramebuffer& frame) {
    const auto width = static_cast<std::uint32_t>(frame.width());
    const auto height = static_cast<std::uint32_t>(frame.height());
    const auto row_stride = ((width * 3U) + 3U) & ~3U;
    const auto pixel_bytes = row_stride * height;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create BMP: " + path.string());
    }
    write_bmp_header(output, width, height, pixel_bytes);

    std::vector<std::uint8_t> row(row_stride);
    const auto pixels = frame.pixels();
    const auto& palette = frame.palette();
    for (int y = frame.height() - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < frame.width(); ++x) {
            const auto color = palette[pixels[static_cast<std::size_t>(y * frame.width() + x)]];
            const auto offset = static_cast<std::size_t>(x * 3);
            row[offset] = color.b;
            row[offset + 1] = color.g;
            row[offset + 2] = color.r;
        }
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

// Minimal mono 16-bit PCM WAV writer for headless audio inspection.
void write_wav_mono16(const std::filesystem::path& path, std::uint32_t sample_rate,
                      const std::vector<std::int16_t>& samples) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create WAV: " + path.string());
    }
    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t byte_rate = sample_rate * 2U;  // mono, 16-bit
    output.write("RIFF", 4);
    write_u32(output, 36U + data_bytes);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    write_u32(output, 16);  // fmt chunk size (PCM)
    write_u16(output, 1);   // format tag: PCM
    write_u16(output, 1);   // channels: mono
    write_u32(output, sample_rate);
    write_u32(output, byte_rate);
    write_u16(output, 2);   // block align (1 channel * 16 bits / 8)
    write_u16(output, 16);  // bits per sample
    output.write("data", 4);
    write_u32(output, data_bytes);
    output.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(data_bytes));
}

// Offline-renders BUMPY.MID (via BUMPY.BNK on the MidiOplPlayer) to a WAV for by-ear
// fidelity checking, with no SDL/audio device involved.
int dump_music_to_wav(const std::filesystem::path& mid_path, const std::filesystem::path& bnk_path,
                      const std::filesystem::path& out_path, double seconds) {
    const auto song = bumpy::MidiSong::load(mid_path);
    const auto bank = bumpy::AdLibBank::load(bnk_path);
    bumpy::MidiOplPlayer player(song, bank, /*loop=*/false);
    const std::uint32_t rate = player.sample_rate();
    const auto frames = static_cast<std::size_t>(seconds * rate);

    std::vector<float> buf(frames);
    player.render(buf.data(), buf.size());

    std::vector<std::int16_t> pcm(frames);
    double energy = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = std::clamp(buf[i], -1.0f, 1.0f);
        pcm[i] = static_cast<std::int16_t>(std::lround(s * 32767.0f));
        energy += double(buf[i]) * buf[i];
    }
    write_wav_mono16(out_path, rate, pcm);
    const double rms = frames > 0 ? std::sqrt(energy / static_cast<double>(frames)) : 0.0;
    std::cout << "wrote " << out_path.string() << " (" << frames << " frames @ " << rate
              << " Hz, rms=" << rms << ")\n";
    return 0;
}

// Offline-renders a single PC-speaker sweep-engine SFX preset (bumpy::kSfxPresets)
// via SpeakerVoice to WAV, with no SDL/audio device involved -- for by-ear fidelity
// checking of the sweep engine's pitch-contour/duration against a DOSBox-X capture
// (audio design spec Task 6, Step 6: kSfxIsrBaseHz is a tuning constant).
int dump_sfx_to_wav(int preset_id, const std::filesystem::path& out_path) {
    constexpr int kPresetCount = sizeof(bumpy::kSfxPresets) / sizeof(bumpy::kSfxPresets[0]);
    if (preset_id < 0 || preset_id >= kPresetCount) {
        throw std::runtime_error("preset id out of range 0.." + std::to_string(kPresetCount - 1));
    }
    const bumpy::SfxPreset& preset = bumpy::kSfxPresets[preset_id];
    if (!preset.used) {
        std::cout << "warning: preset " << preset_id << " is marked unused\n";
    }

    bumpy::SpeakerVoice voice;
    voice.start(preset);
    const std::uint32_t rate = bumpy::SpeakerVoice::kSampleRate;
    // 1 second covers most recovered sweep presets, but the long ones (steps=400:
    // 0x03/0x0f) run ~10s at the current placeholder kSfxIsrBaseHz (see speaker_sfx.h)
    // and are truncated by this dump.
    const auto frames = static_cast<std::size_t>(rate);

    std::vector<float> buf(frames, 0.0f);
    voice.render_add(buf.data(), buf.size());

    std::vector<std::int16_t> pcm(frames);
    double energy = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = std::clamp(buf[i], -1.0f, 1.0f);
        pcm[i] = static_cast<std::int16_t>(std::lround(s * 32767.0f));
        energy += double(buf[i]) * buf[i];
    }
    write_wav_mono16(out_path, rate, pcm);
    const double rms = frames > 0 ? std::sqrt(energy / static_cast<double>(frames)) : 0.0;
    std::cout << "wrote " << out_path.string() << " (preset 0x" << std::hex << preset_id
              << std::dec << ", " << frames << " frames @ " << rate << " Hz, rms=" << rms
              << ", finished=" << (!voice.active()) << ")\n";
    return 0;
}

bumpy::IndexedFramebuffer render_menu_frame(const std::filesystem::path& asset_root, int level_value) {
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);
    bumpy::Menu menu;
    bumpy::MenuView view = menu.view();
    view.level_value = level_value;  // 0/1/2 = EASY/MEDIUM/HARD indicator
    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);
    // The port's TAB->OPTIONS hint, so --render-title previews the title exactly as the
    // shell composes it (the shell draws the same hint on Screen::menu).
    const auto font = bumpy::Font::load(asset_root / "DDFNT2.CAR");
    bumpy::draw_tab_hint(font, frame);
    return frame;
}

#ifndef __EMSCRIPTEN__
// CPU nearest-neighbour reference upscale: each frame pixel becomes a k x k block.
std::vector<std::uint8_t> nearest_upscale_rgba(const bumpy::IndexedFramebuffer& frame, int k) {
    const auto src = frame.to_rgba();
    const int w = frame.width();
    const int h = frame.height();
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * k * h * k * 4);
    for (int y = 0; y < h * k; ++y) {
        for (int x = 0; x < w * k; ++x) {
            const std::uint32_t p = src[static_cast<std::size_t>(y / k) * w + (x / k)];
            const std::size_t o = (static_cast<std::size_t>(y) * w * k + x) * 4;
            out[o] = static_cast<std::uint8_t>(p & 0xff);
            out[o + 1] = static_cast<std::uint8_t>((p >> 8) & 0xff);
            out[o + 2] = static_cast<std::uint8_t>((p >> 16) & 0xff);
            out[o + 3] = static_cast<std::uint8_t>((p >> 24) & 0xff);
        }
    }
    return out;
}

// Renders three representative composed screens through the GL flat path at
// integer scales and compares them byte-for-byte with the CPU nearest reference.
// This is the spec's flat-path parity gate: interior pixels must be 1:1.
int present_parity(const std::filesystem::path& asset_root) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window =
        SDL_CreateWindow("parity", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    int failures = 0;
    {
        bumpy::GlPresenter presenter(window);

        std::vector<std::pair<std::string, bumpy::IndexedFramebuffer>> cases;
        cases.emplace_back("menu", render_menu_frame(asset_root, 0));

        const auto level = bumpy::LevelResources::load(asset_root, 1);
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        bumpy::IndexedFramebuffer board(320, 200);
        bumpy::render_board(level, 0, {}, board);
        bumpy::draw_bum_entities(level.bum_entities(0), bank.bytes(), board);
        cases.emplace_back("board", std::move(board));

        auto world = bumpy::WorldResources::load(asset_root, 1);
        bumpy::WorldMap map(1);
        bumpy::IndexedFramebuffer mapframe(320, 200);
        bumpy::render_map(world.backdrop(), map.view(), bank.bytes(), mapframe);
        cases.emplace_back("map", std::move(mapframe));

        for (const auto& [name, frame] : cases) {
            for (const int k : {2, 4}) {
                const auto got = presenter.render_flat_offscreen(frame, 320 * k, 200 * k, 200);
                const auto want = nearest_upscale_rgba(frame, k);
                const bool pass = got == want;
                std::cout << name << " x" << k << ": " << (pass ? "PASS" : "FAIL") << '\n';
                if (!pass) {
                    ++failures;
                }
            }
        }
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
#endif  // !__EMSCRIPTEN__

int render_title_to_bmp(const std::filesystem::path& asset_root, const std::filesystem::path& out_path,
                        int level_value) {
    write_24bit_bmp(out_path, render_menu_frame(asset_root, level_value));
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump the raw decoded TITRE.VEC bytes for offline format analysis.
int dump_title_raw(const std::filesystem::path& asset_root, const std::filesystem::path& out_path) {
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const auto bytes = resources.title.decoded_bytes();
    std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create dump: " + out_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path.string() << '\n';
    return 0;
}

// Decode any layered-VEC file (TITRE.VEC, MONDE?.VEC, and the level blobs
// D?.PAV/D?.DEC/D?.BUM, which share the same container) and dump the decoded
// bytes for offline format analysis. Prints the layer chain that produced them.
int decode_vec_file(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto bytes = resource.decoded_bytes();
    std::cout << in_path.string() << ": " << resource.layers().size() << " layer(s)\n";
    for (std::size_t i = 0; i < resource.layers().size(); ++i) {
        const auto& layer = resource.layers()[i];
        std::cout << "  layer " << i << ": method " << static_cast<int>(layer.method)
                  << (layer.final_layer ? " (final)" : "") << ", input " << layer.input_size
                  << " -> output " << layer.output_size << '\n';
    }
    std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create dump: " + out_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path.string() << '\n';
    return 0;
}

// Render a decoded 320x200 screen-format VEC (TITRE/MONDE?/BUMPRESE/DESSFIN: a
// 99-byte header with a 16-colour palette at offset 51, then four 8000-byte
// plane-sequential bit-planes) to a BMP, for visual format confirmation.
int render_screen_vec(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto decoded = resource.decoded_bytes();
    constexpr std::size_t header = 99;
    constexpr std::size_t plane = 320 * 200 / 8;
    if (decoded.size() < header + 4 * plane) {
        throw std::runtime_error("decoded VEC is not a 320x200 screen: " + in_path.string());
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto* pal = decoded.data() + (header - 48);
    for (int c = 0; c < 16; ++c) {
        const auto* e = pal + c * 3;
        frame.set_palette(static_cast<std::uint8_t>(c),
                          bumpy::Rgba{bumpy::vga_dac_to_rgba_component(e[0]),
                                      bumpy::vga_dac_to_rgba_component(e[1]),
                                      bumpy::vga_dac_to_rgba_component(e[2]), 0xff});
    }
    const auto* planes = decoded.data() + header;
    for (std::size_t p = 0; p < static_cast<std::size_t>(320) * 200; ++p) {
        const std::size_t byte = p >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(p & 7U);
        std::uint8_t v = 0;
        for (int pl = 0; pl < 4; ++pl) {
            v = static_cast<std::uint8_t>(v | (((planes[pl * plane + byte] >> shift) & 1U) << pl));
        }
        frame.pixel(static_cast<int>(p % 320), static_cast<int>(p / 320)) = v;
    }
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Probe the pixel geometry of a decoded D?.PAV (or any raw blob) by rendering it
// under a selectable layout and dimensions. layout: planeseq | rowint | lin4 |
// lin8. Palette is "DEBUG" (16 high-contrast colours) or a screen-format VEC.
int render_pav(const std::filesystem::path& pav_path, const std::string& pal_path,
               const std::filesystem::path& out_path, const std::string& layout, int w, int h,
               int hdr) {
    const auto pav_resource = bumpy::decode_vec_resource(pav_path);
    const auto pav = pav_resource.decoded_bytes();
    std::vector<std::uint8_t> pal_screen;
    if (pal_path != "DEBUG") {
        const auto pal_resource = bumpy::decode_vec_resource(pal_path);
        const auto decoded = pal_resource.decoded_bytes();
        pal_screen.assign(decoded.begin(), decoded.end());
    }
    bumpy::IndexedFramebuffer frame(w, h);
    if (pal_path == "DEBUG") {
        static const bumpy::Rgba dbg[16] = {
            {0, 0, 0, 255},      {255, 0, 0, 255},    {0, 255, 0, 255},   {255, 255, 0, 255},
            {0, 0, 255, 255},    {255, 0, 255, 255},  {0, 255, 255, 255}, {255, 255, 255, 255},
            {128, 128, 128, 255},{128, 0, 0, 255},    {0, 128, 0, 255},   {128, 128, 0, 255},
            {0, 0, 128, 255},    {128, 0, 128, 255},  {0, 128, 128, 255}, {200, 200, 200, 255}};
        for (int c = 0; c < 16; ++c) {
            frame.set_palette(static_cast<std::uint8_t>(c), dbg[c]);
        }
    } else {
        const auto* pal = pal_screen.data() + (99 - 48);
        for (int c = 0; c < 16; ++c) {
            const auto* e = pal + c * 3;
            frame.set_palette(static_cast<std::uint8_t>(c),
                              bumpy::Rgba{bumpy::vga_dac_to_rgba_component(e[0]),
                                          bumpy::vga_dac_to_rgba_component(e[1]),
                                          bumpy::vga_dac_to_rgba_component(e[2]), 0xff});
        }
    }
    const auto* d = pav.data() + hdr;
    const std::size_t avail = pav.size() - hdr;
    auto get = [&](std::size_t i) -> std::uint8_t { return i < avail ? d[i] : 0; };
    const std::size_t plane_bytes = static_cast<std::size_t>(w) / 8;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t v = 0;
            if (layout == "planeseq") {
                const std::size_t plane = static_cast<std::size_t>(w) * h / 8;
                const std::size_t bit = static_cast<std::size_t>(y) * w + x;
                const unsigned sh = 7U - static_cast<unsigned>(bit & 7U);
                for (int pl = 0; pl < 4; ++pl)
                    v |= ((get(pl * plane + (bit >> 3)) >> sh) & 1U) << pl;
            } else if (layout == "rowint") {
                const std::size_t row = static_cast<std::size_t>(y) * plane_bytes * 4;
                const unsigned sh = 7U - static_cast<unsigned>(x & 7U);
                for (int pl = 0; pl < 4; ++pl)
                    v |= ((get(row + pl * plane_bytes + (x >> 3)) >> sh) & 1U) << pl;
            } else if (layout == "lin4") {
                const std::size_t idx = (static_cast<std::size_t>(y) * w + x) >> 1;
                const std::uint8_t b = get(idx);
                v = (x & 1) ? (b & 0x0f) : (b >> 4);
            } else {  // lin8
                v = get(static_cast<std::size_t>(y) * w + x) & 0x0f;
            }
            frame.pixel(x, y) = v;
        }
    }
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (" << layout << " " << w << "x" << h << ")\n";
    return 0;
}

// Render the post-world-9 ending screen (DESSFIN.VEC, drawn by FUN_1000_3ed4) to a BMP,
// through the exact shared screen_image helpers the SDL shell uses for Screen::outro, for
// by-eye confirmation of the outro render path.
int render_outro_to_bmp(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto screen = resource.decoded_bytes();
    if (!bumpy::is_screen_image(screen)) {
        throw std::runtime_error("decoded VEC is not a 320x200 screen: " + in_path.string());
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::apply_screen_image_palette(screen, frame);
    bumpy::draw_screen_image(screen, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump the HIGH-SCORE table (FUN_1000_5681/57e1) through the shared renderer for by-eye
// checking. With an optional `insert_score`, insert it and show the name-entry caret.
int render_highscores_to_bmp(const std::filesystem::path& asset_root,
                             const std::filesystem::path& score_path,
                             const std::filesystem::path& out_path, long insert_score) {
    const auto score_bytes = bumpy::read_binary_file(score_path);  // raw screen image
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::HighScoreTable table;
    bumpy::HighScoreScreenView view{};  // view mode
    if (insert_score > 0) {
        view.mode = bumpy::HighScoreMode::entry;
        view.insert_row = table.insert(static_cast<std::uint32_t>(insert_score));
        view.cursor_col = 0;
        view.caret_visible = true;
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_high_scores(score_bytes, table, bank.bytes(), view, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump the GAME OVER screen (FUN_1000_11eb) through the shared renderer for by-eye checking.
int render_gameover_to_bmp(const std::filesystem::path& asset_root,
                           const std::filesystem::path& score_path,
                           const std::filesystem::path& out_path) {
    const auto score_bytes = bumpy::read_binary_file(score_path);  // raw screen image
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_game_over(score_bytes, bank.bytes(), frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump the PASSWORD screen (FUN_1000_0f7a) through the shared renderer for by-eye checking.
// With an optional 6-letter `code`, show it in the entry field and its OK/ERROR result flash;
// otherwise show the default AAAAAA field with the caret.
int render_password_to_bmp(const std::filesystem::path& asset_root,
                           const std::filesystem::path& score_path,
                           const std::filesystem::path& out_path, const std::string& code) {
    const auto score_bytes = bumpy::read_binary_file(score_path);  // raw screen image
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::PasswordScreenView view{};
    if (!code.empty()) {
        for (std::size_t i = 0; i < view.code.size() && i < code.size(); ++i) {
            view.code[i] = code[i];
        }
        view.showing_result = true;
        view.result_ok = bumpy::password_world(view.code) >= 2;
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::render_password(score_bytes, bank.bytes(), view, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump a settings-overlay page through the shared renderer for by-eye checking.
// page = root | video | audio | passwords (default root).
int render_options_to_bmp(const std::filesystem::path& asset_root,
                          const std::filesystem::path& score_path,
                          const std::filesystem::path& out_path, const std::string& page) {
    const auto score_bytes = bumpy::read_binary_file(score_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto fleche = bumpy::read_binary_file(asset_root / "FLECHE.BIN");
    bumpy::SettingsRenderer renderer(score_bytes, bank.bytes(), fleche);

    bumpy::SettingsView view{};
    view.render3d = true;
    view.square_pixels = false;
    view.fullscreen = true;
    view.music = true;
    view.sfx = true;
    view.render3d_available = true;
    view.page = page == "video"       ? bumpy::SettingsPage::video
                : page == "audio"     ? bumpy::SettingsPage::audio
                : page == "passwords" ? bumpy::SettingsPage::passwords
                                      : bumpy::SettingsPage::root;

    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(view, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (" << page << ")\n";
    return 0;
}

// Compose a static playfield board (MONDE backdrop + D?.PAV objects on the 16x16
// grid) and dump it to a BMP for by-eye comparison with the original.
int render_board_to_bmp(const std::filesystem::path& asset_root, int level_number,
                        const std::filesystem::path& monde_path, std::size_t board_index,
                        const std::filesystem::path& out_path, bool draw_map, bool draw_entities,
                        bool draw_sprites) {
    const auto level = bumpy::LevelResources::load(asset_root, level_number);
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats =
        bumpy::render_board(level, board_index, backdrop.decoded_bytes(), frame, draw_map);
    std::cout << "wrote " << out_path.string() << " (level " << level_number << " board "
              << board_index << ": " << stats.objects_drawn << " objects, " << stats.stacked_cells
              << " stacked cells -> " << stats.stacked_tiles << " tiles";
    if (draw_sprites) {
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        const auto ent = bumpy::draw_bum_entities(level.bum_entities(board_index), bank.bytes(), frame);
        std::cout << "; entity sprites: " << ent.layer_a << " A / " << ent.layer_b << " B / "
                  << ent.layer_c << " C (" << ent.skipped << " skipped)";
    } else if (draw_entities) {
        const auto ent = bumpy::overlay_bum_entities(level.bum_entities(board_index), frame);
        std::cout << "; BUM markers: " << ent.layer_a << " A / " << ent.layer_b << " B / "
                  << ent.layer_c << " C";
    }
    write_24bit_bmp(out_path, frame);
    std::cout << ")\n";
    return 0;
}

// --render-sprite <level> <frame> <out.rgba>: decode one BUMSPJEU.BIN frame using
// the level's board-0 in-game DEC palette and dump it as raw RGBA -- a little-endian
// int32 width, int32 height, then width*height RGBA bytes (top-to-bottom), with the
// sprite transparent index emitted as fully transparent (0,0,0,0). Feeds the offline
// exe-icon build (tools/re/build_icon.py); not part of the shipped game.
int render_sprite_to_rgba(const std::filesystem::path& asset_root, int level_number,
                          int frame_index, const std::filesystem::path& out_path) {
    const auto level = bumpy::LevelResources::load(asset_root, level_number);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto dac = level.board(0).palette();
    std::array<bumpy::Rgba, bumpy::LevelBoard::palette_colors> pal{};
    for (int c = 0; c < bumpy::LevelBoard::palette_colors; ++c) {
        const std::uint8_t* e = dac.data() + static_cast<std::size_t>(c) * 3;
        pal[static_cast<std::size_t>(c)] =
            bumpy::Rgba{bumpy::vga_dac_to_rgba_component(e[0]), bumpy::vga_dac_to_rgba_component(e[1]),
                        bumpy::vga_dac_to_rgba_component(e[2]), 0xff};
    }
    const auto sprite = bumpy::decode_sprite_frame(bank.bytes(), frame_index);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(sprite.width) * sprite.height * 4);
    for (std::size_t i = 0; i < sprite.pixels.size(); ++i) {
        const std::uint8_t idx = sprite.pixels[i];
        const bumpy::Rgba c =
            idx == bumpy::sprite_transparent_index ? bumpy::Rgba{0, 0, 0, 0} : pal[idx];
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = c.a;
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot create RGBA dump: " + out_path.string());
    }
    const std::int32_t w = sprite.width;
    const std::int32_t h = sprite.height;
    out.write(reinterpret_cast<const char*>(&w), 4);
    out.write(reinterpret_cast<const char*>(&h), 4);
    out.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    std::cout << "wrote " << out_path.string() << " (" << w << "x" << h << " frame " << frame_index
              << ")\n";
    return 0;
}

#ifndef __EMSCRIPTEN__
// RGBA (rows top-to-bottom) -> 24-bit BMP, for GL readback dumps.
void write_24bit_bmp_rgba(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& rgba, int w, int h) {
    const auto row_stride = ((static_cast<std::uint32_t>(w) * 3U) + 3U) & ~3U;
    const auto pixel_bytes = row_stride * static_cast<std::uint32_t>(h);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create BMP: " + path.string());
    }
    write_bmp_header(output, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                     pixel_bytes);
    std::vector<std::uint8_t> row(row_stride);
    for (int y = h - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < w; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * w + x) * 4;
            row[static_cast<std::size_t>(x) * 3] = rgba[src + 2];
            row[static_cast<std::size_t>(x) * 3 + 1] = rgba[src + 1];
            row[static_cast<std::size_t>(x) * 3 + 2] = rgba[src];
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
}

// Shader dir: next to the exe (installed layout) or the repo checkout.
std::filesystem::path resolve_shader_dir(const std::filesystem::path& asset_root,
                                         std::string_view executable_path) {
    std::error_code error;
    const auto exe =
        std::filesystem::weakly_canonical(std::filesystem::path(executable_path), error);
    if (!error && exe.has_parent_path() &&
        std::filesystem::is_directory(exe.parent_path() / "shaders3d", error)) {
        return exe.parent_path() / "shaders3d";
    }
    return asset_root / "shaders3d";
}

// Offline 3D-diorama frame: board 0-input start state (ball hanging at its entry
// cell), rendered headless at 1280x800 -- the by-eye gate for the diorama look.
int render_3d_to_bmp(const std::filesystem::path& asset_root, std::string_view exe_path,
                     int level_number, const std::filesystem::path& monde_path,
                     std::size_t board_index, const std::filesystem::path& out_path) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window =
        SDL_CreateWindow("render3d", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    int rc = 1;
    if (window) {
        bumpy::GlPresenter presenter(window);
        const auto level = bumpy::LevelResources::load(asset_root, level_number);
        const auto backdrop = bumpy::decode_vec_resource(monde_path);
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        bumpy::SpriteCache sprites(bank.bytes());
        const auto scene = bumpy::build_scene3d(level, board_index, backdrop.decoded_bytes());
        bumpy::SceneRenderer renderer(presenter.gl(),
                                      resolve_shader_dir(asset_root, exe_path));
        renderer.set_scene(scene, sprites);

        bumpy::LevelGame game(level.bum_entities(board_index));
        bumpy::BumEntities live{};
        std::copy(game.grid().begin(), game.grid().begin() + bumpy::BumEntities::record_size,
                  live.bytes.begin());
        std::optional<bumpy::MonsterPose> monster;
        if (game.monster_present()) {
            monster = bumpy::MonsterPose{game.monster_frame(), game.monster_x(),
                                         game.monster_y()};
        }
        const auto quads = bumpy::build_live_quads(
            live, {}, monster, bumpy::BallPose{game.ball_frame(), game.ball_x(), game.ball_y()},
            sprites);

        const int w = 1280;
        const int h = 800;
        auto target = bumpy::make_offscreen_target(presenter.gl(), w, h);
        presenter.gl().BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        renderer.render(quads, static_cast<float>(game.ball_x()),
                        static_cast<float>(game.ball_y()), bumpy::Viewport{0, 0, w, h});
        const auto rgba = bumpy::read_target_rgba(presenter.gl(), target);
        presenter.gl().BindFramebuffer(GL_FRAMEBUFFER, 0);
        bumpy::destroy_offscreen_target(presenter.gl(), target);
        write_24bit_bmp_rgba(out_path, rgba, w, h);
        std::cout << "wrote " << out_path.string() << " (" << quads.size() << " quads)\n";
        rc = 0;
        SDL_DestroyWindow(window);
    } else {
        std::cerr << SDL_GetError() << '\n';
    }
    SDL_Quit();
    return rc;
}
#endif  // !__EMSCRIPTEN__

// Drive the in-level LevelGame on a board for a number of frames with a held
// direction, dumping <prefix>NN.bmp every few frames (board art + live entities +
// the ball) so the playfield gameplay can be verified by eye without the window.
int render_play_to_bmps(const std::filesystem::path& asset_root, int level_number,
                        const std::filesystem::path& monde_path, std::size_t board_index,
                        const std::string& dir, const std::string& prefix) {
    const auto level = bumpy::LevelResources::load(asset_root, level_number);
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto backdrop_bytes = backdrop.decoded_bytes();
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");

    bumpy::LevelGame game(level.bum_entities(board_index));
    bumpy::LevelInput input{};
    input.left = dir == "left" || dir == "leftfire";
    input.right = dir == "right";
    input.up = dir == "up";
    input.down = dir == "down";
    input.fire = dir == "fire" || dir == "leftfire";

    auto dump = [&](int index) {
        bumpy::IndexedFramebuffer frame(320, 200);
        bumpy::render_board(level, board_index, backdrop_bytes, frame);
        bumpy::BumEntities live{};
        std::copy(game.grid().begin(), game.grid().begin() + bumpy::BumEntities::record_size,
                  live.bytes.begin());
        // Tile bump/spring animations, drawn exactly as the SDL app does: blank the
        // static tile under each animating cell, then overlay the spring frames.
        std::array<bumpy::ObjectAnimSprite, 7> anims{};
        const std::size_t anim_count = game.object_anims(anims);
        for (std::size_t k = 0; k < anim_count; ++k) {
            const std::size_t cell = anims[k].cell;
            const std::size_t off = anims[k].layer_b ? bumpy::BumEntities::layer_b_offset
                                                     : bumpy::BumEntities::layer_a_offset;
            live.bytes[cell + off] = 0;
        }
        bumpy::draw_bum_entities(live, bank.bytes(), frame);
        bumpy::draw_object_anims({anims.data(), anim_count}, bank.bytes(), frame);
        if (game.monster_present()) {
            bumpy::draw_monster(bank.bytes(), game.monster_frame(), game.monster_x(),
                                game.monster_y(), frame);
        }
        bumpy::draw_ball(bank.bytes(), game.ball_frame(), game.ball_x(), game.ball_y(), frame);
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), frame);
        std::cout << "step " << std::setw(2) << index << ": cell 0x" << std::hex
                  << static_cast<int>(game.ball_cell()) << " state 0x"
                  << static_cast<int>(game.player_state()) << " frame 0x" << game.ball_frame()
                  << std::dec << " at (" << game.ball_x() << "," << game.ball_y() << ") gems "
                  << static_cast<int>(game.collectibles_left()) << " score " << game.score()
                  << " springs " << anim_count;
        if (anim_count > 0) {
            std::cout << " [cell 0x" << std::hex << static_cast<int>(anims[0].cell) << " frame 0x"
                      << anims[0].frame_index << std::dec << (anims[0].layer_b ? " B]" : " A]");
        }
        if (game.monster_present()) {
            std::cout << " | mob cell 0x" << std::hex << static_cast<int>(game.monster_cell())
                      << " frame 0x" << game.monster_frame() << std::dec << " at ("
                      << game.monster_x() << "," << game.monster_y() << ")";
        }
        std::cout << "\n";
    };

    constexpr int kFrames = 40;
    dump(0);
    for (int i = 1; i <= kFrames; ++i) {
        game.tick(input);
        dump(i);
    }
    std::cout << "wrote " << (kFrames + 1) << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

// Compose the world-map screen (MONDE backdrop + the Bumpy avatar at node 1) and
// dump it to a BMP for by-eye comparison with the original world-select capture.
int render_map_to_bmp(const std::filesystem::path& asset_root, int world_number,
                      const std::filesystem::path& monde_path,
                      const std::filesystem::path& out_path, int cleared_count) {
    if (world_number < 1 || world_number > bumpy::kWorldCount) {
        std::cerr << "--render-map world must be 1.." << bumpy::kWorldCount << '\n';
        return 2;
    }
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map(world_number);  // node 1 of the requested world
    std::vector<std::uint8_t> cleared(
        static_cast<std::size_t>(bumpy::world_node_count(world_number)), 0);
    for (int i = 0; i < cleared_count && i < static_cast<int>(cleared.size()); ++i) {
        cleared[static_cast<std::size_t>(i)] = 1;
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats =
        bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), frame, cleared);
    bumpy::draw_lives(bank.bytes(), 5, frame);
    const auto font = bumpy::Font::load(asset_root / "DDFNT2.CAR");
    bumpy::draw_score(font, 1234567, bumpy::kMapScoreX, bumpy::kMapScoreBaselineY,
                      bumpy::kScoreColor, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (world " << world_number << ", avatar "
              << (stats.avatar_drawn ? "drawn" : "skipped") << " at node " << map.current_node()
              << ", " << stats.markers_drawn << " node markers)\n";
    return 0;
}

// Drive the world map through the fire-to-enter cloud-jump on the given node and dump
// every animation step to <prefix>NN.bmp for by-eye verification of the recovered
// sequence (frames + bounce + cloud).
int render_jump_to_bmps(const std::filesystem::path& asset_root,
                        const std::filesystem::path& monde_path, int node,
                        const std::string& prefix) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();

    bumpy::WorldMap map;  // node 1
    // Walk to the requested node so the jump plays somewhere clearly visible.
    for (int n = 1; n < node; ++n) {
        map.update(bumpy::MenuInput{.right = true});
        int guard = 0;
        while (map.is_sliding() && guard++ < 1000) {
            map.update(bumpy::MenuInput{});
        }
        map.update(bumpy::MenuInput{});  // release
    }

    auto dump = [&](int index) {
        bumpy::IndexedFramebuffer frame(320, 200);
        bumpy::render_map(screen, map.view(), bank.bytes(), frame);
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), frame);
        std::cout << "step " << index << ": frame 0x" << std::hex << map.view().avatar_frame
                  << std::dec << " offset_y " << map.view().avatar_offset_y << " cloud "
                  << (map.view().cloud_visible ? "on" : "off") << "\n";
    };

    int index = 0;
    map.update(bumpy::MenuInput{.confirm = true});  // start the jump (poses step 0)
    dump(index++);
    int guard = 0;
    while (map.is_jumping() && guard++ < 1000) {
        map.update(bumpy::MenuInput{});
        dump(index++);
    }
    std::cout << "wrote " << index << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

// Dump the edge-to-centre screen-change darken (FUN_1000_3467) over a real screen --
// the world-map backdrop -- as <prefix>NN.bmp, one per step, so the wipe's geometry and
// pacing can be checked by eye against the original (see analysis/specs/screen-flow.md).
int render_transition_to_bmps(const std::filesystem::path& asset_root,
                              const std::filesystem::path& monde_path, const std::string& prefix) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map;  // node 1
    bumpy::IndexedFramebuffer outgoing(320, 200);
    bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), outgoing);

    auto dump = [&](int index, const bumpy::IndexedFramebuffer& f) {
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), f);
    };

    dump(0, outgoing);  // step 0: the un-darkened outgoing screen
    bumpy::ScreenTransition transition;
    transition.begin(outgoing);
    int index = 1;
    while (transition.active()) {
        bumpy::IndexedFramebuffer frame(320, 200);
        transition.render(frame);
        dump(index, frame);
        std::cout << "step " << std::setw(2) << index << ": border "
                  << bumpy::ScreenTransition::kCellW * transition.step() << "x"
                  << bumpy::ScreenTransition::kCellH * transition.step() << " px\n";
        transition.advance();
        ++index;
    }
    std::cout << "wrote " << index << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

int run_sdl_menu(const std::filesystem::path& asset_root, int start_world, bumpy::PortConfig config,
                 const std::filesystem::path& cfg_path) {
    warn_if_assets_changed(asset_root);
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);

    // Load the starting world (D{n} + MONDE{n}.VEC). The shell owns this bundle and
    // reloads it on each world change; the world-independent sprite bank + font are
    // loaded once and must outlive run().
    auto world = bumpy::WorldResources::load(asset_root, start_world);
    const auto sprite_bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto font = bumpy::Font::load(asset_root / "DDFNT2.CAR");
    // The post-world-9 ending screen (FUN_1000_3ed4). World-independent, so decode it once;
    // decoded_bytes() is a view into this resource, which must outlive run().
    const auto outro = bumpy::decode_vec_resource(asset_root / "DESSFIN.VEC");
    // The HIGH-SCORE / GAME-OVER backdrop (SCORE.VEC, MENU index 3). Ships as a raw 320x200
    // screen image (not a compressed VEC), so read it directly. World-independent; outlives run().
    const auto score_screen = bumpy::read_binary_file(asset_root / "SCORE.VEC");
    // The Tab settings overlay's renderer: SCORE.VEC backdrop + sprite bank glyphs +
    // FLECHE.BIN cursor. World-independent, constructed once, outlives run().
    const bumpy::SettingsRenderer settings_renderer(
        score_screen, sprite_bank.bytes(), resources.cursor_sprites);
    // The intro-music song + AdLib instrument bank (FUN_1000_30dd): world-independent, loaded
    // once and outliving run(); AudioEngine only ever reads them to (re)construct the player.
    const auto midi_song = bumpy::MidiSong::load(asset_root / "BUMPY.MID");
    const auto adlib_bank = bumpy::AdLibBank::load(asset_root / "BUMPY.BNK");
    bumpy::AudioEngine audio_engine(midi_song, adlib_bank);

    bumpy::App app(world.board_count(), start_world);
    bumpy::IndexedFramebuffer frame(320, 200);
    if (bumpy::is_screen_image(resources.splash.decoded_bytes())) {
        bumpy::apply_screen_image_palette(resources.splash.decoded_bytes(), frame);
        bumpy::draw_screen_image(resources.splash.decoded_bytes(), frame);
    }

    // SdlApp owns the SDL library lifetime (SDL_Init/SDL_Quit); construct it before SdlAudio
    // so SDL is already up when the audio device opens, and so SdlAudio (destroyed first,
    // LIFO) tears down its stream before SdlApp's destructor calls SDL_Quit().
    bumpy::SdlApp sdl;
    // Opens the default playback device and starts pulling from audio_engine.render() on
    // SDL's audio thread. Constructed around the whole run loop so the stream lives exactly
    // as long as the window does. The original game supports a "no sound" mode, so a
    // missing/locked/headless audio device must degrade to muted, not abort the game: if
    // construction throws, log a warning and keep `sdl_audio` empty. AudioEngine's
    // start_music()/stop_music()/play_sfx() calls remain safe to call with no SdlAudio
    // pulling from them -- they just accumulate state nobody renders -- so the run loop
    // below needs no changes for the muted path.
    std::optional<bumpy::SdlAudio> sdl_audio;
    try {
        sdl_audio.emplace(audio_engine);
    } catch (const std::exception& error) {
        std::cerr << "warning: could not open audio device, running muted: " << error.what() << '\n';
    }

    if (sdl_audio) {
        sdl.set_audio_pump(&*sdl_audio);
    }

    return sdl.run(app, renderer, settings_renderer, asset_root, std::move(world),
                   sprite_bank.bytes(), font, resources.splash.decoded_bytes(),
                   outro.decoded_bytes(), score_screen, frame, audio_engine, config, cfg_path);
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef __EMSCRIPTEN__
    // A destructor rather than a call before each `return`: main() has a dozen exits (the
    // offline dev tools) plus the catch below, and every one of them must reach the shell
    // -- a crash that leaves the page dead is exactly the case worth covering.
    struct GameExitNotice {
        ~GameExitNotice() { bumpy_web_game_exited(); }
    } game_exit_notice;
#endif
    try {
        const auto asset_root =
            find_asset_root(argc > 0 ? std::string_view(argv[0]) : std::string_view{});
        const auto cfg_path =
            config_file_path(argc > 0 ? std::string_view(argv[0]) : std::string_view{});
        bumpy::PortConfig config = bumpy::load_port_config(cfg_path);
        // --render3d anywhere on the command line: start in 3D (overrides the config).
        bool render3d_flag = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--render3d") {
                render3d_flag = true;
            }
        }
        if (render3d_flag) {
            config.render3d = true;
        }
        if (argc >= 2 && argc <= 4 && std::string_view(argv[1]) == "--render-title") {
            const auto out_path = argc >= 3 ? std::filesystem::path(argv[2])
                                            : asset_root / "analysis/generated/menu_with_marker.bmp";
            // Optional difficulty 0/1/2 (EASY/MEDIUM/HARD) to preview the LEVEL indicator.
            const int level_value =
                (argc == 4 && argv[3][0] >= '0' && argv[3][0] <= '2') ? argv[3][0] - '0' : 0;
            return render_title_to_bmp(asset_root, out_path, level_value);
        }
        if (argc == 3 && std::string_view(argv[1]) == "--dump-title-raw") {
            return dump_title_raw(asset_root, argv[2]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--decode-vec") {
            return decode_vec_file(argv[2], argv[3]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-screen") {
            return render_screen_vec(argv[2], argv[3]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-outro") {
            // --render-outro <DESSFIN.VEC> <out.bmp>: the ending screen via the shell's path.
            return render_outro_to_bmp(argv[2], argv[3]);
        }
        if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--render-highscores") {
            // --render-highscores <SCORE.VEC> <out.bmp> [insert_score]
            const long insert = argc == 5 ? std::stol(argv[4]) : 0;
            return render_highscores_to_bmp(asset_root, argv[2], argv[3], insert);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-gameover") {
            // --render-gameover <SCORE.VEC> <out.bmp>
            return render_gameover_to_bmp(asset_root, argv[2], argv[3]);
        }
        if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--render-password") {
            // --render-password <SCORE.VEC> <out.bmp> [CODE]: [CODE] shows the OK/ERROR flash.
            return render_password_to_bmp(asset_root, argv[2], argv[3], argc == 5 ? argv[4] : "");
        }
        if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "--render-options") {
            // --render-options <SCORE.VEC> <out.bmp> [root|video|audio|passwords]
            return render_options_to_bmp(asset_root, argv[2], argv[3], argc == 5 ? argv[4] : "root");
        }
        if ((argc == 5 || argc == 6) && std::string_view(argv[1]) == "--render-map") {
            // --render-map <world> <MONDE.VEC> <out.bmp> [cleared_node_count]
            const int cleared = argc == 6 ? std::stoi(argv[5]) : 0;
            return render_map_to_bmp(asset_root, std::stoi(argv[2]), argv[3], argv[4], cleared);
        }
        if (argc == 7 && std::string_view(argv[1]) == "--render-play") {
            // --render-play <level> <MONDE.VEC> <board> <dir> <out-prefix>
            // dir = none|left|right|up|down|fire (held for every frame).
            return render_play_to_bmps(asset_root, std::stoi(argv[2]), argv[3],
                                       static_cast<std::size_t>(std::stoi(argv[4])), argv[5], argv[6]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-transition") {
            // --render-transition <MONDE.VEC> <out-prefix>
            // Dumps the edge-to-centre screen-change darken over the world map as
            // <prefix>NN.bmp (step 00 = un-darkened, then one per ring).
            return render_transition_to_bmps(asset_root, argv[2], argv[3]);
        }
        if (argc == 5 && std::string_view(argv[1]) == "--render-jump") {
            // --render-jump <node> <MONDE.VEC> <out-prefix>
            // Dumps the fire-to-enter cloud-jump animation on <node> as <prefix>NN.bmp.
            return render_jump_to_bmps(asset_root, argv[3], std::stoi(argv[2]), argv[4]);
        }
        if ((argc == 6 || argc == 7) && std::string_view(argv[1]) == "--render-board") {
            // --render-board <level> <MONDE.VEC> <board_index> <out.bmp> [map|entities]
            // Faithful by default: the base-tile pass clears to colour index 0 and the
            // PAV objects compose the board over it, using the MONDE per-world palette.
            // The optional "map" token overlays the MONDE world-select screen instead
            // of the flat clear (a debug aid; that screen is not the playfield).
            // "entities" overlays the decoded BUM entity grid as inspection markers;
            // "sprites" draws the real BUM entity sprites from BUMSPJEU.BIN.
            const auto token = argc == 7 ? std::string_view(argv[6]) : std::string_view{};
            const bool draw_map = token == "map";
            const bool draw_entities = token == "entities";
            const bool draw_sprites = token == "sprites";
            return render_board_to_bmp(asset_root, std::stoi(argv[2]), argv[3],
                                       static_cast<std::size_t>(std::stoi(argv[4])), argv[5],
                                       draw_map, draw_entities, draw_sprites);
        }
        if (argc == 5 && std::string_view(argv[1]) == "--render-sprite") {
            // --render-sprite <level> <frame> <out.rgba>: one BUMSPJEU frame -> raw RGBA.
            return render_sprite_to_rgba(asset_root, std::stoi(argv[2]), std::stoi(argv[3]), argv[4]);
        }
#ifndef __EMSCRIPTEN__
        if (argc == 6 && std::string_view(argv[1]) == "--render-3d") {
            // --render-3d <level> <MONDE.VEC> <board> <out.bmp>: headless diorama dump.
            return render_3d_to_bmp(asset_root, argv[0], std::stoi(argv[2]), argv[3],
                                    static_cast<std::size_t>(std::stoi(argv[4])), argv[5]);
        }
#endif
        if (argc == 9 && std::string_view(argv[1]) == "--render-pav") {
            // --render-pav <pav> <pal|DEBUG> <out.bmp> <layout> <w> <h> <hdr>
            return render_pav(argv[2], argv[3], argv[4], argv[5], std::stoi(argv[6]),
                              std::stoi(argv[7]), std::stoi(argv[8]));
        }
        if ((argc == 5 || argc == 6) && std::string_view(argv[1]) == "--dump-music") {
            // --dump-music <BUMPY.MID> <BUMPY.BNK> <out.wav> [seconds]
            const double seconds = argc == 6 ? std::stod(argv[5]) : 20.0;
            return dump_music_to_wav(argv[2], argv[3], argv[4], seconds);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--dump-sfx") {
            // --dump-sfx <preset_id> <out.wav>  (preset_id: decimal or 0x-prefixed hex)
            const int preset_id = static_cast<int>(std::stol(argv[2], nullptr, 0));
            return dump_sfx_to_wav(preset_id, argv[3]);
        }
#ifndef __EMSCRIPTEN__
        if (argc == 2 && std::string_view(argv[1]) == "--present-parity") {
            return present_parity(asset_root);
        }
#endif
        // The tool branches above always parse raw argc/argv (unaffected by --render3d).
        // Everything below instead consults `args` -- argv[1..] with any "--render3d"
        // filtered out -- so that flag can be combined with --start-world or the default
        // launch without perturbing the argument counts/positions those branches check.
        std::vector<std::string_view> args;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) != "--render3d") {
                args.emplace_back(argv[i]);
            }
        }
        int start_world = 1;
        if (args.size() == 2 && args[0] == "--start-world") {
            start_world = std::stoi(std::string(args[1]));
            if (start_world < 1 || start_world > bumpy::kWorldCount) {
                std::cerr << "--start-world must be 1.." << bumpy::kWorldCount << '\n';
                return 2;
            }
            return run_sdl_menu(asset_root, start_world, config, cfg_path);
        }
        if (!args.empty()) {
            std::cerr << "usage: bumpy_port.exe [--render-title [out.bmp] | --start-world N | ...]\n";
            return 2;
        }
        return run_sdl_menu(asset_root, start_world, config, cfg_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
