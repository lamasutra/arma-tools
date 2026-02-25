#include "asset_provider.h"
#include "mapinfo_tiles.h"
#include "mosaic.h"

#include <armatools/wrp.h>
#include <armatools/pboindex.h>

#include "png_stream_writer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <new>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fs = std::filesystem;

struct TileLoadReport {
    std::vector<RasterTile> tiles;
    std::vector<std::string> missing_paths;
    std::vector<std::string> decode_failures;
};

static TileLoadReport load_tiles(const std::vector<TileRef>& refs, const AssetProvider& provider) {
    TileLoadReport report;
    for (const auto& ref : refs) {
        auto bytes = provider.read(ref.path);
        if (!bytes) {
            report.missing_paths.push_back(ref.path);
            continue;
        }

        std::string data(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        std::istringstream stream(data, std::ios::binary);
        try {
            auto [image, header] = armatools::paa::decode(stream);
            (void)header;
            report.tiles.push_back({ref, std::move(image)});
        } catch (const std::exception& e) {
            report.decode_failures.push_back(ref.path);
        }
    }
    return report;
}

static bool write_png(const fs::path& path, const MosaicResult& mosaic) {
    if (mosaic.pixels.empty() || mosaic.width <= 0 || mosaic.height <= 0) return false;
    return stbi_write_png(path.string().c_str(), mosaic.width, mosaic.height, 4,
                          mosaic.pixels.data(), mosaic.width * 4);
}

struct LegacyTextureState {
    bool attempted = false;
    bool decoded = false;
    bool blank_name = false;
    bool decode_failed = false;
    bool pac = false;
    armatools::paa::Image image;
};

static std::string to_lower_ascii(std::string_view input) {
    std::string lower;
    lower.reserve(input.size());
    for (char c : input) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

static bool write_legacy_sat(const armatools::wrp::WorldData& world,
                             const AssetProvider& provider,
                             const fs::path& base,
                             const fs::path& out_root,
                             bool verbose,
                             int max_resolution) {
    auto log_verbose = [&](const std::string& msg) {
        if (verbose) std::cerr << msg << '\n';
    };
    log_verbose("Starting legacy SAT generation");
    int width = world.grid.cells_x;
    int height = world.grid.cells_y;
    size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (world.cell_texture_indexes.size() != expected) {
        std::cerr << "Error: legacy cell texture count mismatch\n";
        return false;
    }

    if (world.textures.empty()) {
        std::cerr << "Error: no legacy texture entries available\n";
        return false;
    }

    log_verbose(std::format("Legacy texture entries: {}", world.textures.size()));

    std::vector<LegacyTextureState> states(world.textures.size());
    int tile_width = 0;
    int tile_height = 0;
    int decoded_paa_indices = 0;
    int decoded_pac_indices = 0;
    int failed_decode_indices = 0;
    int empty_name_indices = 0;
    int blank_index0_cells = 0;

    for (size_t i = 0; i < expected; ++i) {
        int idx = static_cast<int>(world.cell_texture_indexes[i]);
        if (idx == 0) {
            ++blank_index0_cells;
            continue;
        }
        if (idx < 0 || idx >= static_cast<int>(states.size())) continue;

        auto& state = states[static_cast<size_t>(idx)];
        if (state.attempted) continue;
        state.attempted = true;

        const std::string& tex_name = world.textures[static_cast<size_t>(idx)].filename;
        if (tex_name.empty()) {
            state.blank_name = true;
            ++empty_name_indices;
            continue;
        }

        fs::path tex_path(tex_name);
        auto ext = to_lower_ascii(tex_path.extension().string());
        bool is_pac = ext == ".pac";

        auto bytes = provider.read(tex_name);
        if (!bytes) {
            state.decode_failed = true;
            ++failed_decode_indices;
            continue;
        }

        std::string data(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        std::istringstream stream(data, std::ios::binary);
        try {
            auto [image, header] = armatools::paa::decode(stream);
            (void)header;

            if (tile_width == 0) {
                tile_width = image.width;
                tile_height = image.height;
            }
            if (image.width != tile_width || image.height != tile_height) {
                state.decode_failed = true;
                ++failed_decode_indices;
                continue;
            }

            state.decoded = true;
            state.pac = is_pac;
            state.image = std::move(image);
            if (is_pac) ++decoded_pac_indices;
            else ++decoded_paa_indices;
        } catch (const std::exception& e) {
            state.decode_failed = true;
            ++failed_decode_indices;
        }
    }

    if (tile_width == 0 || tile_height == 0) {
        std::cerr << "Error: no legacy tile could be decoded\n";
        return false;
    }
    log_verbose(std::format("Legacy tile dimensions: {}x{}", tile_width, tile_height));

    uint64_t canvas_width64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(tile_width);
    uint64_t canvas_height64 = static_cast<uint64_t>(height) * static_cast<uint64_t>(tile_height);
    if (canvas_width64 == 0 || canvas_height64 == 0) {
        std::cerr << "Error: invalid legacy canvas dimensions\n";
        return false;
    }
    log_verbose(std::format("Legacy canvas size: {}x{}", canvas_width64, canvas_height64));

    uint64_t pixel_count64 = canvas_width64 * canvas_height64;
    const uint64_t pixels_limit = std::numeric_limits<size_t>::max() / 4;
    if (pixel_count64 > pixels_limit) {
        std::cerr << "Error: legacy canvas too large to allocate (" << pixel_count64 * 4 << " bytes)\n";
        return false;
    }

    size_t canvas_width = static_cast<size_t>(canvas_width64);
    size_t canvas_height = static_cast<size_t>(canvas_height64);

    int out_width = static_cast<int>(canvas_width);
    int out_height = static_cast<int>(canvas_height);
    double scale = 1.0;
    if (max_resolution > 0) {
        int max_dim = std::max(out_width, out_height);
        if (max_dim > max_resolution) {
            scale = static_cast<double>(max_resolution) / static_cast<double>(max_dim);
            out_width = std::max(1, static_cast<int>(std::floor(static_cast<double>(canvas_width) * scale)));
            out_height = std::max(1, static_cast<int>(std::floor(static_cast<double>(canvas_height) * scale)));
        }
    }

    log_verbose("Streaming PNG: enabled");
    std::cerr << "Output size: " << out_width << "x" << out_height << '\n';
    if (scale < 1.0) {
        std::cerr << "Max resolution cap: " << max_resolution << " -> scaled to "
                  << out_width << "x" << out_height << '\n';
    }

    fs::path sat_path = out_root / (base.string() + "_sat_lco.png");
    try {
        PngStreamWriter png_writer(sat_path, out_width, out_height, 4);
        const size_t out_width_u = static_cast<size_t>(out_width);
        std::vector<uint8_t> row(canvas_width * 4ull);
        std::vector<uint8_t> scaled_row(out_width_u * 4ull);

        for (int out_y = 0; out_y < out_height; ++out_y) {
            std::fill(row.begin(), row.end(), 0);
            size_t src_y = static_cast<size_t>(static_cast<uint64_t>(out_y) * canvas_height
                                               / static_cast<uint64_t>(out_height));
            src_y = (canvas_height - 1) - src_y;
            size_t cell_y = src_y / static_cast<size_t>(tile_height);
            size_t in_tile_y = src_y % static_cast<size_t>(tile_height);

            size_t row_base = cell_y * static_cast<size_t>(width);

            for (int cell_x = 0; cell_x < width; ++cell_x) {
                size_t cell_idx = row_base + static_cast<size_t>(cell_x);
                if (cell_idx >= world.cell_texture_indexes.size()) continue;
                int tex_idx = static_cast<int>(world.cell_texture_indexes[cell_idx]);
                if (tex_idx <= 0 || tex_idx >= static_cast<int>(states.size())) continue;
                const auto& state = states[static_cast<size_t>(tex_idx)];
                if (!state.decoded) continue;

                const auto& image = state.image;
                const uint8_t* tile_row = image.pixels.data()
                    + static_cast<size_t>(in_tile_y) * static_cast<size_t>(tile_width) * 4ull;
                size_t dst_offset = static_cast<size_t>(cell_x) * static_cast<size_t>(tile_width) * 4ull;
                size_t copy_bytes = static_cast<size_t>(tile_width) * 4u;
                std::copy_n(tile_row, static_cast<std::vector<uint8_t>::difference_type>(copy_bytes),
                            row.begin() + static_cast<std::vector<uint8_t>::difference_type>(dst_offset));
            }

            if (scale == 1.0) {
                png_writer.write_row({row.data(), row.size()});
            } else {
                for (int out_x = 0; out_x < out_width; ++out_x) {
                    size_t src_x = static_cast<size_t>(static_cast<uint64_t>(out_x) * canvas_width
                                                       / static_cast<uint64_t>(out_width));
                    size_t src_offset = src_x * 4ull;
                    size_t dst_offset = static_cast<size_t>(out_x) * 4u;
                    std::copy_n(row.begin() + static_cast<std::vector<uint8_t>::difference_type>(src_offset),
                                4, scaled_row.begin() + static_cast<std::vector<uint8_t>::difference_type>(dst_offset));
                }
                png_writer.write_row({scaled_row.data(), scaled_row.size()});
            }

            if (verbose && (out_y % 256 == 0 || out_y == out_height - 1)) {
                int pct = static_cast<int>((static_cast<int64_t>(out_y + 1) * 100) / out_height);
                std::cerr << "["
                          << std::setw(3) << pct
                          << "%] "
                          << (out_y + 1)
                          << "/"
                          << out_height
                          << " rows\n";
            }
        }

        png_writer.finish();
    } catch (const std::exception& e) {
        std::cerr << "Error: streaming PNG failed: " << e.what() << '\n';
        return false;
    }

    std::cerr << "Tile cache entries decoded: "
              << (decoded_paa_indices + decoded_pac_indices)
              << " (paa: " << decoded_paa_indices
              << ", pac: " << decoded_pac_indices << ")\n";
    std::cerr << "Blank index0 cells: " << blank_index0_cells << '\n';
    std::cerr << "empty_name_indices: " << empty_name_indices << '\n';
    std::cerr << "failed_decode_indices: " << failed_decode_indices << '\n';

    return true;
}

static void print_usage() {
    std::cerr << "Usage: wrp_satmask --db <a3db.sqlite> [flags] <input.wrp>\n"
              << "Flags:\n"
              << "  --out <dir>      Output directory (default: input file directory)\n"
              << "  --dump-tiles     Print extracted tile paths/coords\n"
              << "  -v               Enable verbose logging\n"
              << "  --max-resolution N  Cap largest dimension to N (default: 0 for no cap)\n"
              << "  -h, --help       Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::string db_path;
    std::string out_dir;
    bool dump_tiles = false;
    bool verbose = false;
    int max_resolution = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--dump-tiles") == 0) {
            dump_tiles = true;
        } else if (std::strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--max-resolution") == 0 && i + 1 < argc) {
            try {
                max_resolution = std::max(0, std::stoi(argv[++i]));
            } catch (...) {
                std::cerr << "Error: invalid value for --max-resolution\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() != 1) {
        print_usage();
        return 1;
    }

    if (db_path.empty()) {
        std::cerr << "Error: --db <a3db.sqlite> is required\n";
        return 1;
    }

    std::string input_path = positional[0];
    fs::path input_fs = fs::path(input_path);
    if (out_dir.empty()) {
        out_dir = input_fs.parent_path().string();
    }
    fs::path out_root = fs::path(out_dir);
    fs::path base = input_fs.stem();
    std::error_code ec;
    fs::create_directories(out_root, ec);
    if (ec) {
        std::cerr << "Error: cannot create output directory " << out_root << '\n';
        return 1;
    }

    auto log_verbose = [&](const std::string& msg) {
        if (verbose) std::cerr << msg << '\n';
    };

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

    bool legacy_format = world.format.version == 2 || world.format.version == 3;

    log_verbose(std::format("WRP version: {}", world.format.version));
    log_verbose(std::format("MapInfo bytes: {}", world.map_info.size()));
    log_verbose(std::format("Grid: {}x{} cells, terrain {}x{}", world.grid.cells_x, world.grid.cells_y,
                world.grid.terrain_x, world.grid.terrain_y));

    std::shared_ptr<armatools::pboindex::DB> db;
    std::shared_ptr<armatools::pboindex::Index> index;
    try {
        db = std::make_shared<armatools::pboindex::DB>(armatools::pboindex::DB::open(db_path));
        index = std::make_shared<armatools::pboindex::Index>(db->index());
    } catch (const std::exception& e) {
        std::cerr << "Error: opening A3DB " << db_path << ": " << e.what() << '\n';
        return 1;
    }

    AssetProvider provider(index, db);

    if (legacy_format) {
        log_verbose("Processing legacy WRP path");
        if (!write_legacy_sat(world, provider, base, out_root, verbose, max_resolution)) {
            return 1;
        }
        return 0;
    }

    if (world.map_info.empty()) {
        std::cerr << "Warning: MapInfo block missing; cannot extract sat/mask tiles.\n";
        return 0;
    }

    TileCollections tiles = extract_tiles_from_mapinfo(world.map_info);
    log_verbose(std::format("Extracted {} sat tiles and {} mask tiles",
                             tiles.sat_tiles.size(), tiles.mask_tiles.size()));

    std::cout << std::format("Found {} sat tiles and {} mask tiles\n",
                              tiles.sat_tiles.size(), tiles.mask_tiles.size());
    if (tiles.sat_tiles.empty()) {
        std::cerr << "Warning: no sat tiles found in MapInfo; cannot generate outputs.\n";
        return 0;
    }

    if (dump_tiles) {
        auto dump = [&](const char* label, const std::vector<TileRef>& list) {
            std::cout << label << " tiles:\n";
            for (const auto& ref : list) {
                std::cout << "  " << ref.path << " [" << ref.x << "," << ref.y << "]\n";
            }
        };
        dump("Sat", tiles.sat_tiles);
        dump("Mask", tiles.mask_tiles);
    }

    auto sat_report = load_tiles(tiles.sat_tiles, provider);

    log_verbose(std::format("Sat tiles decoded: {}; missing {}; decode failures: {}",
                             sat_report.tiles.size(),
                             sat_report.missing_paths.size(),
                             sat_report.decode_failures.size()));
    if (sat_report.tiles.empty()) {
        std::cerr << "Error: no sat tiles could be decoded\n";
        return 1;
    }

    auto sat_mosaic = build_mosaic(sat_report.tiles);
    if (!sat_mosaic) {
        std::cerr << "Error: failed to build sat mosaic\n";
        return 1;
    }

    fs::path sat_path = out_root / (base.string() + "_sat_lco.png");

    if (!write_png(sat_path, *sat_mosaic)) {
        std::cerr << "Error: could not write " << sat_path << '\n';
        return 1;
    }

    std::cout << std::format("Sat mosaic: {}x{} pixels (tile {}x{})\n",
                              sat_mosaic->width, sat_mosaic->height,
                              sat_mosaic->tile_width, sat_mosaic->tile_height);

    if (!sat_report.missing_paths.empty()) {
        std::cerr << "Missing sat tiles (" << sat_report.missing_paths.size() << "):\n";
        for (const auto& path : sat_report.missing_paths) {
            std::cerr << "  " << path << '\n';
        }
    }
    if (!sat_report.decode_failures.empty()) {
        std::cerr << "Sat decode failures (" << sat_report.decode_failures.size() << "):\n";
        for (const auto& path : sat_report.decode_failures) {
            std::cerr << "  " << path << '\n';
        }
    }

    if (!legacy_format && !tiles.mask_tiles.empty()) {
        auto mask_report = load_tiles(tiles.mask_tiles, provider);
        auto mask_mosaic = build_mosaic(mask_report.tiles);
        if (mask_mosaic && mask_mosaic->placed_tiles > 0) {
            fs::path mask_path = out_root / (base.string() + "_mask_lco.png");
            if (!write_png(mask_path, *mask_mosaic)) {
                std::cerr << "Error: could not write " << mask_path << '\n';
                return 1;
            }
            std::cout << std::format("Mask mosaic: {}x{} pixels (tile {}x{})\n",
                                      mask_mosaic->width, mask_mosaic->height,
                                      mask_mosaic->tile_width, mask_mosaic->tile_height);
            log_verbose(std::format("Mask mosaic tiles placed: {}", mask_mosaic->placed_tiles));
        } else {
            std::cerr << "Warning: mask tiles could not be assembled\n";
        }

        if (!mask_report.missing_paths.empty()) {
            std::cerr << "Missing mask tiles (" << mask_report.missing_paths.size() << "):\n";
            for (const auto& path : mask_report.missing_paths) {
                std::cerr << "  " << path << '\n';
            }
        }
        if (!mask_report.decode_failures.empty()) {
            std::cerr << "Mask decode failures (" << mask_report.decode_failures.size() << "):\n";
            for (const auto& path : mask_report.decode_failures) {
                std::cerr << "  " << path << '\n';
            }
        }
    } else if (legacy_format) {
        std::cout << "Legacy WRP format detected; mask output skipped\n";
    } else if (tiles.mask_tiles.empty()) {
        std::cout << "No mask tiles present; mask output skipped\n";
    }

    return 0;
}
