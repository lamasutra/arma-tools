#include "armatools/lzo.h"

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>

using namespace armatools::lzo;

namespace {

std::istringstream make_stream(const std::vector<uint8_t>& data) {
    return std::istringstream(std::string(data.begin(), data.end()));
}

} // namespace

TEST(Lzo, DecompressOrRawSmall) {
    std::string data = "Hello, World!";
    std::istringstream s(data);
    auto got = decompress_or_raw(s, data.size());
    ASSERT_EQ(got.size(), data.size());
    EXPECT_EQ(std::memcmp(got.data(), data.data(), data.size()), 0);
}

TEST(Lzo, DecompressOrRawZeroSize) {
    std::istringstream s("");
    auto got = decompress_or_raw(s, 0);
    EXPECT_TRUE(got.empty());
}

TEST(Lzo, DecompressPureLiterals) {
    // B_3 path: first byte <= 17 → enter B_3.
    // Tag=1 → t=1, copy t+3=4 literal bytes.
    // Then first_literal_run tag 0x11 (>=16) → enter match as M4.
    // M4: t=0x11, mPos=op-(0x11&8)<<11 = op-0 = 4, t&=7 → 1
    // Read 2 offset bytes: 0x00, 0x00 → mPos-=0 → mPos==op → EOS.
    std::vector<uint8_t> compressed = {
        0x01,                 // B_3 tag: t=1 → copy 4 literals
        'A', 'B', 'C', 'D',  // 4 literal bytes
        0x11,                 // first_literal_run → M4 tag
        0x00, 0x00,           // M4 EOS marker
    };

    auto s = make_stream(compressed);
    auto got = decompress(s, 4);
    ASSERT_EQ(got.size(), 4u);
    EXPECT_EQ(std::string(got.begin(), got.end()), "ABCD");
}

TEST(Lzo, DecompressLiteralAndM3Match) {
    // Produce "ABCDAABC" (8 bytes)
    std::vector<uint8_t> compressed = {
        0x02,                         // B_3 tag: t=2
        'A', 'B', 'C', 'D', 'A',     // 5 literals
        33,                           // M3 tag (32|1)
        16, 0,                        // M3 offset bytes
        0x11,                         // B_3 inline → M4 EOS
        0x00, 0x00,                   // M4 EOS marker
    };

    auto s = make_stream(compressed);
    auto got = decompress(s, 8);
    ASSERT_EQ(got.size(), 8u);
    EXPECT_EQ(std::string(got.begin(), got.end()), "ABCDAABC");
}

TEST(Lzo, DecompressM2Match) {
    // Produce "ABCAABC" (7 bytes)
    std::vector<uint8_t> compressed = {
        0x01,                 // B_3 tag: t=1 → 4 literals
        'A', 'B', 'C', 'A',  // 4 literals
        0x4C,                 // first_literal_run → M2 tag
        0x00,                 // M2 nb=0
        0x11,                 // B_3 inline → M4 EOS
        0x00, 0x00,           // M4 EOS marker
    };

    auto s = make_stream(compressed);
    auto got = decompress(s, 7);
    ASSERT_EQ(got.size(), 7u);
    EXPECT_EQ(std::string(got.begin(), got.end()), "ABCAABC");
}
