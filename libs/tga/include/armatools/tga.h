#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>

namespace armatools::tga {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA, row-major, top-to-bottom, 4 bytes per pixel
};

// decode reads an uncompressed true-color TGA (24/32 bpp).
Image decode(std::istream& r);

// encode writes an uncompressed 32-bit true-color TGA with top-left origin.
void encode(std::ostream& w, const Image& img);

} // namespace armatools::tga
