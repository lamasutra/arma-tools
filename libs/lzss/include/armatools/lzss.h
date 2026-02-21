#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <vector>

namespace armatools::lzss {

// Decompress reads LZSS-compressed data from r and returns exactly
// expected_size bytes of decompressed output. Verifies trailing checksum.
std::vector<uint8_t> decompress(std::istream& r, size_t expected_size);

// decompress_or_raw either decompresses or reads raw bytes depending on
// expected_size. Per BI convention, data smaller than 1024 bytes is stored raw.
std::vector<uint8_t> decompress_or_raw(std::istream& r, size_t expected_size);

// decompress_signed is like decompress but uses a signed additive checksum.
// PAA non-DXT textures use this variant unconditionally (no 1024-byte threshold).
std::vector<uint8_t> decompress_signed(const uint8_t* src, size_t src_len,
                                        size_t expected_size);

// decompress_buf decompresses from a byte buffer with unsigned checksum.
std::vector<uint8_t> decompress_buf(const uint8_t* src, size_t src_len,
                                     size_t expected_size);

// decompress_nochecksum decompresses from a byte buffer without verifying
// the trailing checksum. Used for old PAC palette-indexed LZSS mipmaps.
std::vector<uint8_t> decompress_nochecksum(const uint8_t* src, size_t src_len,
                                            size_t expected_size);

// decompress_buf_auto decompresses from a byte buffer without knowing the
// output size. Decompresses until all input is consumed (last 4 bytes are the
// checksum). Returns empty vector if decompression or checksum validation fails.
std::vector<uint8_t> decompress_buf_auto(const uint8_t* src, size_t src_len);

// --- Compression ---

// compress compresses data using LZSS with unsigned additive checksum.
// This is the standard variant used by PBO files.
std::vector<uint8_t> compress(const uint8_t* data, size_t len);

// compress_signed compresses data using LZSS with signed additive checksum.
// Used by PAA non-DXT textures.
std::vector<uint8_t> compress_signed(const uint8_t* data, size_t len);

// compress_nochecksum compresses data using LZSS without trailing checksum.
std::vector<uint8_t> compress_nochecksum(const uint8_t* data, size_t len);

} // namespace armatools::lzss
