#include "armatools/paa.h"
#include "armatools/binutil.h"
#include "armatools/lzss.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

namespace armatools::paa {

// --- Format name mapping ---

std::string format_name(uint16_t tag) {
    switch (tag) {
        case 0xFF01: return "DXT1";
        case 0xFF02: return "DXT2";
        case 0xFF03: return "DXT3";
        case 0xFF04: return "DXT4";
        case 0xFF05: return "DXT5";
        case 0x4444: return "ARGB4444";
        case 0x1555: return "ARGB1555";
        case 0x8080: return "AI88";
        case 0x8888: return "ARGB8888";
        default: return "";
    }
}

static uint16_t format_tag(const std::string& name) {
    if (name == "DXT1") return 0xFF01;
    if (name == "DXT3") return 0xFF03;
    if (name == "DXT5") return 0xFF05;
    return 0;
}

// --- DXT helpers ---

struct RGB { uint8_t r, g, b; };

static RGB rgb565(uint16_t c) {
    uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1F);
    uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3F);
    uint8_t b5 = static_cast<uint8_t>(c & 0x1F);
    return {static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<uint8_t>((b5 << 3) | (b5 >> 2))};
}

static uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) & 0x1F) << 11) |
           static_cast<uint16_t>(((g >> 2) & 0x3F) << 5) |
           static_cast<uint16_t>((b >> 3) & 0x1F);
}

static uint16_t get_u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
static uint32_t get_u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

// Decode DXT1 block -> 16 RGBA pixels
using Pixel4 = std::array<uint8_t, 4>;
static std::array<Pixel4, 16> decode_dxt1_block(const uint8_t* block) {
    uint16_t c0 = get_u16(block), c1 = get_u16(block + 2);
    auto [r0, g0, b0] = rgb565(c0);
    auto [r1, g1, b1] = rgb565(c1);

    std::array<Pixel4, 4> colors;
    colors[0] = {r0, g0, b0, 255};
    colors[1] = {r1, g1, b1, 255};
    if (c0 > c1) {
        colors[2] = {static_cast<uint8_t>((2u*r0 + r1) / 3),
                     static_cast<uint8_t>((2u*g0 + g1) / 3),
                     static_cast<uint8_t>((2u*b0 + b1) / 3), 255};
        colors[3] = {static_cast<uint8_t>((r0 + 2u*r1) / 3),
                     static_cast<uint8_t>((g0 + 2u*g1) / 3),
                     static_cast<uint8_t>((b0 + 2u*b1) / 3), 255};
    } else {
        colors[2] = {static_cast<uint8_t>((r0 + r1) / 2u),
                     static_cast<uint8_t>((g0 + g1) / 2u),
                     static_cast<uint8_t>((b0 + b1) / 2u), 255};
        colors[3] = {0, 0, 0, 0};
    }

    uint32_t indices = get_u32(block + 4);
    std::array<Pixel4, 16> pixels;
    for (size_t i = 0; i < 16; i++)
        pixels[i] = colors[(indices >> (i * 2)) & 3];
    return pixels;
}

// Decode DXT5 color block -> 16 RGB pixels
using Pixel3 = std::array<uint8_t, 3>;
static std::array<Pixel3, 16> decode_dxt5_color_block(const uint8_t* block) {
    uint16_t c0 = get_u16(block), c1 = get_u16(block + 2);
    auto [r0, g0, b0] = rgb565(c0);
    auto [r1, g1, b1] = rgb565(c1);

    std::array<Pixel3, 4> colors;
    colors[0] = {r0, g0, b0};
    colors[1] = {r1, g1, b1};
    colors[2] = {static_cast<uint8_t>((2u*r0 + r1) / 3),
                 static_cast<uint8_t>((2u*g0 + g1) / 3),
                 static_cast<uint8_t>((2u*b0 + b1) / 3)};
    colors[3] = {static_cast<uint8_t>((r0 + 2u*r1) / 3),
                 static_cast<uint8_t>((g0 + 2u*g1) / 3),
                 static_cast<uint8_t>((b0 + 2u*b1) / 3)};

    uint32_t indices = get_u32(block + 4);
    std::array<Pixel3, 16> pixels;
    for (size_t i = 0; i < 16; i++)
        pixels[i] = colors[(indices >> (i * 2)) & 3];
    return pixels;
}

static std::array<uint8_t, 16> decode_dxt5_alpha(const uint8_t* block) {
    uint16_t a0 = block[0], a1 = block[1];
    std::array<uint8_t, 8> alphas;
    alphas[0] = static_cast<uint8_t>(a0);
    alphas[1] = static_cast<uint8_t>(a1);
    if (a0 > a1) {
        alphas[2] = static_cast<uint8_t>((6*a0 + 1*a1) / 7);
        alphas[3] = static_cast<uint8_t>((5*a0 + 2*a1) / 7);
        alphas[4] = static_cast<uint8_t>((4*a0 + 3*a1) / 7);
        alphas[5] = static_cast<uint8_t>((3*a0 + 4*a1) / 7);
        alphas[6] = static_cast<uint8_t>((2*a0 + 5*a1) / 7);
        alphas[7] = static_cast<uint8_t>((1*a0 + 6*a1) / 7);
    } else {
        alphas[2] = static_cast<uint8_t>((4*a0 + 1*a1) / 5);
        alphas[3] = static_cast<uint8_t>((3*a0 + 2*a1) / 5);
        alphas[4] = static_cast<uint8_t>((2*a0 + 3*a1) / 5);
        alphas[5] = static_cast<uint8_t>((1*a0 + 4*a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
    }
    uint64_t bits = static_cast<uint64_t>(block[2]) | (static_cast<uint64_t>(block[3]) << 8) |
                    (static_cast<uint64_t>(block[4]) << 16) | (static_cast<uint64_t>(block[5]) << 24) |
                    (static_cast<uint64_t>(block[6]) << 32) | (static_cast<uint64_t>(block[7]) << 40);
    std::array<uint8_t, 16> result;
    for (size_t i = 0; i < 16; i++)
        result[i] = alphas[(bits >> (i * 3)) & 7];
    return result;
}

static std::array<uint8_t, 16> decode_dxt3_alpha(const uint8_t* block) {
    std::array<uint8_t, 16> result;
    for (size_t i = 0; i < 16; i++) {
        size_t byte_idx = i / 2;
        if (i % 2 == 0)
            result[i] = static_cast<uint8_t>((block[byte_idx] & 0x0F) * 17);
        else
            result[i] = static_cast<uint8_t>((block[byte_idx] >> 4) * 17);
    }
    return result;
}

// --- LZO buffer decompression (PAA-specific) ---

static std::vector<uint8_t> lzo_decompress(const std::vector<uint8_t>& src, int expected_size) {
    if (src.empty()) throw std::runtime_error("lzo: empty input");

    std::vector<uint8_t> dst;
    dst.reserve(static_cast<size_t>(expected_size));
    size_t ip = 0;
    int state = 0;

    auto read_byte = [&]() -> uint8_t {
        if (ip >= src.size()) throw std::runtime_error("lzo: input overrun");
        return src[ip++];
    };

    auto read_u16le = [&]() -> uint16_t {
        if (ip + 2 > src.size()) throw std::runtime_error("lzo: input overrun reading u16");
        uint16_t v = static_cast<uint16_t>(src[ip]) | (static_cast<uint16_t>(src[ip+1]) << 8);
        ip += 2;
        return v;
    };

    auto copy_literals = [&](int n) {
        if (ip + static_cast<size_t>(n) > src.size()) throw std::runtime_error("lzo: input overrun copying literals");
        dst.insert(dst.end(), src.begin() + static_cast<ptrdiff_t>(ip),
                   src.begin() + static_cast<ptrdiff_t>(ip) + n);
        ip += static_cast<size_t>(n);
    };

    auto consume_zero_bytes = [&]() -> int {
        int count = 0;
        while (true) {
            uint8_t b = read_byte();
            if (b != 0) { ip--; return count; }
            count++;
        }
    };

    auto copy_from_dict = [&](int dist, int length) {
        int pos = static_cast<int>(dst.size()) - dist;
        if (pos < 0) throw std::runtime_error("lzo: lookbehind overrun");
        for (int i = 0; i < length; i++)
            dst.push_back(dst[static_cast<size_t>(pos + i)]);
    };

    // First byte encoding
    if (src[ip] >= 22) {
        int n = read_byte() - 17;
        copy_literals(n);
        state = 4;
    } else if (src[ip] >= 18) {
        int n_state = read_byte() - 17;
        state = n_state;
        if (n_state > 0) copy_literals(n_state);
    }

    int lblen = 0;
    for (;;) {
        uint8_t inst = read_byte();
        int n_state = 0;
        int lbcur = 0;

        if (inst & 0xC0) {
            uint8_t h = read_byte();
            int dist = (static_cast<int>(h) << 3) + ((inst >> 2) & 0x07) + 1;
            lblen = (inst >> 5) + 1;
            lbcur = static_cast<int>(dst.size()) - dist;
            n_state = inst & 0x3;
        } else if (inst & 0x20) {
            lblen = (inst & 0x1f) + 2;
            if (lblen == 2) {
                int zeros = consume_zero_bytes();
                uint8_t b = read_byte();
                lblen += zeros * 255 + 31 + b;
            }
            uint16_t v = read_u16le();
            n_state = v & 0x3;
            int dist = (v >> 2) + 1;
            lbcur = static_cast<int>(dst.size()) - dist;
        } else if (inst & 0x10) {
            lblen = (inst & 0x7) + 2;
            if (lblen == 2) {
                int zeros = consume_zero_bytes();
                uint8_t b = read_byte();
                lblen += zeros * 255 + 7 + b;
            }
            uint16_t v = read_u16le();
            n_state = v & 0x3;
            int dist = static_cast<int>((inst & 0x8) << 11) + (v >> 2);
            if (dist == 0) return dst; // End of stream
            dist += 16384;
            lbcur = static_cast<int>(dst.size()) - dist;
        } else if (state == 0) {
            int n = inst + 3;
            if (n == 3) {
                int zeros = consume_zero_bytes();
                uint8_t b = read_byte();
                n += zeros * 255 + 15 + b;
            }
            copy_literals(n);
            state = 4;
            continue;
        } else if (state != 4) {
            uint8_t h = read_byte();
            int dist = (inst >> 2) + (static_cast<int>(h) << 2) + 1;
            lbcur = static_cast<int>(dst.size()) - dist;
            lblen = 2;
            n_state = inst & 0x3;
        } else {
            uint8_t h = read_byte();
            int dist = (inst >> 2) + (static_cast<int>(h) << 2) + 2049;
            lbcur = static_cast<int>(dst.size()) - dist;
            lblen = 3;
            n_state = inst & 0x3;
        }

        if (lbcur < 0) throw std::runtime_error("lzo: lookbehind overrun");
        copy_from_dict(static_cast<int>(dst.size()) - lbcur, lblen);
        state = n_state;
        if (n_state > 0) copy_literals(n_state);
    }
}

// --- Expected pixel data size ---

static int expected_pixel_size(const std::string& fmt, int w, int h) {
    if (fmt == "DXT1") return std::max(1, w/4) * std::max(1, h/4) * 8;
    if (fmt == "DXT2" || fmt == "DXT3" || fmt == "DXT4" || fmt == "DXT5")
        return std::max(1, w/4) * std::max(1, h/4) * 16;
    if (fmt == "ARGB4444" || fmt == "ARGB1555" || fmt == "AI88") return w * h * 2;
    if (fmt == "ARGB8888") return w * h * 4;
    if (fmt == "INDEX") return w * h;
    return w * h * 4;
}

static bool is_dxt_format(const std::string& fmt) {
    return fmt == "DXT1" || fmt == "DXT2" || fmt == "DXT3" ||
           fmt == "DXT4" || fmt == "DXT5";
}

// --- RLE decompression for OFP CWC/Demo palette-indexed textures ---

static std::vector<uint8_t> rle_decompress(const uint8_t* src, size_t src_len,
                                            size_t expected_size) {
    std::vector<uint8_t> out;
    out.reserve(expected_size);
    size_t ip = 0;

    while (out.size() < expected_size && ip < src_len) {
        uint8_t flag = src[ip++];
        if (flag & 0x80) {
            // Repeat next byte (flag - 0x80 + 1) times
            int count = (flag - 0x80) + 1;
            if (ip >= src_len) break;
            uint8_t val = src[ip++];
            for (int i = 0; i < count && out.size() < expected_size; i++)
                out.push_back(val);
        } else {
            // Literal run of (flag + 1) bytes
            int count = flag + 1;
            for (int i = 0; i < count && ip < src_len && out.size() < expected_size; i++)
                out.push_back(src[ip++]);
        }
    }

    out.resize(expected_size, 0);
    return out;
}

// --- Skip TAGGs helper ---

static bool peek_is_tagg(std::istream& r) {
    char sig[4];
    if (!r.read(sig, 4)) {
        r.clear();
        r.seekg(-r.gcount(), std::ios::cur);
        return false;
    }
    r.seekg(-4, std::ios::cur);
    return sig[0] == 'G' && sig[1] == 'G' && sig[2] == 'A' && sig[3] == 'T';
}

static void skip_taggs(std::istream& r) {
    while (peek_is_tagg(r)) {
        r.seekg(8, std::ios::cur); // skip 8-byte signature ("GGAT" + name)
        uint32_t data_len = binutil::read_u32(r);
        r.seekg(static_cast<std::streamoff>(data_len), std::ios::cur);
    }
}

// --- Decode pixels ---

static void decode_dxt1_image(const uint8_t* data, size_t data_len, Image& img) {
    int bw = std::max(1, img.width / 4), bh = std::max(1, img.height / 4);
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            size_t idx = static_cast<size_t>(by * bw + bx) * 8;
            if (idx + 8 > data_len) return;
            auto pixels = decode_dxt1_block(data + idx);
            for (int py = 0; py < 4; py++)
                for (int px = 0; px < 4; px++) {
                    int x = bx*4+px, y = by*4+py;
                    if (x < img.width && y < img.height) {
                        auto& c = pixels[static_cast<size_t>(py*4+px)];
                        img.set(x, y, c[0], c[1], c[2], c[3]);
                    }
                }
        }
    }
}

static void decode_dxt3_image(const uint8_t* data, size_t data_len, Image& img) {
    int bw = std::max(1, img.width / 4), bh = std::max(1, img.height / 4);
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            size_t idx = static_cast<size_t>(by * bw + bx) * 16;
            if (idx + 16 > data_len) return;
            auto alphas = decode_dxt3_alpha(data + idx);
            auto colors = decode_dxt5_color_block(data + idx + 8);
            for (int py = 0; py < 4; py++)
                for (int px = 0; px < 4; px++) {
                    int x = bx*4+px, y = by*4+py;
                    if (x < img.width && y < img.height) {
                        size_t i = static_cast<size_t>(py*4+px);
                        img.set(x, y, colors[i][0], colors[i][1], colors[i][2], alphas[i]);
                    }
                }
        }
    }
}

static void decode_dxt5_image(const uint8_t* data, size_t data_len, Image& img) {
    int bw = std::max(1, img.width / 4), bh = std::max(1, img.height / 4);
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            size_t idx = static_cast<size_t>(by * bw + bx) * 16;
            if (idx + 16 > data_len) return;
            auto alphas = decode_dxt5_alpha(data + idx);
            auto colors = decode_dxt5_color_block(data + idx + 8);
            for (int py = 0; py < 4; py++)
                for (int px = 0; px < 4; px++) {
                    int x = bx*4+px, y = by*4+py;
                    if (x < img.width && y < img.height) {
                        size_t i = static_cast<size_t>(py*4+px);
                        img.set(x, y, colors[i][0], colors[i][1], colors[i][2], alphas[i]);
                    }
                }
        }
    }
}

static void decode_argb4444(const uint8_t* data, size_t data_len, Image& img) {
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            size_t off = static_cast<size_t>(y * img.width + x) * 2;
            if (off + 2 > data_len) return;
            uint16_t v = get_u16(data + off);
            img.set(x, y,
                static_cast<uint8_t>(((v >> 8) & 0xF) * 17),
                static_cast<uint8_t>(((v >> 4) & 0xF) * 17),
                static_cast<uint8_t>((v & 0xF) * 17),
                static_cast<uint8_t>(((v >> 12) & 0xF) * 17));
        }
}

static void decode_argb1555(const uint8_t* data, size_t data_len, Image& img) {
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            size_t off = static_cast<size_t>(y * img.width + x) * 2;
            if (off + 2 > data_len) return;
            uint16_t v = get_u16(data + off);
            uint8_t a = (v & 0x8000) ? 255 : 0;
            uint8_t r5 = static_cast<uint8_t>((v >> 10) & 0x1F);
            uint8_t g5 = static_cast<uint8_t>((v >> 5) & 0x1F);
            uint8_t b5 = static_cast<uint8_t>(v & 0x1F);
            img.set(x, y, static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                          static_cast<uint8_t>((g5 << 3) | (g5 >> 2)),
                          static_cast<uint8_t>((b5 << 3) | (b5 >> 2)), a);
        }
}

static void decode_ai88(const uint8_t* data, size_t data_len, Image& img) {
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            size_t off = static_cast<size_t>(y * img.width + x) * 2;
            if (off + 2 > data_len) return;
            img.set(x, y, data[off], data[off], data[off], data[off+1]);
        }
}

static void decode_argb8888(const uint8_t* data, size_t data_len, Image& img) {
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            size_t off = static_cast<size_t>(y * img.width + x) * 4;
            if (off + 4 > data_len) return;
            img.set(x, y, data[off+2], data[off+1], data[off], data[off+3]);
        }
}

// Palette is stored as BGR triplets. Convert indexed pixels to RGBA.
static void decode_indexed(const uint8_t* data, size_t data_len,
                            const std::vector<uint8_t>& palette, int n_palette,
                            Image& img) {
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            size_t off = static_cast<size_t>(y * img.width + x);
            if (off >= data_len) return;
            int idx = data[off];
            if (idx < n_palette) {
                // Palette entries are BGR
                uint8_t b = palette[static_cast<size_t>(idx) * 3];
                uint8_t g = palette[static_cast<size_t>(idx) * 3 + 1];
                uint8_t r = palette[static_cast<size_t>(idx) * 3 + 2];
                img.set(x, y, r, g, b, 255);
            } else {
                img.set(x, y, 0, 0, 0, 255);
            }
        }
}

static void decode_pixels(const std::string& fmt, const uint8_t* data, size_t data_len, Image& img) {
    if (fmt == "DXT1") decode_dxt1_image(data, data_len, img);
    else if (fmt == "DXT2" || fmt == "DXT3") decode_dxt3_image(data, data_len, img);
    else if (fmt == "DXT4" || fmt == "DXT5") decode_dxt5_image(data, data_len, img);
    else if (fmt == "ARGB4444") decode_argb4444(data, data_len, img);
    else if (fmt == "ARGB1555") decode_argb1555(data, data_len, img);
    else if (fmt == "AI88") decode_ai88(data, data_len, img);
    else if (fmt == "ARGB8888") decode_argb8888(data, data_len, img);
    else throw std::runtime_error(std::format("paa: unsupported format {}", fmt));
}

// --- Public API ---

Header read_header(std::istream& r) {
    uint16_t type_tag = binutil::read_u16(r);
    std::string fmt = format_name(type_tag);

    if (fmt.empty()) {
        // Old OFP palette-indexed: no type tag, file starts with TAGG or palette.
        // The two bytes we read are the start of TAGG signature ("GG" = 0x4747)
        // or the palette count (OFP Demo).
        r.seekg(-2, std::ios::cur);
        fmt = "INDEX";

        // Check if there are TAGGs (OFP CWC/Resistance) or not (OFP Demo)
        uint8_t peek = binutil::read_u8(r);
        r.seekg(-1, std::ios::cur);
        if (peek >= 0x20) {
            // Has TAGGs
            skip_taggs(r);
        }
    } else {
        skip_taggs(r);
    }

    uint16_t n_palette = binutil::read_u16(r);
    // Palette entries are BGR triplets (3 bytes each)
    if (n_palette > 0) r.seekg(static_cast<std::streamoff>(n_palette) * 3, std::ios::cur);

    uint16_t width_raw = binutil::read_u16(r);
    uint16_t height_raw = binutil::read_u16(r);

    int w = width_raw & 0x7FFF;
    int h = height_raw;

    // For palette-indexed, check for 1234x8765 LZSS signature
    if (fmt == "INDEX" && width_raw == 0x04D2 && height_raw == 0x223D) {
        w = binutil::read_u16(r);
        h = binutil::read_u16(r);
    }

    return {fmt, w, h};
}

static uint32_t read_u24(std::istream& r) {
    uint8_t buf[3];
    if (!r.read(reinterpret_cast<char*>(buf), 3))
        throw std::runtime_error("paa: failed to read u24");
    return static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
           (static_cast<uint32_t>(buf[2]) << 16);
}

std::pair<Image, Header> decode(std::istream& r) {
    uint16_t type_tag = binutil::read_u16(r);
    std::string fmt = format_name(type_tag);
    bool is_index_palette = false;

    if (fmt.empty()) {
        // Old OFP palette-indexed: no type tag.
        r.seekg(-2, std::ios::cur);
        fmt = "INDEX";
        is_index_palette = true;

        uint8_t peek = binutil::read_u8(r);
        r.seekg(-1, std::ios::cur);
        if (peek >= 0x20) {
            skip_taggs(r);
        }
    } else {
        skip_taggs(r);
    }

    // Read palette
    uint16_t n_palette = binutil::read_u16(r);
    std::vector<uint8_t> palette_data;
    if (n_palette > 0) {
        // Palette entries are BGR triplets (3 bytes each)
        palette_data = binutil::read_bytes(r, static_cast<size_t>(n_palette) * 3);
    }

    // Read first mipmap header
    uint16_t width_raw = binutil::read_u16(r);
    uint16_t height_raw = binutil::read_u16(r);

    // For palette-indexed, check for 1234x8765 LZSS signature
    bool palette_lzss = false;
    if (is_index_palette && width_raw == 0x04D2 && height_raw == 0x223D) {
        palette_lzss = true;
        width_raw = binutil::read_u16(r);
        height_raw = binutil::read_u16(r);
    }

    bool lzo_compressed = (width_raw & 0x8000) != 0;
    int w = width_raw & 0x7FFF;
    int h = height_raw;

    uint32_t data_size = read_u24(r);
    auto data = binutil::read_bytes(r, data_size);

    Header hdr{fmt, w, h};

    std::vector<uint8_t> pixels;
    if (is_index_palette) {
        // Palette-indexed: LZSS or RLE decompression
        int expected = w * h;
        if (palette_lzss) {
            pixels = lzss::decompress_nochecksum(data.data(), data.size(),
                                                  static_cast<size_t>(expected));
        } else {
            pixels = rle_decompress(data.data(), data.size(),
                                     static_cast<size_t>(expected));
        }
    } else if (is_dxt_format(fmt)) {
        // DXT: LZO compression when bit 15 of width is set
        if (lzo_compressed) {
            int expected = expected_pixel_size(fmt, w, h);
            pixels = lzo_decompress(data, expected);
        } else {
            pixels = std::move(data);
        }
    } else {
        // Non-DXT (ARGB4444, ARGB1555, AI88, ARGB8888):
        // Always LZSS-compressed with signed checksum, no 1024-byte threshold
        int expected = expected_pixel_size(fmt, w, h);
        if (static_cast<int>(data.size()) < expected) {
            pixels = lzss::decompress_signed(data.data(), data.size(),
                                              static_cast<size_t>(expected));
        } else {
            pixels = std::move(data);
        }
    }

    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);

    if (is_index_palette) {
        decode_indexed(pixels.data(), pixels.size(), palette_data, n_palette, img);
    } else {
        decode_pixels(fmt, pixels.data(), pixels.size(), img);
    }

    return {std::move(img), hdr};
}

// --- DXT Encoding ---

struct NRGBA { uint8_t r, g, b, a; };

static std::array<NRGBA, 16> gather_block(const Image& img, int x0, int y0) {
    std::array<NRGBA, 16> px;
    size_t k = 0;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int xx = std::clamp(x0 + x, 0, img.width - 1);
            int yy = std::clamp(y0 + y, 0, img.height - 1);
            NRGBA& p = px[k++];
            img.get(xx, yy, p.r, p.g, p.b, p.a);
        }
    }
    return px;
}

static std::pair<RGB, RGB> min_max_color(const std::array<NRGBA, 16>& px) {
    RGB mn{255, 255, 255}, mx{0, 0, 0};
    for (const auto& c : px) {
        if (c.r < mn.r) mn.r = c.r;
        if (c.g < mn.g) mn.g = c.g;
        if (c.b < mn.b) mn.b = c.b;
        if (c.r > mx.r) mx.r = c.r;
        if (c.g > mx.g) mx.g = c.g;
        if (c.b > mx.b) mx.b = c.b;
    }
    return {mn, mx};
}

static std::array<RGB, 4> palette_dxt1(uint16_t c0, uint16_t c1) {
    auto [r0, g0, b0] = rgb565(c0);
    auto [r1, g1, b1] = rgb565(c1);
    std::array<RGB, 4> p = {RGB{r0, g0, b0}, RGB{r1, g1, b1}};
    if (c0 > c1) {
        p[2] = {static_cast<uint8_t>((2u*r0+r1)/3), static_cast<uint8_t>((2u*g0+g1)/3), static_cast<uint8_t>((2u*b0+b1)/3)};
        p[3] = {static_cast<uint8_t>((r0+2u*r1)/3), static_cast<uint8_t>((g0+2u*g1)/3), static_cast<uint8_t>((b0+2u*b1)/3)};
    } else {
        p[2] = {static_cast<uint8_t>((r0+r1)/2u), static_cast<uint8_t>((g0+g1)/2u), static_cast<uint8_t>((b0+b1)/2u)};
        p[3] = {0, 0, 0};
    }
    return p;
}

static std::array<RGB, 4> palette_dxt5_color(uint16_t c0, uint16_t c1) {
    auto [r0, g0, b0] = rgb565(c0);
    auto [r1, g1, b1] = rgb565(c1);
    return {RGB{r0,g0,b0}, RGB{r1,g1,b1},
            RGB{static_cast<uint8_t>((2u*r0+r1)/3), static_cast<uint8_t>((2u*g0+g1)/3), static_cast<uint8_t>((2u*b0+b1)/3)},
            RGB{static_cast<uint8_t>((r0+2u*r1)/3), static_cast<uint8_t>((g0+2u*g1)/3), static_cast<uint8_t>((b0+2u*b1)/3)}};
}

static int nearest_color_idx(const std::array<RGB, 4>& p, NRGBA c, bool transparent_mode) {
    if (transparent_mode && c.a < 128) return 3;
    size_t best = 0;
    int limit = transparent_mode ? 3 : 4;
    double best_d = 1e18;
    for (size_t i = 0; i < static_cast<size_t>(limit); i++) {
        double dr = static_cast<double>(c.r) - p[i].r;
        double dg = static_cast<double>(c.g) - p[i].g;
        double db = static_cast<double>(c.b) - p[i].b;
        double d = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return static_cast<int>(best);
}

static int nearest_color_idx4(const std::array<RGB, 4>& p, NRGBA c) {
    size_t best = 0; double best_d = 1e18;
    for (size_t i = 0; i < 4; i++) {
        double dr = static_cast<double>(c.r) - p[i].r;
        double dg = static_cast<double>(c.g) - p[i].g;
        double db = static_cast<double>(c.b) - p[i].b;
        double d = dr*dr + dg*dg + db*db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return static_cast<int>(best);
}

static std::array<uint8_t, 8> alpha_palette_dxt5(uint8_t a0, uint8_t a1) {
    std::array<uint8_t, 8> ap = {a0, a1};
    if (a0 > a1) {
        ap[2] = static_cast<uint8_t>((6u*a0+1u*a1)/7);
        ap[3] = static_cast<uint8_t>((5u*a0+2u*a1)/7);
        ap[4] = static_cast<uint8_t>((4u*a0+3u*a1)/7);
        ap[5] = static_cast<uint8_t>((3u*a0+4u*a1)/7);
        ap[6] = static_cast<uint8_t>((2u*a0+5u*a1)/7);
        ap[7] = static_cast<uint8_t>((1u*a0+6u*a1)/7);
    } else {
        ap[2] = static_cast<uint8_t>((4u*a0+1u*a1)/5);
        ap[3] = static_cast<uint8_t>((3u*a0+2u*a1)/5);
        ap[4] = static_cast<uint8_t>((2u*a0+3u*a1)/5);
        ap[5] = static_cast<uint8_t>((1u*a0+4u*a1)/5);
        ap[6] = 0; ap[7] = 255;
    }
    return ap;
}

static int nearest_alpha_idx(const std::array<uint8_t, 8>& ap, uint8_t a) {
    size_t best = 0;
    int best_d = 256;
    for (size_t i = 0; i < 8; i++) {
        int d = std::abs(static_cast<int>(a) - static_cast<int>(ap[i]));
        if (d < best_d) { best_d = d; best = i; }
    }
    return static_cast<int>(best);
}

static void encode_block_color4(const std::array<NRGBA, 16>& px, uint8_t* out) {
    auto [mn, mx] = min_max_color(px);
    uint16_t c0 = pack565(mx.r, mx.g, mx.b);
    uint16_t c1 = pack565(mn.r, mn.g, mn.b);
    if (c0 <= c1) std::swap(c0, c1);
    auto pal = palette_dxt5_color(c0, c1);
    uint32_t idx_bits = 0;
    for (size_t i = 0; i < 16; i++)
        idx_bits |= static_cast<uint32_t>(nearest_color_idx4(pal, px[i]) & 0x3) << (2 * i);
    std::memcpy(out, &c0, 2); std::memcpy(out+2, &c1, 2); std::memcpy(out+4, &idx_bits, 4);
}

static void encode_block_dxt1(const std::array<NRGBA, 16>& px, uint8_t* out) {
    bool transparent = false;
    for (const auto& p : px) if (p.a < 128) { transparent = true; break; }

    auto [mn, mx] = min_max_color(px);
    uint16_t c_min = pack565(mn.r, mn.g, mn.b);
    uint16_t c_max = pack565(mx.r, mx.g, mx.b);

    uint16_t c0, c1;
    if (transparent) {
        c0 = c_min; c1 = c_max;
        if (c0 > c1) std::swap(c0, c1);
    } else {
        c0 = c_max; c1 = c_min;
        if (c0 <= c1) std::swap(c0, c1);
    }

    auto pal = palette_dxt1(c0, c1);
    uint32_t idx_bits = 0;
    for (size_t i = 0; i < 16; i++)
        idx_bits |= static_cast<uint32_t>(nearest_color_idx(pal, px[i], transparent) & 0x3) << (2 * i);
    std::memcpy(out, &c0, 2); std::memcpy(out+2, &c1, 2); std::memcpy(out+4, &idx_bits, 4);
}

static void encode_block_dxt3(const std::array<NRGBA, 16>& px, uint8_t* out) {
    // Alpha
    for (size_t i = 0; i < 16; i++) {
        uint16_t n = static_cast<uint16_t>((static_cast<uint32_t>(px[i].a) + 8) / 17);
        if (i % 2 == 0)
            out[i/2] = static_cast<uint8_t>(n & 0xF);
        else
            out[i/2] |= static_cast<uint8_t>((n & 0xF) << 4);
    }
    encode_block_color4(px, out + 8);
}

static void encode_block_dxt5(const std::array<NRGBA, 16>& px, uint8_t* out) {
    uint8_t a_min = 255, a_max = 0;
    for (const auto& p : px) { if (p.a < a_min) a_min = p.a; if (p.a > a_max) a_max = p.a; }

    auto ap = alpha_palette_dxt5(a_max, a_min);
    uint64_t bits = 0;
    for (size_t i = 0; i < 16; i++)
        bits |= static_cast<uint64_t>(nearest_alpha_idx(ap, px[i].a) & 0x7) << (3 * i);

    out[0] = a_max; out[1] = a_min;
    out[2] = static_cast<uint8_t>(bits); out[3] = static_cast<uint8_t>(bits >> 8);
    out[4] = static_cast<uint8_t>(bits >> 16); out[5] = static_cast<uint8_t>(bits >> 24);
    out[6] = static_cast<uint8_t>(bits >> 32); out[7] = static_cast<uint8_t>(bits >> 40);
    encode_block_color4(px, out + 8);
}

static std::vector<uint8_t> encode_image_dxt(const Image& img, int block_size,
                                              void (*encode_fn)(const std::array<NRGBA,16>&, uint8_t*)) {
    int bw = std::max(1, (img.width + 3) / 4);
    int bh = std::max(1, (img.height + 3) / 4);
    std::vector<uint8_t> out(static_cast<size_t>(bw) * static_cast<size_t>(bh) * static_cast<size_t>(block_size));
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            auto block = gather_block(img, bx*4, by*4);
            encode_fn(block, out.data() + static_cast<size_t>(by * bw + bx) * static_cast<size_t>(block_size));
        }
    }
    return out;
}

static bool has_alpha(const Image& img) {
    for (size_t i = 3; i < img.pixels.size(); i += 4)
        if (img.pixels[i] < 255) return true;
    return false;
}

static bool is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

Header encode(std::ostream& w, const Image& img, const std::string& format) {
    if (img.width <= 0 || img.height <= 0)
        throw std::runtime_error(std::format("paa: invalid dimensions {}x{}", img.width, img.height));
    if (!is_pow2(img.width) || !is_pow2(img.height))
        throw std::runtime_error(std::format("paa: dimensions must be power-of-two, got {}x{}", img.width, img.height));

    std::string ff = format;
    std::transform(ff.begin(), ff.end(), ff.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
    if (ff.empty()) ff = "auto";

    std::string fmt_name;
    if (ff == "auto") fmt_name = has_alpha(img) ? "DXT5" : "DXT1";
    else if (ff == "dxt1") fmt_name = "DXT1";
    else if (ff == "dxt3") fmt_name = "DXT3";
    else if (ff == "dxt5") fmt_name = "DXT5";
    else throw std::runtime_error(std::format("paa: invalid format {}", format));

    uint16_t tag = format_tag(fmt_name);

    std::vector<uint8_t> data;
    if (fmt_name == "DXT1") data = encode_image_dxt(img, 8, encode_block_dxt1);
    else if (fmt_name == "DXT3") data = encode_image_dxt(img, 16, encode_block_dxt3);
    else if (fmt_name == "DXT5") data = encode_image_dxt(img, 16, encode_block_dxt5);

    if (data.size() > 0xFFFFFF)
        throw std::runtime_error(std::format("paa: mipmap too large ({} bytes)", data.size()));

    // type tag
    binutil::write_u16(w, tag);
    // no TAGGs, no palette
    binutil::write_u16(w, 0);
    // mipmap header (uncompressed)
    binutil::write_u16(w, static_cast<uint16_t>(img.width));
    binutil::write_u16(w, static_cast<uint16_t>(img.height));
    // data size u24
    uint8_t u24[3] = {static_cast<uint8_t>(data.size() & 0xFF),
                       static_cast<uint8_t>((data.size() >> 8) & 0xFF),
                       static_cast<uint8_t>((data.size() >> 16) & 0xFF)};
    w.write(reinterpret_cast<const char*>(u24), 3);
    w.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

    return {fmt_name, img.width, img.height};
}

} // namespace armatools::paa
