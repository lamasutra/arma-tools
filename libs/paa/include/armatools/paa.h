#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace armatools::paa {

struct Header {
    std::string format; // "DXT1", "DXT5", "ARGB4444", etc.
    int width = 0;
    int height = 0;
};

// RGBA pixel buffer (4 bytes per pixel, row-major, top-to-bottom).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA, size = width * height * 4

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        size_t off = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        pixels[off] = r; pixels[off+1] = g; pixels[off+2] = b; pixels[off+3] = a;
    }

    void get(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const {
        size_t off = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        r = pixels[off]; g = pixels[off+1]; b = pixels[off+2]; a = pixels[off+3];
    }
};

// read_header parses a PAA/PAC file header and returns format and dimensions.
Header read_header(std::istream& r);

// decode reads a PAA/PAC file and decodes the first mipmap to an RGBA image.
std::pair<Image, Header> decode(std::istream& r);

// encode writes a minimal PAA file with one mipmap.
// format: "auto", "dxt1", "dxt3", "dxt5"
Header encode(std::ostream& w, const Image& img, const std::string& format = "auto");

// format_name maps a PAA type tag to a human-readable format name.
std::string format_name(uint16_t tag);

} // namespace armatools::paa
