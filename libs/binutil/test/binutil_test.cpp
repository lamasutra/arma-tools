#include "armatools/binutil.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <sstream>

using namespace armatools::binutil;

namespace {

std::istringstream make_stream(const void* data, size_t size) {
    return std::istringstream(std::string(static_cast<const char*>(data), size));
}

} // namespace

TEST(Binutil, ReadU16) {
    uint16_t val = 0x1234;
    auto s = make_stream(&val, sizeof(val));
    EXPECT_EQ(read_u16(s), 0x1234);
}

TEST(Binutil, ReadU32) {
    uint32_t val = 0xDEADBEEF;
    auto s = make_stream(&val, sizeof(val));
    EXPECT_EQ(read_u32(s), 0xDEADBEEF);
}

TEST(Binutil, ReadI32) {
    int32_t val = -1;
    auto s = make_stream(&val, sizeof(val));
    EXPECT_EQ(read_i32(s), -1);
}

TEST(Binutil, ReadF32) {
    float val = 3.14f;
    auto s = make_stream(&val, sizeof(val));
    EXPECT_NEAR(read_f32(s), 3.14f, 0.001f);
}

TEST(Binutil, ReadAsciiz) {
    std::string data("hello\0world\0", 12);
    std::istringstream s(data);
    EXPECT_EQ(read_asciiz(s), "hello");
    EXPECT_EQ(read_asciiz(s), "world");
}

TEST(Binutil, ReadFixedString) {
    char buf[32] = {};
    std::strcpy(buf, "test.pac");
    auto s = make_stream(buf, sizeof(buf));
    EXPECT_EQ(read_fixed_string(s, 32), "test.pac");
}

TEST(Binutil, ReadTransformMatrix) {
    std::array<float, 12> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1, 100, 200, 300};
    auto s = make_stream(identity.data(), sizeof(identity));
    auto m = read_transform_matrix(s);
    for (size_t i = 0; i < 12; i++)
        EXPECT_EQ(m[i], identity[i]);
}

TEST(Binutil, ReadF32Slice) {
    std::array<float, 3> vals = {1.0f, 2.5f, 3.7f};
    auto s = make_stream(vals.data(), sizeof(vals));
    auto out = read_f32_slice(s, 3);
    ASSERT_EQ(out.size(), 3u);
    for (size_t i = 0; i < 3; i++)
        EXPECT_EQ(out[i], vals[i]);
}

TEST(Binutil, ReadSignature) {
    auto s = make_stream("OPRW", 4);
    EXPECT_EQ(read_signature(s), "OPRW");
}

TEST(Binutil, ReadCompressedInt) {
    // 300 = 0x12C: 7-bit encoding → 0xAC, 0x02
    uint8_t data[] = {0xAC, 0x02};
    auto s = make_stream(data, sizeof(data));
    EXPECT_EQ(read_compressed_int(s), 300u);
}

TEST(Binutil, WriteAsciiz) {
    std::ostringstream out;
    write_asciiz(out, "hello");
    std::string result = out.str();
    EXPECT_EQ(result.size(), 6u);
    EXPECT_EQ(result, std::string("hello\0", 6));
}

TEST(Binutil, WriteU32) {
    std::ostringstream out;
    write_u32(out, 0xDEADBEEF);
    auto data = out.str();
    uint32_t v;
    std::memcpy(&v, data.data(), 4);
    EXPECT_EQ(v, 0xDEADBEEF);
}
