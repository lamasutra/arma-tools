#include "armatools/wrp.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <string>

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
static void write_le_f32(std::ostream& w, float v) { write_le32(w, std::bit_cast<uint32_t>(v)); }
static void write_le_f64(std::ostream& w, double v) { write_le64(w, std::bit_cast<uint64_t>(v)); }

struct GeoParams {
    double cell_size;
    double offset_x;
    double offset_z;
    int width;
    int height;
};

static void put_tag(uint8_t* buf, int off, uint16_t tag, uint16_t dtype, uint32_t count, uint32_t value) {
    memcpy(buf + off, &tag, 2);
    memcpy(buf + off + 2, &dtype, 2);
    memcpy(buf + off + 4, &count, 4);
    memcpy(buf + off + 8, &value, 4);
}

static void write_geotiff_header(std::ostream& w, uint32_t img_width, uint32_t img_height,
                                  uint16_t bps, uint16_t sample_fmt, uint32_t pixel_bytes,
                                  const GeoParams& geo) {
    constexpr int num_tags = 13;
    constexpr uint32_t ifd_offset = 8;
    constexpr uint32_t ifd_size = 2 + num_tags * 12 + 4;
    constexpr uint32_t extra_start = ifd_offset + ifd_size;
    constexpr uint32_t pixel_scale_off = extra_start;
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
    off += 0; put_tag(ifd, off, tag_image_width, dt_long, 1, img_width); off += 12;
    put_tag(ifd, off, tag_image_length, dt_long, 1, img_height); off += 12;
    put_tag(ifd, off, tag_bits_per_sample, dt_short, 1, bps); off += 12;
    put_tag(ifd, off, tag_compression, dt_short, 1, 1); off += 12;
    put_tag(ifd, off, tag_photometric, dt_short, 1, 1); off += 12;
    put_tag(ifd, off, tag_strip_offsets, dt_long, 1, pixel_offset); off += 12;
    put_tag(ifd, off, tag_samples_per_pixel, dt_short, 1, 1); off += 12;
    put_tag(ifd, off, tag_rows_per_strip, dt_long, 1, img_height); off += 12;
    put_tag(ifd, off, tag_strip_byte_counts, dt_long, 1, pixel_bytes); off += 12;
    put_tag(ifd, off, tag_sample_format, dt_short, 1, sample_fmt); off += 12;
    put_tag(ifd, off, tag_model_pixel_scale, dt_double, 3, pixel_scale_off); off += 12;
    put_tag(ifd, off, tag_model_tiepoint, dt_double, 6, tiepoint_off); off += 12;
    put_tag(ifd, off, tag_geo_key_directory, dt_short, 12, geo_key_off); off += 12;
    // next IFD = 0 (already zeroed)
    w.write(reinterpret_cast<const char*>(ifd), ifd_size);

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
}

static void write_tiff_float32(std::ostream& w, const std::vector<float>& data,
                                int width, int height, const GeoParams& geo) {
    uint32_t pixel_bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4;
    write_geotiff_header(w, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 32, 3, pixel_bytes, geo);

    for (int row = height - 1; row >= 0; row--) {
        int row_start = row * width;
        for (int col = 0; col < width; col++) {
            write_le_f32(w, data[static_cast<size_t>(row_start + col)]);
        }
    }
}

static void write_tiff_uint16(std::ostream& w, const std::vector<float>& data,
                               int width, int height, double min_val, double max_val,
                               const GeoParams& geo) {
    uint32_t pixel_bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2;
    write_geotiff_header(w, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 16, 1, pixel_bytes, geo);

    double range = max_val - min_val;
    if (range <= 0) range = 1;

    for (int row = height - 1; row >= 0; row--) {
        int row_start = row * width;
        for (int col = 0; col < width; col++) {
            double norm = (static_cast<double>(data[static_cast<size_t>(row_start + col)]) - min_val) / range;
            norm = std::clamp(norm, 0.0, 1.0);
            write_le16(w, static_cast<uint16_t>(norm * 65535));
        }
    }
}

static void write_xyz(std::ostream& w, const std::vector<float>& data,
                      int width, int height, double cell_size, double offset_x, double offset_z) {
    for (int row = 0; row < height; row++) {
        double y = offset_z + static_cast<double>(row) * cell_size;
        for (int col = 0; col < width; col++) {
            double x = offset_x + static_cast<double>(col) * cell_size;
            float z = data[static_cast<size_t>(row * width + col)];
            w << std::format("{:.2f} {:.2f} {:.2f}\n", x, y, z);
        }
    }
}

static void print_usage() {
    std::cerr << "Usage: wrp_heightmap [flags] <input.wrp> <output.tif|output.xyz>\n\n"
              << "Extracts the elevation grid from a WRP file as a heightmap.\n\n"
              << "Output formats:\n"
              << "  float32  - GeoTIFF, 32-bit IEEE float, values in meters (default)\n"
              << "  uint16   - GeoTIFF, 16-bit unsigned, scaled [min..max] -> [0..65535]\n"
              << "  xyz      - ASCII point cloud (X Y Z per line), georeferenced\n\n"
              << "Flags:\n"
              << "  -format <fmt>   Output format: float32|uint16|xyz (default: float32)\n"
              << "  -offset-x <n>   X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>   Z coordinate offset (default: 0)\n";
}

int main(int argc, char* argv[]) {
    std::string format = "float32";
    double offset_x = 200000;
    double offset_z = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) {
            offset_x = std::stod(argv[++i]);
        } else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) {
            offset_z = std::stod(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
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

    if (format != "float32" && format != "uint16" && format != "xyz") {
        std::cerr << "Error: -format must be float32, uint16, or xyz\n";
        return 1;
    }

    if (output_path == "-" && format != "xyz") {
        std::cerr << "Error: stdout output (-) is only supported for xyz format\n";
        return 1;
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

    if (world.elevations.empty()) {
        std::cerr << "Error: no elevation data in " << input_path << '\n';
        return 1;
    }

    int width = world.grid.terrain_x;
    int height = world.grid.terrain_y;
    if (static_cast<int>(world.elevations.size()) != width * height) {
        width = world.grid.cells_x;
        height = world.grid.cells_y;
    }
    if (static_cast<int>(world.elevations.size()) != width * height) {
        std::cerr << "Error: elevation data size " << world.elevations.size()
                  << " does not match grid " << width << "x" << height << '\n';
        return 1;
    }

    std::ostream* out = nullptr;
    std::ofstream out_file;
    if (output_path == "-") {
        out = &std::cout;
    } else {
        out_file.open(output_path, std::ios::binary);
        if (!out_file) {
            std::cerr << "Error: cannot create " << output_path << '\n';
            return 1;
        }
        out = &out_file;
    }

    double cell_size = world.bounds.world_size_x / static_cast<double>(width);
    GeoParams geo{cell_size, offset_x, offset_z, width, height};

    try {
        if (format == "float32") {
            write_tiff_float32(*out, world.elevations, width, height, geo);
        } else if (format == "uint16") {
            write_tiff_uint16(*out, world.elevations, width, height,
                              world.bounds.min_elevation, world.bounds.max_elevation, geo);
        } else {
            write_xyz(*out, world.elevations, width, height, cell_size, offset_x, offset_z);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: writing output: " << e.what() << '\n';
        return 1;
    }

    std::cerr << "Heightmap: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << "Grid: " << width << "x" << height << ", cell size " << cell_size << "m\n";
    std::cerr << std::format("Elevation: {:.1f} .. {:.1f} meters\n", world.bounds.min_elevation, world.bounds.max_elevation);
    std::cerr << std::format("Format: {}, offset X+{:.0f} Z+{:.0f}\n", format, offset_x, offset_z);
    if (output_path != "-") {
        std::cerr << "Output: " << output_path << '\n';
    }

    return 0;
}
