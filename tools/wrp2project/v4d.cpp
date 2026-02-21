#include "project.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

// V4D header (96 bytes)
struct V4DHeader {
    uint32_t version = 2;
    uint32_t reserved0 = 0;
    int32_t scale = -100;
    uint32_t unknown_0c = 60;
    uint32_t unknown_10 = 0;
    uint32_t unknown_14 = 60;
    uint32_t unknown_18 = 1;
    uint32_t unknown_1c = 2;
    uint32_t tile_dim = 0;
    uint32_t tiles_x = 0;
    uint32_t tiles_y = 0;
    uint32_t full_width = 0;
    uint32_t full_height = 0;
    uint8_t padding[36]{};
    uint32_t chunk_size = 0;
    uint32_t chunk_flag = 3;
};

struct V4DChunk {
    bool has_index = false;
    uint32_t tile_index = 0;
    uint32_t decomp_size = 0;
    uint32_t flag = 0;
    std::vector<uint8_t> data; // zlib compressed
};

static int next_pow2(int n) { int p = 1; while (p < n) p *= 2; return p; }

static void tile_layout(int width, int height, int& tile_dim, int& tiles_x, int& tiles_y) {
    int max_dim = std::max(width, height);
    int p2 = next_pow2(max_dim);
    if (p2 <= 2048) {
        tile_dim = p2; tiles_x = 1; tiles_y = 1;
        return;
    }
    tile_dim = 1024;
    tiles_x = (width + tile_dim - 1) / tile_dim;
    tiles_y = (height + tile_dim - 1) / tile_dim;
}

static std::vector<uint8_t> zlib_compress(const uint8_t* data, size_t len) {
    uLongf bound = compressBound(static_cast<uLong>(len));
    std::vector<uint8_t> out(bound);
    int ret = compress2(out.data(), &bound, data, static_cast<uLong>(len), Z_BEST_COMPRESSION);
    if (ret != Z_OK) throw std::runtime_error("zlib compression failed");
    out.resize(bound);
    return out;
}

static void write_v4d_file(const std::string& path, const V4DHeader& hdr, const std::vector<V4DChunk>& chunks) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot create " + path);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    for (const auto& ch : chunks) {
        if (ch.has_index) {
            f.write(reinterpret_cast<const char*>(&ch.tile_index), 4);
            f.write(reinterpret_cast<const char*>(&ch.decomp_size), 4);
            f.write(reinterpret_cast<const char*>(&ch.flag), 4);
        }
        uint32_t comp_size = static_cast<uint32_t>(ch.data.size());
        f.write(reinterpret_cast<const char*>(&comp_size), 4);
        f.write(reinterpret_cast<const char*>(ch.data.data()), static_cast<std::streamsize>(ch.data.size()));
    }
}

static void write_mask_v4d(const std::string& path, int width, int height) {
    int p2 = next_pow2(std::max(width, height));
    if (p2 > 2048) p2 = 2048;

    int tile_pixels = p2 * p2;
    uint32_t decomp_size = static_cast<uint32_t>(tile_pixels * 4);
    uint32_t fill_value = 0x80800000;

    V4DHeader hdr;
    hdr.tile_dim = static_cast<uint32_t>(p2);
    hdr.tiles_x = 1;
    hdr.tiles_y = 1;
    hdr.full_width = static_cast<uint32_t>(p2);
    hdr.full_height = static_cast<uint32_t>(p2);
    hdr.chunk_size = decomp_size;

    std::vector<uint8_t> raw(static_cast<size_t>(tile_pixels) * 4);
    for (int i = 0; i < tile_pixels; i++)
        std::memcpy(raw.data() + i * 4, &fill_value, 4);

    auto compressed = zlib_compress(raw.data(), raw.size());
    write_v4d_file(path, hdr, {{false, 0, 0, 0, compressed}});
}

static void write_heightmap_v4d(const std::string& path, const std::vector<float>& elevations,
                                 int width, int height) {
    int td = 0, tx = 0, ty = 0;
    tile_layout(width, height, td, tx, ty);

    V4DHeader hdr;
    hdr.tile_dim = static_cast<uint32_t>(td);
    hdr.tiles_x = static_cast<uint32_t>(tx);
    hdr.tiles_y = static_cast<uint32_t>(ty);
    hdr.full_width = static_cast<uint32_t>(td * tx);
    hdr.full_height = static_cast<uint32_t>(td * ty);
    hdr.chunk_size = static_cast<uint32_t>(td * td * 4);

    std::vector<V4DChunk> chunks;
    for (int tile_y = 0; tile_y < ty; tile_y++) {
        for (int tile_x = 0; tile_x < tx; tile_x++) {
            std::vector<uint8_t> tile_data(static_cast<size_t>(td) * static_cast<size_t>(td) * 4);
            for (int row = 0; row < td; row++) {
                int src_y = (height - 1) - (tile_y * td + row);
                for (int col = 0; col < td; col++) {
                    int src_x = tile_x * td + col;
                    float val = 0;
                    if (src_x >= 0 && src_x < width && src_y >= 0 && src_y < height)
                        val = elevations[static_cast<size_t>(src_y * width + src_x)];
                    uint32_t bits;
                    std::memcpy(&bits, &val, 4);
                    std::memcpy(tile_data.data() + (static_cast<size_t>(row) * static_cast<size_t>(td) + static_cast<size_t>(col)) * 4, &bits, 4);
                }
            }

            auto compressed = zlib_compress(tile_data.data(), tile_data.size());
            V4DChunk chunk;
            chunk.data = compressed;
            if (tile_x > 0 || tile_y > 0) {
                chunk.has_index = true;
                chunk.tile_index = static_cast<uint32_t>(tile_x) + static_cast<uint32_t>(tile_y) * 256;
                chunk.decomp_size = static_cast<uint32_t>(td * td * 4);
                chunk.flag = 3;
            }
            chunks.push_back(std::move(chunk));
        }
    }

    write_v4d_file(path, hdr, chunks);
}

void write_v4d(ProjectInfo& p) {
    if (p.hm_elevations.empty()) return;

    std::string lower_name = p.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;
    std::string base_path = (fs::path(p.output_dir) / map_name).string();

    write_mask_v4d(base_path + "_001.v4d", p.hm_width, p.hm_height);
    write_heightmap_v4d(base_path + "_002.v4d", p.hm_elevations, p.hm_width, p.hm_height);
}
