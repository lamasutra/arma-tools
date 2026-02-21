#include "armatools/pbo.h"
#include "armatools/binutil.h"
#include "armatools/lzss.h"

#include <format>
#include <stdexcept>

namespace armatools::pbo {

PBO read(std::istream& r) {
    std::unordered_map<std::string, std::string> extensions;
    std::vector<Entry> entries;
    bool first = true;

    for (;;) {
        std::string filename = binutil::read_asciiz(r);
        uint32_t packing_method = binutil::read_u32(r);
        uint32_t original_size = binutil::read_u32(r);
        uint32_t reserved = binutil::read_u32(r);
        uint32_t timestamp = binutil::read_u32(r);
        uint32_t data_size = binutil::read_u32(r);

        if (packing_method == 0x56657273) {
            // Extension header (packing_method == "Vers")
            if (!first)
                throw std::runtime_error("pbo: extension header not in first position");
            for (;;) {
                std::string key = binutil::read_asciiz(r);
                if (key.empty()) break;
                std::string val = binutil::read_asciiz(r);
                extensions[key] = val;
            }
        } else if (filename.empty()) {
            break;
        } else {
            entries.push_back(Entry{
                .filename = filename,
                .packing_method = packing_method,
                .original_size = original_size,
                .reserved = reserved,
                .timestamp = timestamp,
                .data_size = data_size,
            });
        }

        first = false;
    }

    // Current position is the start of file data.
    auto data_start = r.tellg();
    if (data_start == std::istream::pos_type(-1))
        throw std::runtime_error("pbo: failed to get data start position");

    int64_t offset = static_cast<int64_t>(data_start);
    for (auto& e : entries) {
        e.data_offset = offset;
        offset += static_cast<int64_t>(e.data_size);
    }

    // Seek to end of all file data
    r.seekg(offset);
    if (!r)
        throw std::runtime_error("pbo: failed to seek past file data");

    // Try to read trailing 0x00 byte + 20-byte SHA1 checksum.
    std::vector<uint8_t> checksum;
    char zero;
    if (r.read(&zero, 1)) {
        std::vector<uint8_t> buf(20);
        if (r.read(reinterpret_cast<char*>(buf.data()), 20)) {
            checksum = std::move(buf);
        }
    }
    // Clear any eof/fail bits from optional checksum read
    r.clear();

    return PBO{
        .extensions = std::move(extensions),
        .entries = std::move(entries),
        .checksum = std::move(checksum),
    };
}

void extract_file(std::istream& r, const Entry& entry, std::ostream& w) {
    r.seekg(entry.data_offset);
    if (!r)
        throw std::runtime_error(
            std::format("pbo: failed to seek to {} at offset {}",
                        entry.filename, entry.data_offset));

    // OFP-era PBOs can have LZSS-compressed entries (packing_method != 0).
    if (entry.packing_method != 0 && entry.original_size > 0 &&
        entry.data_size != entry.original_size) {
        // Read compressed data, then decompress via LZSS
        std::vector<uint8_t> compressed(entry.data_size);
        if (!r.read(reinterpret_cast<char*>(compressed.data()),
                    static_cast<std::streamsize>(entry.data_size)))
            throw std::runtime_error(
                std::format("pbo: failed to read compressed {}", entry.filename));
        auto decompressed = lzss::decompress_buf(
            compressed.data(), compressed.size(), entry.original_size);
        if (!w.write(reinterpret_cast<const char*>(decompressed.data()),
                     static_cast<std::streamsize>(decompressed.size())))
            throw std::runtime_error(
                std::format("pbo: failed to write {}", entry.filename));
        return;
    }

    constexpr size_t buf_size = 8192;
    char buf[buf_size];
    auto remaining = static_cast<size_t>(entry.data_size);

    while (remaining > 0) {
        size_t to_read = std::min(remaining, buf_size);
        if (!r.read(buf, static_cast<std::streamsize>(to_read)))
            throw std::runtime_error(
                std::format("pbo: failed to extract {}", entry.filename));
        if (!w.write(buf, static_cast<std::streamsize>(to_read)))
            throw std::runtime_error(
                std::format("pbo: failed to write {}", entry.filename));
        remaining -= to_read;
    }
}

} // namespace armatools::pbo
