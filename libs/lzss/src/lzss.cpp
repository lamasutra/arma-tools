#include "armatools/lzss.h"
#include "armatools/binutil.h"

#include <cstring>
#include <format>
#include <stdexcept>

namespace armatools::lzss {

static uint8_t read_byte(std::istream& r) {
    char b;
    if (!r.read(&b, 1))
        throw std::runtime_error("lzss: unexpected end of stream");
    return static_cast<uint8_t>(b);
}

std::vector<uint8_t> decompress(std::istream& r, size_t expected_size) {
    std::vector<uint8_t> out(expected_size);
    size_t out_pos = 0;
    uint32_t sum = 0;
    uint32_t flags = 0;

    auto size = static_cast<int64_t>(expected_size);
    while (size > 0) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            flags = static_cast<uint32_t>(read_byte(r)) | 0xff00;
        }

        if ((flags & 0x01) != 0) {
            // Raw data byte
            uint8_t data = read_byte(r);
            sum += data;
            out[out_pos++] = data;
            size--;
        } else {
            // 2-byte pointer: 12-bit rpos + 4-bit rlen
            uint8_t b1 = read_byte(r);
            uint8_t b2 = read_byte(r);

            auto rpos = static_cast<size_t>(b1) | (static_cast<size_t>(b2 & 0xf0) << 4);
            int rlen = (b2 & 0x0f) + 3;

            // Space fill when rpos > out_pos
            while (rpos > out_pos && rlen > 0) {
                sum += 0x20;
                out[out_pos++] = 0x20;
                size--;
                if (size == 0) break;
                rlen--;
            }

            if (size == 0) break;

            // Copy from previously decoded output
            rpos = out_pos - rpos;
            for (; rlen > 0; rlen--) {
                uint8_t data = out[rpos++];
                sum += data;
                out[out_pos++] = data;
                size--;
                if (size == 0) break;
            }
        }
    }

    // Read and verify 4-byte checksum
    uint32_t checksum = binutil::read_u32(r);
    if (checksum != sum) {
        throw std::runtime_error(
            std::format("lzss: checksum mismatch: expected {:#010x}, got {:#010x}",
                        checksum, sum));
    }

    return out;
}

std::vector<uint8_t> decompress_or_raw(std::istream& r, size_t expected_size) {
    if (expected_size < 1024) {
        return binutil::read_bytes(r, expected_size);
    }
    return decompress(r, expected_size);
}

// Core buffer-based LZSS decompression shared by signed/unsigned variants.
static std::vector<uint8_t> decompress_buf_core(const uint8_t* src, size_t src_len,
                                                  size_t expected_size,
                                                  bool signed_checksum,
                                                  bool verify_checksum = true) {
    std::vector<uint8_t> out(expected_size);
    size_t out_pos = 0;
    size_t ip = 0;
    uint32_t sum = 0;
    uint32_t flags = 0;

    auto remaining = static_cast<int64_t>(expected_size);

    auto next_byte = [&]() -> uint8_t {
        if (ip >= src_len) throw std::runtime_error("lzss: input overrun");
        return src[ip++];
    };

    while (remaining > 0) {
        flags >>= 1;
        if ((flags & 0x100) == 0)
            flags = static_cast<uint32_t>(next_byte()) | 0xff00;

        if ((flags & 0x01) != 0) {
            uint8_t data = next_byte();
            if (signed_checksum)
                sum += static_cast<uint32_t>(static_cast<int8_t>(data));
            else
                sum += data;
            out[out_pos++] = data;
            remaining--;
        } else {
            uint8_t b1 = next_byte();
            uint8_t b2 = next_byte();

            auto rpos = static_cast<size_t>(b1) | (static_cast<size_t>(b2 & 0xf0) << 4);
            int rlen = (b2 & 0x0f) + 3;

            while (rpos > out_pos && rlen > 0) {
                if (signed_checksum)
                    sum += static_cast<uint32_t>(static_cast<int8_t>(0x20));
                else
                    sum += 0x20;
                out[out_pos++] = 0x20;
                remaining--;
                if (remaining == 0) break;
                rlen--;
            }
            if (remaining == 0) break;

            rpos = out_pos - rpos;
            for (; rlen > 0; rlen--) {
                uint8_t data = out[rpos++];
                if (signed_checksum)
                    sum += static_cast<uint32_t>(static_cast<int8_t>(data));
                else
                    sum += data;
                out[out_pos++] = data;
                remaining--;
                if (remaining == 0) break;
            }
        }
    }

    // Verify checksum (4 bytes after compressed data)
    if (verify_checksum) {
        if (ip + 4 > src_len) {
            throw std::runtime_error(
                "lzss: truncated data — missing trailing checksum bytes");
        }
        uint32_t checksum;
        std::memcpy(&checksum, src + ip, 4);
        if (checksum != sum) {
            throw std::runtime_error(
                std::format("lzss: checksum mismatch: expected {:#010x}, got {:#010x}",
                            checksum, sum));
        }
    }

    return out;
}

std::vector<uint8_t> decompress_signed(const uint8_t* src, size_t src_len,
                                        size_t expected_size) {
    return decompress_buf_core(src, src_len, expected_size, true);
}

std::vector<uint8_t> decompress_buf(const uint8_t* src, size_t src_len,
                                     size_t expected_size) {
    return decompress_buf_core(src, src_len, expected_size, false);
}

std::vector<uint8_t> decompress_nochecksum(const uint8_t* src, size_t src_len,
                                            size_t expected_size) {
    return decompress_buf_core(src, src_len, expected_size, false, false);
}

std::vector<uint8_t> decompress_buf_auto(const uint8_t* src, size_t src_len) {
    if (src_len < 5) return {}; // Need at least 1 byte data + 4 bytes checksum

    std::vector<uint8_t> out;
    out.reserve(src_len * 2); // Reasonable initial guess
    size_t ip = 0;
    uint32_t sum = 0;
    uint32_t flags = 0;

    // Decompress until we've consumed all input except the last 4 bytes (checksum)
    size_t data_end = src_len - 4;

    while (ip < data_end) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (ip >= data_end) break;
            flags = static_cast<uint32_t>(src[ip++]) | 0xff00;
        }

        if ((flags & 0x01) != 0) {
            // Literal byte
            if (ip >= data_end) break;
            uint8_t data = src[ip++];
            sum += data;
            out.push_back(data);
        } else {
            // Back-reference: 2-byte pointer
            if (ip + 1 >= data_end) break;
            uint8_t b1 = src[ip++];
            uint8_t b2 = src[ip++];

            auto rpos = static_cast<size_t>(b1) | (static_cast<size_t>(b2 & 0xf0) << 4);
            int rlen = (b2 & 0x0f) + 3;

            // Space fill when rpos > out.size()
            while (rpos > out.size() && rlen > 0) {
                sum += 0x20;
                out.push_back(0x20);
                rlen--;
            }

            // Copy from previously decoded output
            rpos = out.size() - rpos;
            for (; rlen > 0; rlen--) {
                uint8_t data = out[rpos++];
                sum += data;
                out.push_back(data);
            }
        }
    }

    // Verify checksum
    uint32_t checksum;
    std::memcpy(&checksum, src + data_end, 4);
    if (checksum != sum)
        return {};

    return out;
}

// --- Compression ---

// LZSS constants matching the decompressor.
static constexpr size_t N = 4096;       // Ring buffer / max back-reference distance
static constexpr int F = 18;            // Max match length
static constexpr int THRESHOLD = 2;     // Min match length is THRESHOLD + 1 = 3

// Find the longest match in the output buffer for data[pos..].
// Returns (distance_from_pos, length). distance is in [1, min(pos, N)].
// The decompressor encodes offset as (pos - distance), i.e. the absolute
// position in the output stream, clamped to 12 bits (0..4095).
// When rpos > out_pos during decompression, space (0x20) fill is used.
// We only search actual output bytes, so we never produce such references.
static std::pair<size_t, size_t> find_match(const uint8_t* data, size_t len, size_t pos) {
    size_t best_dist = 0;
    size_t best_len = 0;

    // Max distance is N-1 (4095) since the 12-bit rpos field holds 0..4095,
    // and dist=0 is invalid (would mean copy from current position).
    size_t max_dist = std::min(pos, N - 1);
    size_t max_len = std::min(len - pos, static_cast<size_t>(F));

    if (max_len < THRESHOLD + 1)
        return {0, 0};

    // Search backward through the window for matches.
    for (size_t dist = 1; dist <= max_dist; dist++) {
        size_t match_start = pos - dist;
        size_t match_len = 0;

        // Allow overlapping matches (dist < F): the decompressor copies
        // byte-by-byte from rpos, so repeated patterns work.
        while (match_len < max_len) {
            if (data[match_start + (match_len % dist)] != data[pos + match_len])
                break;
            match_len++;
        }

        if (match_len > best_len) {
            best_len = match_len;
            best_dist = dist;
            if (best_len == max_len)
                break; // Can't do better
        }
    }

    if (best_len < THRESHOLD + 1)
        return {0, 0};

    return {best_dist, best_len};
}

// Core compression. Checksum mode: 0=unsigned, 1=signed, 2=none.
static std::vector<uint8_t> compress_core(const uint8_t* data, size_t len,
                                           int checksum_mode) {
    // Worst case: every byte is a literal -> len bytes + len/8 flag bytes + 4 checksum.
    std::vector<uint8_t> out;
    out.reserve(len + len / 8 + 8);

    uint32_t sum = 0;
    size_t pos = 0;

    while (pos < len) {
        // Reserve space for the flag byte.
        size_t flag_pos = out.size();
        out.push_back(0);
        uint8_t flags = 0;

        // Process up to 8 items per flag byte.
        for (int bit = 0; bit < 8 && pos < len; bit++) {
            auto [dist, match_len] = find_match(data, len, pos);

            if (match_len >= static_cast<size_t>(THRESHOLD + 1)) {
                // Back-reference: encode dist as 12-bit rpos, (match_len-3) as 4-bit rlen.
                // The decompressor does: source = out_pos - encoded_rpos, so encoded_rpos = dist.
                auto rpos_enc = static_cast<uint16_t>(dist) & 0xFFF;
                auto rlen_enc = static_cast<uint8_t>(match_len - 3);

                uint8_t b1 = static_cast<uint8_t>(rpos_enc & 0xFF);
                uint8_t b2 = static_cast<uint8_t>(((rpos_enc >> 4) & 0xF0) | rlen_enc);

                out.push_back(b1);
                out.push_back(b2);

                // Add uncompressed bytes to checksum.
                for (size_t i = 0; i < match_len; i++) {
                    if (checksum_mode == 1)
                        sum += static_cast<uint32_t>(static_cast<int8_t>(data[pos + i]));
                    else
                        sum += data[pos + i];
                }

                pos += match_len;
                // Flag bit stays 0 (back-reference).
            } else {
                // Literal byte.
                uint8_t byte = data[pos];
                if (checksum_mode == 1)
                    sum += static_cast<uint32_t>(static_cast<int8_t>(byte));
                else
                    sum += byte;

                out.push_back(byte);
                flags |= (1 << bit);
                pos++;
            }
        }

        // Write the flag byte.
        out[flag_pos] = flags;
    }

    // Append checksum.
    if (checksum_mode != 2) {
        out.push_back(static_cast<uint8_t>(sum & 0xFF));
        out.push_back(static_cast<uint8_t>((sum >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((sum >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((sum >> 24) & 0xFF));
    }

    return out;
}

std::vector<uint8_t> compress(const uint8_t* data, size_t len) {
    return compress_core(data, len, 0);
}

std::vector<uint8_t> compress_signed(const uint8_t* data, size_t len) {
    return compress_core(data, len, 1);
}

std::vector<uint8_t> compress_nochecksum(const uint8_t* data, size_t len) {
    return compress_core(data, len, 2);
}

} // namespace armatools::lzss
