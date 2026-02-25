#include "asset_provider.h"
#include "mapinfo_tiles.h"
#include "mosaic.h"

#include <armatools/wrp.h>
#include <armatools/pboindex.h>
#include <armatools/rvmat.h>

#include "png_stream_writer.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <format>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
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

static bool is_procedural_texture(std::string_view tex) {
    return tex.starts_with("#(");
}

static int texture_rank(std::string_view lower) {
    if (lower.find("_sat_lco") != std::string_view::npos) return 0;
    if (lower.find("_lco") != std::string_view::npos) return 1;
    if (lower.find("_co") != std::string_view::npos) return 2;
    if (lower.find("_d") != std::string_view::npos) return 3;
    return 10;
}

static std::string normalize_slashes(std::string path) {
    for (auto& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

static std::optional<std::pair<int, int>> parse_coords_from_name(std::string_view name) {
    for (size_t i = 0; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) continue;
        size_t j = i;
        int x = 0;
        while (j < name.size() && std::isdigit(static_cast<unsigned char>(name[j]))) {
            x = x * 10 + (name[j] - '0');
            j++;
        }
        if (j >= name.size() || name[j] != '-') continue;
        size_t k = j + 1;
        int y = 0;
        size_t y_start = k;
        while (k < name.size() && std::isdigit(static_cast<unsigned char>(name[k]))) {
            y = y * 10 + (name[k] - '0');
            k++;
        }
        if (k == y_start) continue;
        return std::make_pair(x, y);
    }
    return std::nullopt;
}

static std::string format_tile_name(const std::string& prefix, int x, int y, std::string_view suffix,
                                    std::string_view ext) {
    std::ostringstream out;
    out << prefix << std::setfill('0') << std::setw(3) << x
        << "-" << std::setfill('0') << std::setw(3) << y << suffix << ext;
    return out.str();
}

static std::vector<std::string> build_sat_tile_candidates(const std::string& rvmat_path) {
    std::string normalized = normalize_slashes(rvmat_path);
    fs::path rvmat_fs(normalized);
    std::string base = rvmat_fs.stem().string();
    std::string dir = rvmat_fs.parent_path().string();
    if (!dir.empty() && dir.back() != '/') dir.push_back('/');

    std::optional<std::pair<int, int>> coords = parse_coords_from_name(base);
    if (!coords) coords = parse_coords_from_name(normalized);

    std::string prefix_base;
    if (auto pos = base.find("_l"); pos != std::string::npos) {
        prefix_base = base.substr(0, pos);
    } else {
        prefix_base = base;
    }

    std::vector<std::string> candidates;
    const std::array<std::string_view, 4> exts = {".paa", ".png", ".tga", ".pac"};
    if (coords) {
        for (auto ext : exts) {
            candidates.push_back(dir + format_tile_name("p_", coords->first, coords->second, "_sat_lco", ext));
            candidates.push_back(dir + format_tile_name("s_", coords->first, coords->second, "_lco", ext));
            candidates.push_back(dir + format_tile_name("s_", coords->first, coords->second, "_sat_lco", ext));
        }
    }
    if (!prefix_base.empty()) {
        for (auto ext : exts) {
            candidates.push_back(dir + prefix_base + "_sat_lco" + std::string(ext));
            candidates.push_back(dir + prefix_base + "_lco" + std::string(ext));
        }
    }

    return candidates;
}

static std::string resolve_texture_path(const std::string& rvmat_path, const std::string& texture_path) {
    if (texture_path.empty()) return "";
    if (texture_path.find('\\') != std::string::npos || texture_path.find('/') != std::string::npos) {
        return texture_path;
    }
    fs::path base = fs::path(rvmat_path).parent_path();
    return (base / texture_path).string();
}

static std::string select_stage_texture(const armatools::rvmat::Material& mat) {
    int best_rank = 100;
    int best_stage = 1000;
    std::string best;
    for (const auto& st : mat.stages) {
        if (st.texture_path.empty()) continue;
        if (is_procedural_texture(st.texture_path)) continue;
        auto lower = to_lower_ascii(st.texture_path);
        int rank = texture_rank(lower);
        int stage = st.stage_number >= 0 ? st.stage_number : 1000;
        if (rank < best_rank || (rank == best_rank && stage < best_stage)) {
            best_rank = rank;
            best_stage = stage;
            best = st.texture_path;
        }
    }
    return best;
}

struct RvmatTextureInfo {
    bool attempted = false;
    bool ok = false;
    std::string texture_path;
};

struct ModernTextureState {
    bool attempted = false;
    bool decoded = false;
    bool blank_name = false;
    bool decode_failed = false;
    bool pac = false;
    std::shared_ptr<armatools::paa::Image> image;
};

static bool write_modern_sat_from_rvmat(const armatools::wrp::WorldData& world,
                                        const AssetProvider& provider,
                                        const fs::path& base,
                                        const fs::path& out_root,
                                        bool verbose,
                                        int max_resolution) {
    auto log_verbose = [&](const std::string& msg) {
        if (verbose) std::cerr << msg << '\n';
    };
    log_verbose("Starting modern SAT generation from RVMAT");

    int width = world.grid.cells_x;
    int height = world.grid.cells_y;
    size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (world.cell_texture_indexes.size() != expected) {
        std::cerr << "Error: cell texture count mismatch\n";
        return false;
    }
    if (world.textures.empty()) {
        std::cerr << "Error: no texture entries available\n";
        return false;
    }

    std::vector<ModernTextureState> states(world.textures.size());
    std::unordered_map<std::string, RvmatTextureInfo> rvmat_cache;
    std::unordered_map<std::string, bool> exists_cache;
    std::unordered_map<std::string, std::shared_ptr<armatools::paa::Image>> texture_cache;

    int tile_width = 0;
    int tile_height = 0;
    int decoded_indices = 0;
    int failed_decode_indices = 0;
    int empty_name_indices = 0;
    int blank_index0_cells = 0;

    std::vector<int> used_indices;
    used_indices.reserve(expected);
    {
        std::vector<uint8_t> seen(states.size(), 0);
        for (size_t i = 0; i < expected; ++i) {
            int idx = static_cast<int>(world.cell_texture_indexes[i]);
            if (idx == 0) {
                ++blank_index0_cells;
                continue;
            }
            if (idx < 0 || idx >= static_cast<int>(states.size())) continue;
            if (seen[static_cast<size_t>(idx)]) continue;
            seen[static_cast<size_t>(idx)] = 1;
            used_indices.push_back(idx);
        }
    }
    const size_t progress_step = std::max<size_t>(1, used_indices.size() / 20);
    log_verbose(std::format("Unique texture indices: {}", used_indices.size()));

    auto exists_cached = [&](const std::string& path) -> bool {
        auto it = exists_cache.find(path);
        if (it != exists_cache.end()) return it->second;
        bool ok = provider.exists(path);
        exists_cache.emplace(path, ok);
        return ok;
    };

    auto resolve_rvmat_texture = [&](const std::string& rvmat_path) -> std::string {
        auto& info = rvmat_cache[rvmat_path];
        if (info.attempted) return info.ok ? info.texture_path : "";
        info.attempted = true;
        if (verbose) {
            std::cerr << "Resolve RVMAT: " << rvmat_path << '\n';
        }
        for (const auto& candidate : build_sat_tile_candidates(rvmat_path)) {
            if (exists_cached(candidate)) {
                info.texture_path = candidate;
                info.ok = true;
                return info.texture_path;
            }
        }
        if (verbose) {
            std::cerr << "Reading RVMAT bytes: " << rvmat_path << '\n';
        }
        auto bytes = provider.read(rvmat_path);
        if (!bytes) return "";
        if (verbose) {
            std::cerr << "RVMAT bytes: " << rvmat_path << " size=" << bytes->size() << '\n';
        }
        constexpr size_t kMaxRvmatSize = 2 * 1024 * 1024;
        if (bytes->size() > kMaxRvmatSize) {
            std::cerr << "Warning: skipping oversized RVMAT (" << bytes->size()
                      << " bytes): " << rvmat_path << '\n';
            return "";
        }
        std::string data(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        try {
            auto mat = armatools::rvmat::parse_bytes(data);
            auto tex = select_stage_texture(mat);
            if (tex.empty()) return "";
            info.texture_path = resolve_texture_path(rvmat_path, tex);
            info.ok = !info.texture_path.empty();
            return info.ok ? info.texture_path : "";
        } catch (const std::exception&) {
            return "";
        }
    };

    for (size_t i = 0; i < used_indices.size(); ++i) {
        int idx = used_indices[i];
        if (verbose && (i == 0 || (i + 1) % progress_step == 0 || i + 1 == used_indices.size())) {
            int pct = static_cast<int>(((i + 1) * 100) / std::max<size_t>(1, used_indices.size()));
            std::cerr << "["
                      << std::setw(3) << pct
                      << "%] "
                      << (i + 1)
                      << "/"
                      << used_indices.size()
                      << " textures scanned\n";
        }
        auto& state = states[static_cast<size_t>(idx)];
        state.attempted = true;

        const auto& entry = world.textures[static_cast<size_t>(idx)];
        std::string tex_path;
        std::string rvmat_used;
        if (verbose && i == 0) {
            if (!entry.filenames.empty()) {
                std::cerr << "First texture entry candidates: " << entry.filenames.size() << '\n';
                for (size_t k = 0; k < std::min<size_t>(entry.filenames.size(), 3); ++k) {
                    std::cerr << "  rvmat[" << k << "] " << entry.filenames[k] << '\n';
                }
            } else {
                std::cerr << "First texture entry filename: " << entry.filename << '\n';
            }
        }
        auto t0 = std::chrono::steady_clock::now();
        if (!entry.filenames.empty()) {
            for (const auto& candidate : entry.filenames) {
                if (candidate.empty()) continue;
                tex_path = resolve_rvmat_texture(candidate);
                if (!tex_path.empty()) {
                    rvmat_used = candidate;
                    break;
                }
            }
        } else if (!entry.filename.empty()) {
            tex_path = resolve_rvmat_texture(entry.filename);
            if (!tex_path.empty()) rvmat_used = entry.filename;
        }
        if (tex_path.empty()) {
            if (entry.filenames.empty() && entry.filename.empty()) {
                state.blank_name = true;
                ++empty_name_indices;
            } else {
                state.decode_failed = true;
                ++failed_decode_indices;
            }
            continue;
        }
        if (verbose && i == 0) {
            auto t1 = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            std::cerr << "First resolve time: " << ms << " ms\n";
        }

        fs::path tex_fs(tex_path);
        auto ext = to_lower_ascii(tex_fs.extension().string());
        bool is_pac = ext == ".pac";

        auto cache_it = texture_cache.find(tex_path);
        if (cache_it == texture_cache.end()) {
            auto bytes = provider.read(tex_path);
            if (!bytes && !rvmat_used.empty()) {
                fs::path base_dir = fs::path(rvmat_used).parent_path();
                auto alt_path = (base_dir / tex_path).string();
                if (alt_path != tex_path) {
                    bytes = provider.read(alt_path);
                    if (bytes) tex_path = std::move(alt_path);
                }
            }
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

                auto shared = std::make_shared<armatools::paa::Image>(std::move(image));
                cache_it = texture_cache.emplace(tex_path, shared).first;
                ++decoded_indices;
            } catch (const std::exception&) {
                state.decode_failed = true;
                ++failed_decode_indices;
                continue;
            }
        }

        state.decoded = true;
        state.pac = is_pac;
        state.image = cache_it->second;

    }

    if (tile_width == 0 || tile_height == 0) {
        std::cerr << "Error: no tile could be decoded from RVMAT\n";
        return false;
    }
    log_verbose(std::format("Tile dimensions: {}x{}", tile_width, tile_height));

    uint64_t canvas_width64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(tile_width);
    uint64_t canvas_height64 = static_cast<uint64_t>(height) * static_cast<uint64_t>(tile_height);
    if (canvas_width64 == 0 || canvas_height64 == 0) {
        std::cerr << "Error: invalid canvas dimensions\n";
        return false;
    }
    log_verbose(std::format("Canvas size: {}x{}", canvas_width64, canvas_height64));

    uint64_t pixel_count64 = canvas_width64 * canvas_height64;
    const uint64_t pixels_limit = std::numeric_limits<size_t>::max() / 4;
    if (pixel_count64 > pixels_limit) {
        std::cerr << "Error: canvas too large to allocate (" << pixel_count64 * 4 << " bytes)\n";
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

                const auto& image = *state.image;
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

    std::cerr << "Tile cache entries decoded: " << decoded_indices << '\n';
    std::cerr << "Blank index0 cells: " << blank_index0_cells << '\n';
    std::cerr << "empty_name_indices: " << empty_name_indices << '\n';
    std::cerr << "failed_decode_indices: " << failed_decode_indices << '\n';

    return true;
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

    TileCollections tiles;
    if (!world.map_info.empty()) {
        tiles = extract_tiles_from_mapinfo(world.map_info);
        log_verbose(std::format("Extracted {} sat tiles and {} mask tiles",
                                 tiles.sat_tiles.size(), tiles.mask_tiles.size()));
        std::cout << std::format("Found {} sat tiles and {} mask tiles\n",
                                  tiles.sat_tiles.size(), tiles.mask_tiles.size());
    }
    if (tiles.sat_tiles.empty()) {
        std::cerr << "Warning: no sat tiles found in MapInfo; falling back to RVMAT-based SAT.\n";
        if (!write_modern_sat_from_rvmat(world, provider, base, out_root, verbose, max_resolution)) {
            return 1;
        }
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
