#include <catch2/catch_test_macros.hpp>

#include "core/sha256.h"

#include <algorithm>
#include <string>

namespace {

std::string hex_of(const std::string& text) {
    return bumpy::sha256_hex(text.data(), text.size());
}

}  // namespace

// FIPS 180-4 / NIST CAVP known-answer vectors.
TEST_CASE("sha256 matches the standard vectors") {
    REQUIRE(hex_of("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(hex_of("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 56 bytes: the padding lands exactly on the two-block boundary.
    REQUIRE(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 112 bytes.
    REQUIRE(hex_of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                   "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("sha256 is independent of how the input is chunked") {
    const std::string message(1000, 'x');
    const std::string one_shot = bumpy::sha256_hex(message.data(), message.size());

    bumpy::Sha256 streamed;
    // Chunk sizes chosen to straddle the 64-byte block boundary repeatedly.
    std::size_t offset = 0;
    for (const std::size_t chunk : {1U, 63U, 64U, 65U, 127U, 300U}) {
        const std::size_t take = std::min(chunk, message.size() - offset);
        streamed.update(message.data() + offset, take);
        offset += take;
    }
    streamed.update(message.data() + offset, message.size() - offset);

    REQUIRE(streamed.hex_digest() == one_shot);
}

TEST_CASE("sha256 handles a message longer than 64 KB") {
    const std::string message(1'000'000, 'a');
    REQUIRE(bumpy::sha256_hex(message.data(), message.size()) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
