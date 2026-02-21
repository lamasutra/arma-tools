#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace armatools::binutil {

// Assumes little-endian (x86/x64). Fail at compile time otherwise.
static_assert(std::endian::native == std::endian::little,
              "armatools requires a little-endian platform");

// --- Read helpers (throw on failure) ---

inline uint8_t read_u8(std::istream& r) {
    char b;
    if (!r.read(&b, 1))
        throw std::runtime_error("binutil: failed to read u8");
    return static_cast<uint8_t>(b);
}

inline uint16_t read_u16(std::istream& r) {
    uint16_t v;
    if (!r.read(reinterpret_cast<char*>(&v), 2))
        throw std::runtime_error("binutil: failed to read u16");
    return v;
}

inline int32_t read_i32(std::istream& r) {
    int32_t v;
    if (!r.read(reinterpret_cast<char*>(&v), 4))
        throw std::runtime_error("binutil: failed to read i32");
    return v;
}

inline uint32_t read_u32(std::istream& r) {
    uint32_t v;
    if (!r.read(reinterpret_cast<char*>(&v), 4))
        throw std::runtime_error("binutil: failed to read u32");
    return v;
}

inline float read_f32(std::istream& r) {
    float v;
    if (!r.read(reinterpret_cast<char*>(&v), 4))
        throw std::runtime_error("binutil: failed to read f32");
    return v;
}

inline std::vector<float> read_f32_slice(std::istream& r, size_t n) {
    std::vector<float> out(n);
    if (n > 0 && !r.read(reinterpret_cast<char*>(out.data()),
                          static_cast<std::streamsize>(n * 4)))
        throw std::runtime_error("binutil: failed to read f32 slice");
    return out;
}

inline std::vector<uint16_t> read_u16_slice(std::istream& r, size_t n) {
    std::vector<uint16_t> out(n);
    if (n > 0 && !r.read(reinterpret_cast<char*>(out.data()),
                          static_cast<std::streamsize>(n * 2)))
        throw std::runtime_error("binutil: failed to read u16 slice");
    return out;
}

inline std::vector<uint32_t> read_u32_slice(std::istream& r, size_t n) {
    std::vector<uint32_t> out(n);
    if (n > 0 && !r.read(reinterpret_cast<char*>(out.data()),
                          static_cast<std::streamsize>(n * 4)))
        throw std::runtime_error("binutil: failed to read u32 slice");
    return out;
}

inline std::string read_asciiz(std::istream& r) {
    std::string s;
    char c;
    while (r.read(&c, 1)) {
        if (c == '\0') return s;
        s += c;
    }
    throw std::runtime_error("binutil: unexpected end of stream reading asciiz");
}

inline std::string read_fixed_string(std::istream& r, size_t size) {
    std::string buf(size, '\0');
    if (!r.read(buf.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("binutil: failed to read fixed string");
    auto pos = buf.find('\0');
    if (pos != std::string::npos)
        buf.resize(pos);
    return buf;
}

inline std::array<float, 12> read_transform_matrix(std::istream& r) {
    std::array<float, 12> m{};
    if (!r.read(reinterpret_cast<char*>(m.data()), 48))
        throw std::runtime_error("binutil: failed to read transform matrix");
    return m;
}

inline std::string read_signature(std::istream& r) {
    char buf[4];
    if (!r.read(buf, 4))
        throw std::runtime_error("binutil: failed to read signature");
    return {buf, 4};
}

inline uint32_t read_compressed_int(std::istream& r) {
    uint32_t result = 0;
    for (int shift = 0;; shift += 7) {
        uint8_t b = read_u8(r);
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return result;
    }
}

inline std::vector<uint8_t> read_bytes(std::istream& r, size_t n) {
    std::vector<uint8_t> buf(n);
    if (n > 0 && !r.read(reinterpret_cast<char*>(buf.data()),
                          static_cast<std::streamsize>(n)))
        throw std::runtime_error("binutil: failed to read bytes");
    return buf;
}

// --- Write helpers (throw on failure) ---

inline void write_u8(std::ostream& w, uint8_t v) {
    if (!w.write(reinterpret_cast<const char*>(&v), 1))
        throw std::runtime_error("binutil: failed to write u8");
}

inline void write_u16(std::ostream& w, uint16_t v) {
    if (!w.write(reinterpret_cast<const char*>(&v), 2))
        throw std::runtime_error("binutil: failed to write u16");
}

inline void write_u32(std::ostream& w, uint32_t v) {
    if (!w.write(reinterpret_cast<const char*>(&v), 4))
        throw std::runtime_error("binutil: failed to write u32");
}

inline void write_f32(std::ostream& w, float v) {
    if (!w.write(reinterpret_cast<const char*>(&v), 4))
        throw std::runtime_error("binutil: failed to write f32");
}

inline void write_f64(std::ostream& w, double v) {
    if (!w.write(reinterpret_cast<const char*>(&v), 8))
        throw std::runtime_error("binutil: failed to write f64");
}

inline void write_asciiz(std::ostream& w, const std::string& s) {
    if (!s.empty() && !w.write(s.data(), static_cast<std::streamsize>(s.size())))
        throw std::runtime_error("binutil: failed to write asciiz string");
    write_u8(w, 0);
}

inline void write_short_bool(std::ostream& w, bool v) {
    write_u16(w, v ? 1 : 0);
}

} // namespace armatools::binutil
