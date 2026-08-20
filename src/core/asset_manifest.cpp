#include "core/asset_manifest.h"

#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace {

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream source(path, std::ios::binary);
    if (!source) {
        throw std::runtime_error("cannot open asset: " + path.string());
    }
    bumpy::Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (source) {
        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(source.gcount());
        if (count != 0) {
            hash.update(buffer.data(), count);
        }
    }
    if (!source.eof()) {
        throw std::runtime_error("cannot read asset: " + path.string());
    }
    return hash.hex_digest();
}

bool is_plain_filename(const std::string& name) {
    const std::filesystem::path path(name);
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos && !path.has_root_name() &&
           !path.has_root_directory();
}

}  // namespace

namespace bumpy {

AssetManifest AssetManifest::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open asset manifest: " + path.string());
    }

    AssetManifest result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() < 67 || line.substr(64, 2) != "  ") {
            throw std::runtime_error("invalid asset manifest line");
        }
        auto name = line.substr(66);
        if (!is_plain_filename(name)) {
            throw std::runtime_error("asset name is not a plain filename: " + name);
        }
        result.entries_.emplace_back(line.substr(0, 64), std::move(name));
    }
    return result;
}

AssetVerification AssetManifest::verify(const std::filesystem::path& root) const {
    AssetVerification result;
    result.file_count = entries_.size();
    for (const auto& [expected, name] : entries_) {
        const auto path = root / name;
        if (!std::filesystem::is_regular_file(path)) {
            result.missing.push_back(name);
        } else if (sha256_file(path) != expected) {
            result.changed.push_back(name);
        }
    }
    std::sort(result.missing.begin(), result.missing.end());
    std::sort(result.changed.begin(), result.changed.end());
    return result;
}

}  // namespace bumpy
