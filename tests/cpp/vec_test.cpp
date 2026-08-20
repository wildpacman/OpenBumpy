#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "resources/vec.h"

#include "core/sha256.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    return bumpy::sha256_hex(bytes.data(), bytes.size());
}

void append_be_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void append_be_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
}

std::string command_layout_sha(const bumpy::VecResource& resource) {
    std::vector<std::uint8_t> bytes;
    for (const auto& layer : resource.layers()) {
        append_be_u32(bytes, static_cast<std::uint32_t>(layer.input_size));
        append_be_u32(bytes, layer.output_size);
        append_be_u32(bytes, layer.auxiliary);
        append_be_u16(bytes, layer.method_flags);
        append_be_u16(bytes, layer.checksum);
        append_be_u32(bytes, static_cast<std::uint32_t>(layer.payload_size));
        bytes.push_back(layer.method);
        bytes.push_back(layer.final_layer ? 1 : 0);
        if (const auto* method = std::get_if<bumpy::VecMethod4>(&layer.command)) {
            bytes.push_back(method->marker);
        } else if (const auto* method = std::get_if<bumpy::VecMethod12>(&layer.command)) {
            append_be_u16(bytes, method->marker);
            append_be_u32(bytes, static_cast<std::uint32_t>(method->mask_block_count));
            append_be_u32(bytes, static_cast<std::uint32_t>(method->literal_count));
        }
    }
    return sha256_hex(bytes);
}

std::filesystem::path write_fixture(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
    return path;
}

}  // namespace

TEST_CASE("vec decoder matches approved menu resource report") {
    const auto title = bumpy::decode_vec_resource("TITRE.VEC");

    REQUIRE(title.terminal_offset() == std::filesystem::file_size("TITRE.VEC"));
    REQUIRE(title.layers().size() == 1);
    REQUIRE(title.layers()[0].input_size == std::filesystem::file_size("TITRE.VEC"));
    REQUIRE(title.layers()[0].output_size == 32099);
    REQUIRE(title.layers()[0].auxiliary == 0x49dc3356);
    REQUIRE(title.layers()[0].method == 4);
    REQUIRE(title.layers()[0].final_layer);
    REQUIRE(std::holds_alternative<bumpy::VecMethod4>(title.layers()[0].command));
    REQUIRE(command_layout_sha(title) == "4d12a3b8d8c8ef559c56e65c81ecddfa553659bd9583464172908937353876b9");
    REQUIRE(sha256_hex(title.decoded_bytes()) == "bc167b89d7b3926f167b4a3bf11234574e939f98d2475f4af39f3f050ffcc8b7");

    const auto mask = bumpy::decode_vec_resource("MASKBUMP.VEC");

    REQUIRE(mask.terminal_offset() == std::filesystem::file_size("MASKBUMP.VEC"));
    REQUIRE(mask.layers().size() == 3);
    REQUIRE(mask.layers()[0].method == 4);
    REQUIRE(mask.layers()[0].output_size == 3358);
    REQUIRE(!mask.layers()[0].final_layer);
    REQUIRE(mask.layers()[1].method == 12);
    REQUIRE(mask.layers()[1].output_size == 6560);
    REQUIRE(!mask.layers()[1].final_layer);
    REQUIRE(mask.layers()[2].method == 12);
    REQUIRE(mask.layers()[2].output_size == 32099);
    REQUIRE(mask.layers()[2].final_layer);
    REQUIRE(std::holds_alternative<bumpy::VecMethod12>(mask.layers()[2].command));
    REQUIRE(command_layout_sha(mask) == "78f54fcd9d7bc16ef10b67ea92efefb3b97f89fc71710e9c427e836eda963c21");
    REQUIRE(sha256_hex(mask.decoded_bytes()) == "a737e851ab831c3233d4ef9c3f39fb429bb846a1d33bb6b9e738a0b1c3e1d65a");
}

TEST_CASE("vec decoder accepts every supplied vec in the recovered family") {
    const std::map<std::string, std::string> expected_hashes{
        {"BUMPRESE.VEC", "fc3413e5dae7b9c7829fa636ca19b1ac4c202432b83da0f118d8e628b2a10014"},
        {"DESSFIN.VEC", "b3a8db48c97c9dc01aa8f551a8846c98d4c084060722f2f0657ffd3797f773e2"},
        {"MASKBUMP.VEC", "a737e851ab831c3233d4ef9c3f39fb429bb846a1d33bb6b9e738a0b1c3e1d65a"},
        {"MONDE1.VEC", "dae353b2f660bab56a5e19558610bf389f2cbaab0ca3800fa511e7abee741ed4"},
        {"MONDE2.VEC", "160328902d881bec060bc95f1f33fb107ac34a0b219b551e86d090414e74b59b"},
        {"MONDE3.VEC", "40cada7df5519e0e67075d7da9092b04a78cc4adaf2ef3a74ba9cb488e67630a"},
        {"MONDE4.VEC", "c79dfeef8b6b4071eb8d17ef9e1974689499abb6abe6d765f82763043277c390"},
        {"MONDE5.VEC", "4ffe18d38d496e0e94d7c31e09ac7290c77e80b07b2c2b3593ee650b62067d1c"},
        {"MONDE6.VEC", "45d7ac0de9dc8ee696eb9361b7900ce0d1108eb6995e5ac686858125bcdca821"},
        {"MONDE7.VEC", "649ab9053f465c0a896ecf3a7ad80c2d36a73c91a39c3c9c2a55fb2cfdcf8ea4"},
        {"MONDE8.VEC", "3e4cf35919307bb2c6229762f465815b052014826d5bbe66f33c65d009c478d7"},
        {"MONDE9.VEC", "bba48ebc9fb7db543a81388e8d41a63b2a4575746d3338a8b998fdc94280c507"},
        {"TITRE.VEC", "bc167b89d7b3926f167b4a3bf11234574e939f98d2475f4af39f3f050ffcc8b7"},
    };

    for (const auto& [name, expected_hash] : expected_hashes) {
        const auto resource = bumpy::decode_vec_resource(name);
        INFO(name);
        REQUIRE(resource.terminal_offset() == std::filesystem::file_size(name));
        REQUIRE(resource.decoded_bytes().size() == 32099);
        REQUIRE(sha256_hex(resource.decoded_bytes()) == expected_hash);
    }

    REQUIRE_THROWS_WITH(
        bumpy::decode_vec_resource("SCORE.VEC"),
        Catch::Matchers::ContainsSubstring("SCORE.VEC") &&
            Catch::Matchers::ContainsSubstring("0x00000008"));
}

TEST_CASE("vec decoder reports malformed fixtures with resource name and failing offset") {
    const auto path = write_fixture("bumpy-truncated-vec.vec", {0x00, 0x00, 0x7d});

    try {
        (void)bumpy::decode_vec_resource(path);
        FAIL("expected malformed VEC to throw");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        REQUIRE(message.find("bumpy-truncated-vec.vec") != std::string::npos);
        REQUIRE(message.find("0x00000000") != std::string::npos);
    }

    std::filesystem::remove(path);
}
