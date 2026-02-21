#include "armatools/lzss.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <sstream>

using namespace armatools::lzss;

// --- Decompression tests ---

TEST(Lzss, DecompressOrRawSmall) {
    // Data < 1024 bytes should be returned as-is (raw, no decompression).
    std::string data = "Hello, World!";
    std::istringstream s(data);
    auto got = decompress_or_raw(s, data.size());
    ASSERT_EQ(got.size(), data.size());
    EXPECT_EQ(std::memcmp(got.data(), data.data(), data.size()), 0);
}

TEST(Lzss, DecompressOrRawZeroSize) {
    std::istringstream s("");
    auto got = decompress_or_raw(s, 0);
    EXPECT_TRUE(got.empty());
}

// --- Compression round-trip tests ---

// Helper: compress with unsigned checksum, then decompress_buf and verify.
static void round_trip_unsigned(const std::vector<uint8_t>& input) {
    auto compressed = compress(input.data(), input.size());

    // Compressed output should have at least the 4-byte checksum.
    ASSERT_GE(compressed.size(), 4u);

    auto decompressed = decompress_buf(compressed.data(), compressed.size(), input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

// Helper: compress with signed checksum, then decompress_signed and verify.
static void round_trip_signed(const std::vector<uint8_t>& input) {
    auto compressed = compress_signed(input.data(), input.size());
    ASSERT_GE(compressed.size(), 4u);

    auto decompressed = decompress_signed(compressed.data(), compressed.size(), input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

// Helper: compress without checksum, then decompress_nochecksum and verify.
static void round_trip_nochecksum(const std::vector<uint8_t>& input) {
    auto compressed = compress_nochecksum(input.data(), input.size());

    auto decompressed = decompress_nochecksum(compressed.data(), compressed.size(), input.size());
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(decompressed, input);
}

TEST(LzssCompress, EmptyInput) {
    std::vector<uint8_t> empty;
    auto compressed = compress(empty.data(), 0);
    // Should just be the 4-byte checksum (sum=0).
    ASSERT_EQ(compressed.size(), 4u);
    uint32_t checksum;
    std::memcpy(&checksum, compressed.data(), 4);
    EXPECT_EQ(checksum, 0u);
}

TEST(LzssCompress, SingleByte) {
    std::vector<uint8_t> data = {0x42};
    round_trip_unsigned(data);
}

TEST(LzssCompress, SmallLiterals) {
    // Short data with no repeats -- should be all literals.
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
    round_trip_unsigned(data);
}

TEST(LzssCompress, RepeatedBytes) {
    // Highly repetitive data -- should compress well with back-references.
    std::vector<uint8_t> data(1000, 'A');
    round_trip_unsigned(data);

    auto compressed = compress(data.data(), data.size());
    // Compressed should be significantly smaller than original.
    EXPECT_LT(compressed.size(), data.size() / 2);
}

TEST(LzssCompress, RepeatingPattern) {
    // Repeating multi-byte pattern.
    std::vector<uint8_t> data;
    for (int i = 0; i < 200; i++) {
        data.push_back('A');
        data.push_back('B');
        data.push_back('C');
        data.push_back('D');
    }
    round_trip_unsigned(data);

    auto compressed = compress(data.data(), data.size());
    EXPECT_LT(compressed.size(), data.size() / 2);
}

TEST(LzssCompress, SequentialBytes) {
    // Sequential bytes 0..255 repeated -- has some patterns.
    std::vector<uint8_t> data(512);
    std::iota(data.begin(), data.end(), 0);
    round_trip_unsigned(data);
}

TEST(LzssCompress, RandomData) {
    // Random data -- should not crash and should round-trip correctly.
    // Compression ratio may be poor.
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> data(2000);
    for (auto& b : data) b = static_cast<uint8_t>(dist(gen));
    round_trip_unsigned(data);
}

TEST(LzssCompress, LargeData) {
    // Larger input to test window boundary behavior.
    std::vector<uint8_t> data;
    data.reserve(8192);
    for (size_t i = 0; i < 8192; i++)
        data.push_back(static_cast<uint8_t>(i % 127));
    round_trip_unsigned(data);
}

TEST(LzssCompress, LongDistanceMatch) {
    // Create data where matches are far apart (near max window distance).
    std::vector<uint8_t> data;
    // Write a 10-byte pattern, then ~4000 bytes of other data, then repeat the pattern.
    std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x12, 0x34};
    data.insert(data.end(), pattern.begin(), pattern.end());
    for (int i = 0; i < 4000; i++)
        data.push_back(static_cast<uint8_t>(i & 0xFF));
    data.insert(data.end(), pattern.begin(), pattern.end());
    round_trip_unsigned(data);
}

TEST(LzssCompress, Exactly8Items) {
    // Test flag byte boundary: exactly 8 bytes (one full flag byte group).
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    round_trip_unsigned(data);
}

TEST(LzssCompress, SignedChecksum) {
    // Test signed checksum variant with bytes that would differ between signed/unsigned.
    std::vector<uint8_t> data;
    for (int i = 0; i < 500; i++)
        data.push_back(static_cast<uint8_t>(0x80 + (i % 128)));
    round_trip_signed(data);
}

TEST(LzssCompress, NoChecksum) {
    std::vector<uint8_t> data(300, 'X');
    round_trip_nochecksum(data);
}

TEST(LzssCompress, StreamDecompress) {
    // Test that compress output can be decompressed via the stream-based decompress().
    std::vector<uint8_t> data;
    for (int i = 0; i < 2000; i++)
        data.push_back(static_cast<uint8_t>(i % 50));

    auto compressed = compress(data.data(), data.size());

    std::string compressed_str(reinterpret_cast<const char*>(compressed.data()),
                               compressed.size());
    std::istringstream s(compressed_str);
    auto decompressed = decompress(s, data.size());
    ASSERT_EQ(decompressed.size(), data.size());
    EXPECT_EQ(decompressed, data);
}

TEST(LzssCompress, AutoDecompress) {
    // Test that compress output can be decompressed via decompress_buf_auto().
    std::vector<uint8_t> data(500, 'Z');
    auto compressed = compress(data.data(), data.size());
    auto decompressed = decompress_buf_auto(compressed.data(), compressed.size());
    ASSERT_EQ(decompressed.size(), data.size());
    EXPECT_EQ(decompressed, data);
}
