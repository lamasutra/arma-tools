#include "armatools/wrp.h"
#include "armatools/surface.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

// GeoTIFF constants
static constexpr uint16_t tag_image_width = 256;
static constexpr uint16_t tag_image_length = 257;
static constexpr uint16_t tag_bits_per_sample = 258;
static constexpr uint16_t tag_compression = 259;
static constexpr uint16_t tag_photometric = 262;
static constexpr uint16_t tag_strip_offsets = 273;
static constexpr uint16_t tag_samples_per_pixel = 277;
static constexpr uint16_t tag_rows_per_strip = 278;
static constexpr uint16_t tag_strip_byte_counts = 279;
static constexpr uint16_t tag_sample_format = 339;
static constexpr uint16_t tag_model_pixel_scale = 33550;
static constexpr uint16_t tag_model_tiepoint = 33922;
static constexpr uint16_t tag_geo_key_directory = 34735;

static constexpr uint16_t dt_short = 3;
static constexpr uint16_t dt_long = 4;
static constexpr uint16_t dt_double = 12;

static void write_le16(std::ostream& w, uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); }
static void write_le32(std::ostream& w, uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); }
static void write_le64(std::ostream& w, uint64_t v) { w.write(reinterpret_cast<const char*>(&v), 8); }
static void write_le_f64(std::ostream& w, double v) { write_le64(w, std::bit_cast<uint64_t>(v)); }

static void put_tag(uint8_t* buf, int off, uint16_t tag, uint16_t dtype, uint32_t count, uint32_t value) {
    memcpy(buf + off, &tag, 2);
    memcpy(buf + off + 2, &dtype, 2);
    memcpy(buf + off + 4, &count, 4);
    memcpy(buf + off + 8, &value, 4);
}

struct GeoParams {
    double cell_size;
    double offset_x;
    double offset_z;
    int width;
    int height;
};

static void write_tiff_rgb(std::ostream& w, const std::vector<armatools::surface::RGB>& pixels,
                            int width, int height, const GeoParams& geo) {
    uint32_t pixel_bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 3;

    constexpr int num_tags = 13;
    constexpr uint32_t ifd_offset = 8;
    constexpr uint32_t ifd_size = 2 + num_tags * 12 + 4;
    constexpr uint32_t extra_start = ifd_offset + ifd_size;
    constexpr uint32_t bps_off = extra_start;
    constexpr uint32_t bps_size = 6; // 3 x uint16
    constexpr uint32_t pixel_scale_off = bps_off + bps_size;
    constexpr uint32_t tiepoint_off = pixel_scale_off + 24;
    constexpr uint32_t geo_key_off = tiepoint_off + 48;
    constexpr uint32_t pixel_offset = geo_key_off + 24;

    // TIFF header
    w.write("II", 2);
    write_le16(w, 42);
    write_le32(w, ifd_offset);

    // IFD
    uint8_t ifd[ifd_size];
    memset(ifd, 0, sizeof(ifd));
    uint16_t tag_count = num_tags;
    memcpy(ifd, &tag_count, 2);
    int off = 2;
    put_tag(ifd, off, tag_image_width, dt_long, 1, static_cast<uint32_t>(width)); off += 12;
    put_tag(ifd, off, tag_image_length, dt_long, 1, static_cast<uint32_t>(height)); off += 12;
    put_tag(ifd, off, tag_bits_per_sample, dt_short, 3, bps_off); off += 12;
    put_tag(ifd, off, tag_compression, dt_short, 1, 1); off += 12;
    put_tag(ifd, off, tag_photometric, dt_short, 1, 2); off += 12; // RGB
    put_tag(ifd, off, tag_strip_offsets, dt_long, 1, pixel_offset); off += 12;
    put_tag(ifd, off, tag_samples_per_pixel, dt_short, 1, 3); off += 12;
    put_tag(ifd, off, tag_rows_per_strip, dt_long, 1, static_cast<uint32_t>(height)); off += 12;
    put_tag(ifd, off, tag_strip_byte_counts, dt_long, 1, pixel_bytes); off += 12;
    put_tag(ifd, off, tag_sample_format, dt_short, 1, 1); off += 12; // unsigned int
    put_tag(ifd, off, tag_model_pixel_scale, dt_double, 3, pixel_scale_off); off += 12;
    put_tag(ifd, off, tag_model_tiepoint, dt_double, 6, tiepoint_off); off += 12;
    put_tag(ifd, off, tag_geo_key_directory, dt_short, 12, geo_key_off); off += 12;
    w.write(reinterpret_cast<const char*>(ifd), ifd_size);

    // BitsPerSample: [8, 8, 8]
    write_le16(w, 8); write_le16(w, 8); write_le16(w, 8);

    // ModelPixelScale
    write_le_f64(w, geo.cell_size);
    write_le_f64(w, geo.cell_size);
    write_le_f64(w, 0.0);

    // ModelTiepoint
    write_le_f64(w, 0.0);
    write_le_f64(w, 0.0);
    write_le_f64(w, 0.0);
    write_le_f64(w, geo.offset_x);
    write_le_f64(w, geo.offset_z + static_cast<double>(geo.height - 1) * geo.cell_size);
    write_le_f64(w, 0.0);

    // GeoKeyDirectory
    write_le16(w, 1); write_le16(w, 1); write_le16(w, 0); write_le16(w, 2);
    write_le16(w, 1024); write_le16(w, 0); write_le16(w, 1); write_le16(w, 1);
    write_le16(w, 1025); write_le16(w, 0); write_le16(w, 1); write_le16(w, 1);

    // Pixel data: RGB, flipped vertically
    uint8_t buf[3];
    for (int row = height - 1; row >= 0; row--) {
        int row_start = row * width;
        for (int col = 0; col < width; col++) {
            const auto& c = pixels[static_cast<size_t>(row_start + col)];
            buf[0] = c.r; buf[1] = c.g; buf[2] = c.b;
            w.write(reinterpret_cast<const char*>(buf), 3);
        }
    }
}

static void print_usage() {
    std::cerr << "Usage: wrp_satmask [flags] <input.wrp> <output.tif>\n\n"
              << "Generates an RGB surface mask GeoTIFF from WRP material/texture cell data.\n\n"
              << "Each cell is colored by its dominant surface material category.\n"
              << "Also writes materials.json with classification details.\n\n"
              << "Flags:\n"
              << "  --pretty          Pretty-print materials.json output\n"
              << "  -offset-x <n>    X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>    Z coordinate offset (default: 0)\n"
              << "  -materials <p>   Output path for materials.json (default: alongside .tif)\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    double offset_x = 200000;
    double offset_z = 0;
    std::string materials_path;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) offset_x = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-materials") == 0 && i + 1 < argc) materials_path = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() < 2) {
        print_usage();
        return 1;
    }

    std::string input_path = positional[0];
    std::string output_path = positional[1];

    if (materials_path.empty()) {
        materials_path = (fs::path(output_path).parent_path() / "materials.json").string();
    }

    std::ifstream f(input_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error: cannot open " << input_path << '\n';
        return 1;
    }

    armatools::wrp::WorldData world;
    try {
        world = armatools::wrp::read(f, {.no_objects = true});
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << input_path << ": " << e.what() << '\n';
        return 1;
    }

    if (world.cell_texture_indexes.empty()) {
        std::cerr << "Error: no cell texture data in " << input_path << '\n';
        return 1;
    }
    if (world.textures.empty()) {
        std::cerr << "Error: no material data in " << input_path << '\n';
        return 1;
    }

    int width = world.grid.cells_x;
    int height = world.grid.cells_y;

    if (static_cast<int>(world.cell_texture_indexes.size()) != width * height) {
        std::cerr << std::format("Error: cell texture data size {} does not match grid {}x{}\n",
                                  world.cell_texture_indexes.size(), width, height);
        return 1;
    }

    // Pre-classify all texture entries
    std::vector<armatools::surface::Info> categories(world.textures.size());
    for (size_t i = 0; i < world.textures.size(); i++) {
        categories[i] = armatools::surface::classify(world.textures[i].filename);
    }

    // Build pixel array
    std::vector<armatools::surface::RGB> pixels(world.cell_texture_indexes.size());
    std::unordered_map<size_t, int> cell_counts;
    armatools::surface::RGB unknown_color = armatools::surface::classify("").color;
    for (size_t i = 0; i < world.cell_texture_indexes.size(); i++) {
        size_t idx = static_cast<size_t>(world.cell_texture_indexes[i]);
        if (idx < categories.size()) {
            pixels[i] = categories[idx].color;
        } else {
            pixels[i] = unknown_color;
        }
        cell_counts[idx]++;
    }

    // Write RGB GeoTIFF
    GeoParams geo{world.grid.cell_size, offset_x, offset_z, width, height};

    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot create " << output_path << '\n';
        return 1;
    }
    write_tiff_rgb(out, pixels, width, height, geo);
    out.close();

    // Build materials.json
    json materials = json::array();
    for (size_t i = 0; i < world.textures.size(); i++) {
        const auto& cat = categories[i];
        materials.push_back({
            {"index", static_cast<int>(i)},
            {"filename", world.textures[i].filename},
            {"category", std::string(armatools::surface::category_name(cat.category))},
            {"color", armatools::surface::hex(cat.color)},
            {"cell_count", cell_counts[i]},
        });
    }

    // Sort by cell count descending
    std::sort(materials.begin(), materials.end(), [](const json& a, const json& b) {
        return a["cell_count"].get<int>() > b["cell_count"].get<int>();
    });

    std::ofstream mat_file(materials_path);
    if (!mat_file) {
        std::cerr << "Error: cannot create " << materials_path << '\n';
        return 1;
    }
    if (pretty) mat_file << std::setw(2) << materials << '\n';
    else mat_file << materials << '\n';

    // Stats to stderr
    std::cerr << "SatMask: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << std::format("Grid: {}x{}, cell size {:.0f}m\n", width, height, world.grid.cell_size);
    std::cerr << "Materials: " << world.textures.size() << " textures\n";

    // Per-category cell counts
    std::unordered_map<int, int> cat_counts; // category enum → count
    for (auto tex_idx : world.cell_texture_indexes) {
        size_t idx = static_cast<size_t>(tex_idx);
        int cat_val = (idx < categories.size())
            ? static_cast<int>(categories[idx].category)
            : static_cast<int>(armatools::surface::Category::Unknown);
        cat_counts[cat_val]++;
    }
    const auto& cat_table = armatools::surface::category_table();
    for (const auto& ci : cat_table) {
        int cnt = cat_counts[static_cast<int>(ci.category)];
        if (cnt > 0) {
            double pct = 100.0 * static_cast<double>(cnt) / static_cast<double>(world.cell_texture_indexes.size());
            std::cerr << std::format("  {:<10s} {:5d} cells ({:.1f}%)\n",
                                      std::string(armatools::surface::category_name(ci.category)), cnt, pct);
        }
    }

    std::cerr << std::format("Offset: X+{:.0f} Z+{:.0f}\n", offset_x, offset_z);
    std::cerr << "Output: " << output_path << '\n';
    std::cerr << "Materials: " << materials_path << '\n';

    return 0;
}
