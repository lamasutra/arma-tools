#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <vector>

namespace armatools::lzo {

// Decompress reads LZO1X-1 compressed data from r and returns exactly
// expected_size bytes of decompressed output.
std::vector<uint8_t> decompress(std::istream& r, size_t expected_size);

// decompress_or_raw either decompresses or reads raw bytes depending on
// expected_size. Per BI convention, data smaller than 1024 bytes is stored raw.
std::vector<uint8_t> decompress_or_raw(std::istream& r, size_t expected_size);

} // namespace armatools::lzo
