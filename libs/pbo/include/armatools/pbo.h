#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace armatools::pbo {

struct Entry {
    std::string filename;
    uint32_t packing_method = 0;
    uint32_t original_size = 0;
    uint32_t reserved = 0;
    uint32_t timestamp = 0;
    uint32_t data_size = 0;
    int64_t data_offset = 0;
};

struct PBO {
    std::unordered_map<std::string, std::string> extensions;
    std::vector<Entry> entries;
    std::vector<uint8_t> checksum; // 20-byte SHA1, may be empty for OFP-era PBOs
};

// Read parses PBO headers, extension properties, and the trailing checksum.
// The stream must support seekg (e.g. std::ifstream or std::istringstream).
PBO read(std::istream& r);

// extract_file extracts a single PBO entry's data to the given writer.
void extract_file(std::istream& r, const Entry& entry, std::ostream& w);

} // namespace armatools::pbo
