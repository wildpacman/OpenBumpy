#include "core/sha256.h"

#include <algorithm>
#include <cstring>

namespace bumpy {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

void Sha256::compress(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        const auto* p = block + i * 4;
        w[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(p[0]) << 24) |
                                         (static_cast<std::uint32_t>(p[1]) << 16) |
                                         (static_cast<std::uint32_t>(p[2]) << 8) |
                                         static_cast<std::uint32_t>(p[3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t a = w[i - 15];
        const std::uint32_t b = w[i - 2];
        const std::uint32_t s0 = rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3);
        const std::uint32_t s1 = rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += size;
    while (size != 0) {
        const std::size_t take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == buffer_.size()) {
            compress(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::string Sha256::hex_digest() {
    // The length is in bits and covers the message only, so capture it before the
    // padding bytes go through update() and inflate total_bytes_.
    const std::uint64_t bit_length = total_bytes_ * 8;

    constexpr std::uint8_t kPadStart = 0x80;
    update(&kPadStart, 1);
    constexpr std::uint8_t kZero = 0;
    while (buffered_ != 56) {
        update(&kZero, 1);
    }
    std::array<std::uint8_t, 8> length_be{};
    for (std::size_t i = 0; i < 8; ++i) {
        length_be[i] = static_cast<std::uint8_t>(bit_length >> (56 - i * 8));
    }
    update(length_be.data(), length_be.size());  // completes the final block

    constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const auto word : state_) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto byte = static_cast<std::uint8_t>(word >> shift);
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0fU]);
        }
    }
    return hex;
}

std::string sha256_hex(const void* data, std::size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hash.hex_digest();
}

}  // namespace bumpy
