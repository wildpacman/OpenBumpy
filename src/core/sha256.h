#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bumpy {

// Streaming SHA-256 (FIPS 180-4). Deliberately dependency-free: this replaces a
// Windows BCrypt call that was bumpy_core's only OS dependency, and the web build
// cannot link bcrypt at all.
class Sha256 {
public:
    void update(const void* data, std::size_t size) noexcept;
    // Appends the padding, consumes the final block, and returns the digest as 64
    // lowercase hex characters. Call once: it mutates the internal state.
    [[nodiscard]] std::string hex_digest();

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{};
    std::uint64_t total_bytes_{};
};

// One-shot convenience over the whole buffer.
[[nodiscard]] std::string sha256_hex(const void* data, std::size_t size);

}  // namespace bumpy
