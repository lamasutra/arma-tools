#include "armatools/tga.h"
#include "armatools/binutil.h"

#include <format>
#include <stdexcept>

namespace armatools::tga {

Image decode(std::istream& r) {
    uint8_t hdr[18];
    if (!r.read(reinterpret_cast<char*>(hdr), 18))
        throw std::runtime_error("tga: failed to read header");

    int id_len = hdr[0];
    uint8_t color_map_type = hdr[1];
    uint8_t image_type = hdr[2];

    if (color_map_type != 0)
        throw std::runtime_error("tga: color-mapped images are not supported");
    if (image_type != 2)
        throw std::runtime_error(
            std::format("tga: only uncompressed true-color (type 2) is supported, got {}", image_type));

    int w = static_cast<int>(hdr[12]) | (static_cast<int>(hdr[13]) << 8);
    int h = static_cast<int>(hdr[14]) | (static_cast<int>(hdr[15]) << 8);
    int bpp = hdr[16];
    uint8_t desc = hdr[17];

    if (w <= 0 || h <= 0)
        throw std::runtime_error(std::format("tga: invalid dimensions {}x{}", w, h));
    if (bpp != 24 && bpp != 32)
        throw std::runtime_error(std::format("tga: only 24/32 bpp is supported, got {}", bpp));

    // Skip ID field
    if (id_len > 0) {
        std::vector<uint8_t> dummy(static_cast<size_t>(id_len));
        if (!r.read(reinterpret_cast<char*>(dummy.data()), id_len))
            throw std::runtime_error("tga: failed to read ID field");
    }

    bool top_origin = (desc & 0x20) != 0;
    int bytes_per_pixel = bpp / 8;
    int row_size = w * bytes_per_pixel;

    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w * h * 4));

    std::vector<uint8_t> row(static_cast<size_t>(row_size));
    for (int yy = 0; yy < h; yy++) {
        if (!r.read(reinterpret_cast<char*>(row.data()), row_size))
            throw std::runtime_error("tga: failed to read pixel row");

        int y = top_origin ? yy : (h - 1 - yy);
        for (int x = 0; x < w; x++) {
            int off = x * bytes_per_pixel;
            uint8_t b = row[static_cast<size_t>(off)];
            uint8_t g = row[static_cast<size_t>(off + 1)];
            uint8_t rv = row[static_cast<size_t>(off + 2)];
            uint8_t a = (bytes_per_pixel == 4) ? row[static_cast<size_t>(off + 3)] : uint8_t(255);
            size_t dst = static_cast<size_t>((y * w + x) * 4);
            img.pixels[dst] = rv;
            img.pixels[dst + 1] = g;
            img.pixels[dst + 2] = b;
            img.pixels[dst + 3] = a;
        }
    }
    return img;
}

void encode(std::ostream& w, const Image& img) {
    if (img.width <= 0 || img.height <= 0 || img.width > 65535 || img.height > 65535)
        throw std::runtime_error(std::format("tga: invalid dimensions {}x{}", img.width, img.height));

    uint8_t hdr[18] = {};
    hdr[2] = 2; // uncompressed true-color
    hdr[12] = static_cast<uint8_t>(img.width & 0xFF);
    hdr[13] = static_cast<uint8_t>((img.width >> 8) & 0xFF);
    hdr[14] = static_cast<uint8_t>(img.height & 0xFF);
    hdr[15] = static_cast<uint8_t>((img.height >> 8) & 0xFF);
    hdr[16] = 32;   // pixel depth
    hdr[17] = 0x28;  // 8 alpha bits + top-left origin
    if (!w.write(reinterpret_cast<const char*>(hdr), 18))
        throw std::runtime_error("tga: failed to write header");

    std::vector<uint8_t> row(static_cast<size_t>(img.width * 4));
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            size_t src = static_cast<size_t>((y * img.width + x) * 4);
            size_t dst = static_cast<size_t>(x * 4);
            row[dst] = img.pixels[src + 2];     // B
            row[dst + 1] = img.pixels[src + 1]; // G
            row[dst + 2] = img.pixels[src];     // R
            row[dst + 3] = img.pixels[src + 3]; // A
        }
        if (!w.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size())))
            throw std::runtime_error("tga: failed to write pixel row");
    }
}

} // namespace armatools::tga
