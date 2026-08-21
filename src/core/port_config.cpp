#include "core/port_config.h"

#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

#include <array>
#include <string_view>

// The browser has no directory next to an executable, so the five flags live in one
// localStorage entry holding exactly the key=value text the desktop file format uses.
// Bytes are copied through HEAPU8 rather than the UTF-8 helpers: the config is ASCII by
// construction, and this needs no EM_JS_DEPS declaration to link.
EM_JS(int, bumpy_cfg_read, (char* out, int capacity), {
    var text;
    try {
        text = localStorage.getItem("bumpy_port_cfg");
    } catch (e) {
        return -1;  // storage disabled (private mode, blocked cookies) -> use defaults
    }
    if (text === null || text.length + 1 > capacity) {
        return -1;
    }
    for (var i = 0; i < text.length; ++i) {
        HEAPU8[out + i] = text.charCodeAt(i) & 0xff;
    }
    HEAPU8[out + text.length] = 0;
    return text.length;
});

// Returns 1 on a successful localStorage.setItem, 0 otherwise (quota exceeded, storage
// disabled) so save_port_config can report failure the same way the desktop file write does.
EM_JS(int, bumpy_cfg_write, (const char* text, int length), {
    var s = "";
    for (var i = 0; i < length; ++i) {
        s += String.fromCharCode(HEAPU8[text + i]);
    }
    try {
        localStorage.setItem("bumpy_port_cfg", s);
    } catch (e) {
        return 0;  // storage full or disabled: settings just do not survive the reload
    }
    return 1;
});
#endif

namespace bumpy {

namespace {

// "1"/"0" only; anything else leaves `out` untouched (tolerate hand-edits).
void parse_bool(std::string_view value, bool& out) {
    if (value == "1") {
        out = true;
    } else if (value == "0") {
        out = false;
    }
}

}  // namespace

PortConfig parse_port_config(std::string_view text) noexcept {
    PortConfig config;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            continue;
        }
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (key == "render3d") {
            parse_bool(value, config.render3d);
        } else if (key == "square_pixels") {
            parse_bool(value, config.square_pixels);
        } else if (key == "fullscreen") {
            parse_bool(value, config.fullscreen);
        } else if (key == "music") {
            parse_bool(value, config.music);
        } else if (key == "sfx") {
            parse_bool(value, config.sfx);
        }
    }
    return config;
}

std::string serialize_port_config(const PortConfig& config) {
    std::ostringstream out;
    out << "# Bumpy's Arcade Fantasy port settings (auto-written; hand-edits are kept\n"
        << "# for known keys, unknown keys are ignored)\n"
        << "render3d=" << (config.render3d ? 1 : 0) << '\n'
        << "square_pixels=" << (config.square_pixels ? 1 : 0) << '\n'
        << "fullscreen=" << (config.fullscreen ? 1 : 0) << '\n'
        << "music=" << (config.music ? 1 : 0) << '\n'
        << "sfx=" << (config.sfx ? 1 : 0) << '\n';
    return out.str();
}

PortConfig load_port_config(const std::filesystem::path& path) noexcept {
#ifdef __EMSCRIPTEN__
    (void)path;  // the web build persists to localStorage, not to a file
    std::array<char, 1024> buffer{};
    const int length = bumpy_cfg_read(buffer.data(), static_cast<int>(buffer.size()));
    if (length < 0) {
        return {};
    }
    return parse_port_config(std::string_view(buffer.data(), static_cast<std::size_t>(length)));
#else
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parse_port_config(text.str());
    } catch (...) {
        return {};
    }
#endif
}

bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept {
#ifdef __EMSCRIPTEN__
    (void)path;
    try {
        const std::string text = serialize_port_config(config);
        return bumpy_cfg_write(text.c_str(), static_cast<int>(text.size())) != 0;
    } catch (...) {
        return false;
    }
#else
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << serialize_port_config(config);
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
#endif
}

}  // namespace bumpy
