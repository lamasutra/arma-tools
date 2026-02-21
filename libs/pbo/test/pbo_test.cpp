#include "armatools/pbo.h"

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>

using namespace armatools::pbo;

namespace {

// Helper to build a minimal PBO in memory.
// Layout:
//   Extension header ("Vers") with prefix="test_prefix"
//   One file entry: "config.bin" with 5 bytes of data
//   End-of-headers marker
//   5 bytes of file data: "hello"
//   0x00 + 20 bytes of SHA1 checksum (all zeros)
std::string build_test_pbo() {
    std::ostringstream out;
    auto write_asciiz = [&](const std::string& s) {
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        out.put('\0');
    };
    auto write_u32 = [&](uint32_t v) {
        out.write(reinterpret_cast<const char*>(&v), 4);
    };

    // Extension header
    write_asciiz("");           // empty filename for header entry
    write_u32(0x56657273);      // packing_method = "Vers"
    write_u32(0);               // original_size
    write_u32(0);               // reserved
    write_u32(0);               // timestamp
    write_u32(0);               // data_size
    // Extension key-value pairs
    write_asciiz("prefix");
    write_asciiz("test_prefix");
    write_asciiz("");           // end of extensions

    // File entry: config.bin
    write_asciiz("config.bin");
    write_u32(0);               // packing_method (uncompressed)
    write_u32(5);               // original_size
    write_u32(0);               // reserved
    write_u32(1000);            // timestamp
    write_u32(5);               // data_size

    // End-of-headers marker (empty filename)
    write_asciiz("");
    write_u32(0);
    write_u32(0);
    write_u32(0);
    write_u32(0);
    write_u32(0);

    // File data
    out.write("hello", 5);

    // Trailing checksum: 0x00 + 20 zero bytes
    out.put('\0');
    char zeros[20] = {};
    out.write(zeros, 20);

    return out.str();
}

} // namespace

TEST(Pbo, ReadBasic) {
    std::string data = build_test_pbo();
    std::istringstream s(data);
    auto p = read(s);

    // Extensions
    ASSERT_EQ(p.extensions.count("prefix"), 1u);
    EXPECT_EQ(p.extensions.at("prefix"), "test_prefix");

    // Entries
    ASSERT_EQ(p.entries.size(), 1u);
    EXPECT_EQ(p.entries[0].filename, "config.bin");
    EXPECT_EQ(p.entries[0].packing_method, 0u);
    EXPECT_EQ(p.entries[0].original_size, 5u);
    EXPECT_EQ(p.entries[0].data_size, 5u);
    EXPECT_EQ(p.entries[0].timestamp, 1000u);

    // Checksum
    ASSERT_EQ(p.checksum.size(), 20u);
}

TEST(Pbo, ExtractFile) {
    std::string data = build_test_pbo();
    std::istringstream s(data);
    auto p = read(s);

    ASSERT_EQ(p.entries.size(), 1u);

    std::ostringstream out;
    extract_file(s, p.entries[0], out);
    EXPECT_EQ(out.str(), "hello");
}
