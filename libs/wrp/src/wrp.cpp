#include <armatools/wrp.h>

#include <armatools/binutil.h>
#include <armatools/lzss.h>
#include <armatools/lzo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace armatools::wrp {

using namespace armatools::binutil;
constexpr double kPi = 3.141592653589793238462643383279502884;

// ---------------------------------------------------------------------------
// Forward declarations (internal helpers)
// ---------------------------------------------------------------------------

static WorldData read_oprw(std::istream& r, Options opts);
static WorldData read_4wvr(std::istream& r, Options opts);
static WorldData read_1wvr(std::istream& r, Options opts);

static WorldData read_oprw_legacy(std::istream& r, int ver, Options opts);
static WorldData read_oprw_modern(std::istream& r, int version, Options opts);

static std::vector<uint8_t> read_quad_tree(std::istream& r, int size_x, int size_y, int elem_size);
static void skip_quad_tree(std::istream& r);

static void read_1wvr_nets(std::istream& r, WorldData& w);

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

static double rad_to_deg(double rad) {
    return rad * 180.0 / kPi;
}

static double clamp_val(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static std::istringstream make_stream(const std::vector<uint8_t>& data) {
    std::string s(reinterpret_cast<const char*>(data.data()), data.size());
    return std::istringstream(std::move(s), std::ios::binary);
}

static CellFlagsInfo compute_cell_flags(const std::vector<uint32_t>& flags) {
    CellFlagsInfo cfi{};
    cfi.total_cells = static_cast<int>(flags.size());
    for (auto f : flags) {
        if (f & 0x20) cfi.forest_cells++;
        if (f & 0x40) cfi.roadway_cells++;
        switch (f & 0x03) {
        case 0: cfi.surface.ground++; break;
        case 1: cfi.surface.tidal++; break;
        case 2: cfi.surface.coastline++; break;
        case 3: cfi.surface.sea++; break;
        }
    }
    return cfi;
}

static void compute_elevation_bounds(const std::vector<float>& elev, BoundsInfo& bounds) {
    double min_e = std::numeric_limits<float>::max();
    double max_e = -std::numeric_limits<float>::max();
    for (auto e : elev) {
        double ev = static_cast<double>(e);
        if (ev < min_e) min_e = ev;
        if (ev > max_e) max_e = ev;
    }
    bounds.min_elevation = min_e;
    bounds.max_elevation = max_e;
}


// Build unique model list from objects and assign model_index back.
static void build_model_index(WorldData& w) {
    std::map<std::string, int> model_map;
    for (auto& obj : w.objects) {
        if (model_map.find(obj.model_name) == model_map.end()) {
            int idx = static_cast<int>(w.models.size());
            model_map[obj.model_name] = idx;
            w.models.push_back(obj.model_name);
        }
    }
    w.stats.model_count = static_cast<int>(w.models.size());
    for (auto& obj : w.objects) {
        auto it = model_map.find(obj.model_name);
        if (it != model_map.end()) {
            obj.model_index = it->second;
        }
    }
}

// ---------------------------------------------------------------------------
// extract_position_rotation
// ---------------------------------------------------------------------------

void extract_position_rotation(const std::array<float, 12>& m_in,
                                std::array<double, 3>& pos, Rotation& rot, double& scale)
{
    // Sanitize NaN/Inf values
    std::array<float, 12> m = m_in;
    for (auto& v : m) {
        double d = static_cast<double>(v);
        if (std::isnan(d) || std::isinf(d)) {
            v = 0.0f;
        }
    }

    // Position is the 4th row
    pos[0] = static_cast<double>(m[9]);
    pos[1] = static_cast<double>(m[10]);
    pos[2] = static_cast<double>(m[11]);

    // Extract scale from column vector norms of the 3x3 part
    double scale_x = std::sqrt(double(m[0])*m[0] + double(m[3])*m[3] + double(m[6])*m[6]);
    double scale_y = std::sqrt(double(m[1])*m[1] + double(m[4])*m[4] + double(m[7])*m[7]);
    double scale_z = std::sqrt(double(m[2])*m[2] + double(m[5])*m[5] + double(m[8])*m[8]);
    scale = (scale_x + scale_y + scale_z) / 3.0;

    // Normalize the 3x3 rotation part
    std::array<double, 9> r{};
    if (scale_x > 1e-6) {
        r[0] = double(m[0]) / scale_x;
        r[3] = double(m[3]) / scale_x;
        r[6] = double(m[6]) / scale_x;
    }
    if (scale_y > 1e-6) {
        r[1] = double(m[1]) / scale_y;
        r[4] = double(m[4]) / scale_y;
        r[7] = double(m[7]) / scale_y;
    }
    if (scale_z > 1e-6) {
        r[2] = double(m[2]) / scale_z;
        r[5] = double(m[5]) / scale_z;
        r[8] = double(m[8]) / scale_z;
    }

    // x = acos(clamp(r33))
    double r33 = clamp_val(r[8], -1.0, 1.0);
    double x_rad = std::acos(r33);
    double sin_x = std::sin(x_rad);

    double y_rad, z_rad;

    if (std::abs(sin_x) > 1e-6) {
        y_rad = std::asin(clamp_val(r[6] / sin_x, -1.0, 1.0));
        z_rad = std::asin(clamp_val(r[5] / sin_x, -1.0, 1.0));
    } else {
        // Gimbal lock
        y_rad = std::atan2(-r[1], r[0]);
        z_rad = 0.0;
    }

    rot.pitch = rad_to_deg(x_rad);
    rot.yaw   = rad_to_deg(y_rad);
    rot.roll  = rad_to_deg(z_rad);
}

// ---------------------------------------------------------------------------
// read (top-level dispatcher)
// ---------------------------------------------------------------------------

WorldData read(std::istream& r, Options opts) {
    auto sig = read_signature(r);

    if (sig == "OPRW") return read_oprw(r, opts);
    if (sig == "4WVR") return read_4wvr(r, opts);
    if (sig == "1WVR") return read_1wvr(r, opts);

    throw std::runtime_error(std::format("unknown WRP signature: \"{}\"", sig));
}

// ---------------------------------------------------------------------------
// OPRW dispatcher
// ---------------------------------------------------------------------------

static WorldData read_oprw(std::istream& r, Options opts) {
    uint32_t ver = read_u32(r);
    if (ver == 2 || ver == 3) {
        return read_oprw_legacy(r, static_cast<int>(ver), opts);
    }
    if (ver >= 12 && ver <= 25) {
        return read_oprw_modern(r, static_cast<int>(ver), opts);
    }
    throw std::runtime_error(std::format("oprw: unsupported version {}", ver));
}

// ---------------------------------------------------------------------------
// OPRW Legacy (v2/v3)
// ---------------------------------------------------------------------------

static WorldData read_oprw_legacy(std::istream& r, int ver, Options opts) {
    WorldData w;
    w.format = {"OPRW", ver};

    int layer_x = 256, layer_y = 256;
    int map_x = 256, map_y = 256;

    if (ver == 3) {
        layer_x = static_cast<int>(read_u32(r));
        layer_y = static_cast<int>(read_u32(r));
        map_x   = static_cast<int>(read_u32(r));
        map_y   = static_cast<int>(read_u32(r));
    }

    w.grid = {layer_x, layer_y, 50.0, map_x, map_y};

    int layer_cells = layer_x * layer_y;
    int map_cells = map_x * map_y;

    // 1. PackedCellBitFlags: uint32[layerCells] -- LZSS compressed
    {
        auto data = lzss::decompress_or_raw(r, static_cast<size_t>(layer_cells) * 4);
        auto s = make_stream(data);
        w.cell_bit_flags = read_u32_slice(s, static_cast<size_t>(layer_cells));
    }

    // Compute cell flags summary
    w.stats.cell_flags = compute_cell_flags(w.cell_bit_flags);
    w.stats.has_cell_flags = true;

    // 2. PackedCellEnvSounds: byte[layerCells] -- LZSS compressed
    w.cell_env_sounds = lzss::decompress_or_raw(r, static_cast<size_t>(layer_cells));

    // 3. nPeaks + XYZTriplet[nPeaks]
    {
        uint32_t n_peaks = read_u32(r);
        w.stats.peak_count = static_cast<int>(n_peaks);
        if (n_peaks > 0) {
            auto peak_data = read_bytes(r, static_cast<size_t>(n_peaks) * 12);
            auto s = make_stream(peak_data);
            w.peaks.resize(n_peaks);
            for (uint32_t i = 0; i < n_peaks; i++) {
                auto floats = read_f32_slice(s, 3);
                w.peaks[i] = {floats[0], floats[1], floats[2]};
            }
        }
    }

    // 4. PackedCellTextureIndexes: ushort[layerCells] -- LZSS compressed
    {
        auto data = lzss::decompress_or_raw(r, static_cast<size_t>(layer_cells) * 2);
        auto s = make_stream(data);
        w.cell_texture_indexes = read_u16_slice(s, static_cast<size_t>(layer_cells));
    }

    // 5. PackedCellExtFlags: uint32[layerCells] -- LZSS compressed
    {
        auto data = lzss::decompress_or_raw(r, static_cast<size_t>(layer_cells) * 4);
        auto s = make_stream(data);
        w.cell_ext_flags = read_u32_slice(s, static_cast<size_t>(layer_cells));
    }

    // 6. PackedCellElevations: float[mapCells] -- LZSS compressed
    {
        auto data = lzss::decompress_or_raw(r, static_cast<size_t>(map_cells) * 4);
        auto s = make_stream(data);
        w.elevations = read_f32_slice(s, static_cast<size_t>(map_cells));
    }

    compute_elevation_bounds(w.elevations, w.bounds);
    w.bounds.world_size_x = static_cast<double>(layer_x) * w.grid.cell_size;
    w.bounds.world_size_y = static_cast<double>(layer_y) * w.grid.cell_size;

    // 7. nTextures + Texture[n]
    {
        uint32_t n_tex = read_u32(r);
        w.textures.resize(n_tex);
        for (uint32_t i = 0; i < n_tex; i++) {
            w.textures[i].filename = read_asciiz(r);
            w.textures[i].color = read_u8(r);
        }
        w.stats.texture_count = static_cast<int>(n_tex);
    }

    // 8. nModels + Model[n]
    {
        uint32_t n_models = read_u32(r);
        w.models.resize(n_models);
        for (uint32_t i = 0; i < n_models; i++) {
            w.models[i] = read_asciiz(r);
        }
        w.stats.model_count = static_cast<int>(n_models);
    }

    // 9. Objects (terminated by 0xFFFFFFFF sentinel)
    if (!opts.no_objects) {
        for (;;) {
            uint32_t obj_id = read_u32(r);
            if (obj_id == 0xFFFFFFFF) break;

            uint32_t model_idx = read_u32(r);
            auto transform = read_transform_matrix(r);

            int mi = static_cast<int>(model_idx);
            std::string model_name;
            if (mi >= 0 && mi < static_cast<int>(w.models.size())) {
                model_name = w.models[static_cast<size_t>(mi)];
            } else {
                w.warnings.push_back({
                    "INVALID_MODEL_INDEX",
                    std::format("object {} references model index {} (max {})",
                                obj_id, model_idx, w.models.size() - 1)
                });
            }

            std::array<double, 3> pos{};
            Rotation rot{};
            double scale = 1.0;
            extract_position_rotation(transform, pos, rot, scale);

            w.objects.push_back({obj_id, mi, model_name, transform, pos, rot, scale});
        }
    } else {
        for (;;) {
            uint32_t obj_id = read_u32(r);
            if (obj_id == 0xFFFFFFFF) break;
            read_bytes(r, 52); // model_idx(4) + transform(48)
        }
    }

    w.stats.object_count = static_cast<int>(w.objects.size());

    w.warnings.push_back({"ROADS_UNSUPPORTED", "OPRW format does not contain road/net data"});

    return w;
}

// ---------------------------------------------------------------------------
// QuadTree
// ---------------------------------------------------------------------------

static int ceil_log2(int n) {
    if (n <= 1) return 0;
    n--;
    int bits = 0;
    while (n != 0) {
        bits++;
        n >>= 1;
    }
    return bits;
}

static std::pair<int, int> calculate_log_dimensions(int size_x, int size_y,
                                                      int leaf_log_x, int leaf_log_y,
                                                      int log_branch)
{
    int log_x = ceil_log2(size_x);
    int log_y = ceil_log2(size_y);

    int num_levels_x = (log_x - leaf_log_x + log_branch - 1) / log_branch;
    int num_levels_y = (log_y - leaf_log_y + log_branch - 1) / log_branch;
    int num_levels = std::max(num_levels_x, num_levels_y);

    int log_total_x = num_levels * log_branch + leaf_log_x;
    int log_total_y = num_levels * log_branch + leaf_log_y;

    return {log_total_x, log_total_y};
}

static void fill_leaf(std::vector<uint8_t>& buf, int stride,
                       int x0, int y0, int w, int h,
                       const uint8_t* leaf, int elem_size,
                       int leaf_log_x, int leaf_log_y)
{
    int leaf_w = 1 << leaf_log_x;
    int leaf_h = 1 << leaf_log_y;

    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int lx = dx % leaf_w;
            int ly = dy % leaf_h;

            const uint8_t* elem_data;
            switch (elem_size) {
            case 4:
                elem_data = leaf;
                break;
            case 2:
                elem_data = leaf + lx * 2;
                break;
            case 1:
            default:
                elem_data = leaf + (ly * leaf_w + lx);
                break;
            }

            size_t dst_off = static_cast<size_t>((y0 + dy) * stride + (x0 + dx)) * static_cast<size_t>(elem_size);
            std::memcpy(&buf[dst_off], elem_data, static_cast<size_t>(elem_size));
        }
    }
}

static void read_quad_tree_node(std::istream& r, std::vector<uint8_t>& buf, int stride,
                                 int x0, int y0, int w, int h,
                                 int elem_size, int leaf_log_x, int leaf_log_y)
{
    uint16_t flag_mask = read_u16(r);

    int child_w = w / 4;
    int child_h = h / 4;

    for (int i = 0; i < 16; i++) {
        int cx = x0 + (i % 4) * child_w;
        int cy = y0 + (i / 4) * child_h;

        if (flag_mask & (1 << i)) {
            // Subtree node
            read_quad_tree_node(r, buf, stride, cx, cy, child_w, child_h,
                                elem_size, leaf_log_x, leaf_log_y);
        } else {
            // Leaf: 4 bytes
            auto leaf_data = read_bytes(r, 4);
            fill_leaf(buf, stride, cx, cy, child_w, child_h,
                      leaf_data.data(), elem_size, leaf_log_x, leaf_log_y);
        }
    }
}

static std::vector<uint8_t> read_quad_tree(std::istream& r, int size_x, int size_y, int elem_size) {
    if (elem_size != 1 && elem_size != 2 && elem_size != 4) {
        throw std::runtime_error(
            std::format("quadtree: invalid elem_size {} (must be 1, 2, or 4)", elem_size));
    }

    int leaf_log_x, leaf_log_y;
    switch (elem_size) {
    case 1:  leaf_log_x = 1; leaf_log_y = 1; break; // 2x2
    case 2:  leaf_log_x = 1; leaf_log_y = 0; break; // 2x1
    default: leaf_log_x = 0; leaf_log_y = 0; break; // 1x1
    }

    constexpr int log_branch = 2;

    auto [log_total_x, log_total_y] = calculate_log_dimensions(
        size_x, size_y, leaf_log_x, leaf_log_y, log_branch);

    int total_x = 1 << log_total_x;
    int total_y = 1 << log_total_y;

    std::vector<uint8_t> virtual_buf(
        static_cast<size_t>(total_x) * static_cast<size_t>(total_y) * static_cast<size_t>(elem_size), 0);

    // Read root flag
    uint8_t flag = read_u8(r);

    if (flag == 0) {
        // Single leaf covers entire grid
        auto leaf_data = read_bytes(r, 4);
        fill_leaf(virtual_buf, total_x, 0, 0, total_x, total_y,
                  leaf_data.data(), elem_size, leaf_log_x, leaf_log_y);
    } else {
        read_quad_tree_node(r, virtual_buf, total_x, 0, 0, total_x, total_y,
                            elem_size, leaf_log_x, leaf_log_y);
    }

    // Extract actual grid from virtual grid
    if (total_x == size_x && total_y == size_y) {
        return virtual_buf;
    }

    std::vector<uint8_t> out(static_cast<size_t>(size_x) * static_cast<size_t>(size_y) * static_cast<size_t>(elem_size));
    for (int y = 0; y < size_y; y++) {
        size_t src_off = static_cast<size_t>(y) * static_cast<size_t>(total_x) * static_cast<size_t>(elem_size);
        size_t dst_off = static_cast<size_t>(y) * static_cast<size_t>(size_x) * static_cast<size_t>(elem_size);
        std::memcpy(&out[dst_off], &virtual_buf[src_off],
                     static_cast<size_t>(size_x) * static_cast<size_t>(elem_size));
    }
    return out;
}

static void skip_quad_tree_node(std::istream& r) {
    uint16_t flag_mask = read_u16(r);
    for (int i = 0; i < 16; i++) {
        if (flag_mask & (1 << i)) {
            skip_quad_tree_node(r);
        } else {
            read_bytes(r, 4);
        }
    }
}

static void skip_quad_tree(std::istream& r) {
    uint8_t flag = read_u8(r);
    if (flag == 0) {
        read_bytes(r, 4);
        return;
    }
    skip_quad_tree_node(r);
}

// ---------------------------------------------------------------------------
// OPRW Modern (v12-25)
// ---------------------------------------------------------------------------

struct OprwModernParser {
    std::istream& r;
    int version;
    Options opts;
    WorldData w;

    std::vector<uint8_t> read_compressed(size_t expected_size) {
        if (version >= 23) {
            return lzo::decompress_or_raw(r, expected_size);
        }
        return lzss::decompress_or_raw(r, expected_size);
    }

    RoadLink read_road_link() {
        RoadLink link;

        uint16_t conn_count = read_u16(r);

        link.positions.resize(conn_count);
        for (size_t i = 0; i < conn_count; i++) {
            auto floats = read_f32_slice(r, 3);
            link.positions[i] = {floats[0], floats[1], floats[2]};
        }

        if (version >= 24) {
            link.connection_types.resize(conn_count);
            for (size_t i = 0; i < conn_count; i++) {
                link.connection_types[i] = read_u8(r);
            }
        }

        link.object_id = read_i32(r);

        if (version >= 16) {
            link.p3d_path = read_asciiz(r);
            link.transform = read_transform_matrix(r);
            extract_position_rotation(link.transform, link.position, link.rotation, link.scale);
        }

        return link;
    }

    WorldData parse() {
        w.format = {"OPRW", version};

        // 1. AppID (v>=25)
        if (version >= 25) {
            w.app_id = static_cast<int>(read_i32(r));
        }

        // 2. LandRange, TerrainRange, CellSize
        int land_range_x = 0, land_range_y = 0;
        int terrain_range_x = 0, terrain_range_y = 0;
        float cell_size = 0.0f;

        if (version >= 12) {
            land_range_x    = static_cast<int>(read_i32(r));
            land_range_y    = static_cast<int>(read_i32(r));
            terrain_range_x = static_cast<int>(read_i32(r));
            terrain_range_y = static_cast<int>(read_i32(r));
            cell_size       = read_f32(r);
        }

        w.grid = {land_range_x, land_range_y, static_cast<double>(cell_size),
                   terrain_range_x, terrain_range_y};

        int land_cells = land_range_x * land_range_y;
        int terrain_cells = terrain_range_x * terrain_range_y;

        // 3. Geography QuadTree (int16, elemSize=2)
        {
            auto geo_data = read_quad_tree(r, land_range_x, land_range_y, 2);
            auto s = make_stream(geo_data);
            auto geo_flags = read_u16_slice(s, static_cast<size_t>(land_cells));
            w.cell_bit_flags.resize(static_cast<size_t>(land_cells));
            for (size_t i = 0; i < static_cast<size_t>(land_cells); i++) {
                w.cell_bit_flags[i] = static_cast<uint32_t>(geo_flags[i]);
            }
        }

        w.stats.cell_flags = compute_cell_flags(w.cell_bit_flags);
        w.stats.has_cell_flags = true;

        // 4. SoundMap QuadTree (byte, elemSize=1)
        w.cell_env_sounds = read_quad_tree(r, land_range_x, land_range_y, 1);

        // 5. Mountains: count(int32) + Vector3P[]
        {
            int32_t n_peaks = read_i32(r);
            w.stats.peak_count = static_cast<int>(n_peaks);
            if (n_peaks > 0) {
                w.peaks.resize(static_cast<size_t>(n_peaks));
                for (size_t i = 0; i < static_cast<size_t>(n_peaks); i++) {
                    auto floats = read_f32_slice(r, 3);
                    w.peaks[i] = {floats[0], floats[1], floats[2]};
                }
            }
        }

        // 6. Materials QuadTree (uint16, elemSize=2)
        {
            auto mat_data = read_quad_tree(r, land_range_x, land_range_y, 2);
            auto s = make_stream(mat_data);
            w.cell_texture_indexes = read_u16_slice(s, static_cast<size_t>(land_cells));
        }

        // 7. Random (v<21): compressed (LandRange*2 bytes)
        if (version < 21) {
            read_compressed(static_cast<size_t>(land_cells) * 2);
        }

        // 8. GrassApprox (v>=18): compressed (TerrainRange bytes)
        if (version >= 18) {
            read_compressed(static_cast<size_t>(terrain_cells));
        }

        // 9. PrimTexIndex (v>=22): compressed (TerrainRange bytes)
        if (version >= 22) {
            read_compressed(static_cast<size_t>(terrain_cells));
        }

        // 10. Elevation: compressed float32[TerrainRange]
        {
            auto elev_data = read_compressed(static_cast<size_t>(terrain_cells) * 4);
            auto s = make_stream(elev_data);
            w.elevations = read_f32_slice(s, static_cast<size_t>(terrain_cells));
        }

        compute_elevation_bounds(w.elevations, w.bounds);
        w.bounds.world_size_x = static_cast<double>(land_range_x) * static_cast<double>(cell_size);
        w.bounds.world_size_y = static_cast<double>(land_range_y) * static_cast<double>(cell_size);

        // 11. MatNames: count(int32) + (asciiz + byte)[]
        {
            int32_t n_materials = read_i32(r);
            w.textures.resize(static_cast<size_t>(n_materials));
            for (size_t i = 0; i < static_cast<size_t>(n_materials); i++) {
                w.textures[i].filename = read_asciiz(r);
                w.textures[i].color = read_u8(r);
            }
            w.stats.texture_count = static_cast<int>(n_materials);
        }

        // 12. Models: count(int32) + asciiz[]
        {
            int32_t n_models = read_i32(r);
            w.models.resize(static_cast<size_t>(n_models));
            for (size_t i = 0; i < static_cast<size_t>(n_models); i++) {
                w.models[i] = read_asciiz(r);
            }
            w.stats.model_count = static_cast<int>(n_models);
        }

        // 13. EntityInfos (v>=15)
        if (version >= 15) {
            int32_t n_entities = read_i32(r);
            for (int i = 0; i < n_entities; i++) {
                read_asciiz(r); // className
                read_asciiz(r); // shapeName
                read_bytes(r, 12); // Vector3P
                read_i32(r); // ObjectID
            }
        }

        // 14. ObjectOffsets QuadTree
        skip_quad_tree(r);

        // 15. SizeOfObjects (int32)
        int32_t size_of_objects = read_i32(r);

        // 16. MapObjectOffsets QuadTree
        skip_quad_tree(r);

        // 17. SizeOfMapInfo (int32)
        int32_t size_of_map_info = read_i32(r);

        // 18. Persistent: compressed (LandRange bytes)
        read_compressed(static_cast<size_t>(land_cells));

        // 19. SubDivHints: compressed (TerrainRange bytes)
        read_compressed(static_cast<size_t>(terrain_cells));

        // 20. MaxObjectId (int32)
        read_i32(r);

        // 21. RoadNetSize (int32)
        read_i32(r);

        // 22. RoadNet: per-cell arrays of RoadLink[]
        {
            w.road_links.resize(static_cast<size_t>(land_cells));
            int total_road_links = 0;
            for (size_t i = 0; i < static_cast<size_t>(land_cells); i++) {
                int32_t n_links = read_i32(r);
                if (n_links > 0) {
                    w.road_links[i].resize(static_cast<size_t>(n_links));
                    for (size_t j = 0; j < static_cast<size_t>(n_links); j++) {
                        w.road_links[i][j] = read_road_link();
                    }
                    total_road_links += n_links;
                }
            }
            w.stats.road_net_count = total_road_links;
        }

        // 23. Objects: 60-byte records (SizeOfObjects/60 entries)
        int n_objects = static_cast<int>(size_of_objects) / 60;
        if (!opts.no_objects) {
            w.objects.reserve(static_cast<size_t>(n_objects));
            for (int i = 0; i < n_objects; i++) {
                int32_t obj_id = read_i32(r);
                int32_t model_idx_raw = read_i32(r);
                auto transform = read_transform_matrix(r);
                read_i32(r); // ShapeParam

                int mi = static_cast<int>(model_idx_raw);
                std::string model_name;
                if (mi >= 0 && mi < static_cast<int>(w.models.size())) {
                    model_name = w.models[static_cast<size_t>(mi)];
                } else if (mi >= 0) {
                    w.warnings.push_back({
                        "INVALID_MODEL_INDEX",
                        std::format("object {} references model index {} (max {})",
                                    obj_id, model_idx_raw, w.models.size())
                    });
                }

                std::array<double, 3> pos{};
                Rotation rot{};
                double scale = 1.0;
                extract_position_rotation(transform, pos, rot, scale);

                w.objects.push_back({
                    static_cast<uint32_t>(obj_id), mi, model_name,
                    transform, pos, rot, scale
                });
            }
        } else {
            // Skip object data
            read_bytes(r, static_cast<size_t>(n_objects) * 60);
        }
        w.stats.object_count = static_cast<int>(w.objects.size());

        // 24. MapInfos: variable-length map display entries (infoType + MapData).
        // Not object records — skip.
        if (size_of_map_info > 0) {
            read_bytes(r, static_cast<size_t>(size_of_map_info));
        }

        return w;
    }
};

static WorldData read_oprw_modern(std::istream& r, int version, Options opts) {
    OprwModernParser parser{r, version, opts, {}};
    return parser.parse();
}

// ---------------------------------------------------------------------------
// 4WVR
// ---------------------------------------------------------------------------

static WorldData read_4wvr(std::istream& r, Options opts) {
    WorldData w;
    w.format = {"4WVR", 4};

    int cells_x = static_cast<int>(read_u32(r));
    int cells_y = static_cast<int>(read_u32(r));

    w.grid = {cells_x, cells_y, 50.0, cells_x, cells_y};

    int total_cells = cells_x * cells_y;

    // Elevations: ushort[Ysize][Xsize] -- centimetres (0.05m per unit)
    {
        auto elev_raw = read_u16_slice(r, static_cast<size_t>(total_cells));
        w.elevations.resize(static_cast<size_t>(total_cells));
        double min_e = std::numeric_limits<double>::max();
        double max_e = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < static_cast<size_t>(total_cells); i++) {
            double m = static_cast<double>(elev_raw[i]) * 0.05;
            w.elevations[i] = static_cast<float>(m);
            if (m < min_e) min_e = m;
            if (m > max_e) max_e = m;
        }
        w.bounds.min_elevation = min_e;
        w.bounds.max_elevation = max_e;
        w.bounds.world_size_x = static_cast<double>(cells_x) * w.grid.cell_size;
        w.bounds.world_size_y = static_cast<double>(cells_y) * w.grid.cell_size;
    }

    // TextureIndex: ushort[Ysize][Xsize]
    w.cell_texture_indexes = read_u16_slice(r, static_cast<size_t>(total_cells));

    // TextureFilenames: char[512][32]
    w.textures.reserve(512);
    for (int i = 0; i < 512; i++) {
        auto name = read_fixed_string(r, 32);
        w.textures.push_back({name, 0});
    }

    {
        int tex_count = 0;
        for (auto& t : w.textures) {
            if (!t.filename.empty()) tex_count++;
        }
        w.stats.texture_count = tex_count;
    }

    // Models: 128-byte records to EOF
    if (!opts.no_objects) {
        for (;;) {
            // Try to read a 128-byte record: transform(48) + objID(4) + name(76)
            std::array<float, 12> transform{};
            if (!r.read(reinterpret_cast<char*>(transform.data()), 48)) break;

            int32_t obj_id;
            if (!r.read(reinterpret_cast<char*>(&obj_id), 4)) break;

            char name_buf[76];
            if (!r.read(name_buf, 76)) break;

            // Find null terminator
            std::string model_name;
            auto null_pos = std::find(name_buf, name_buf + 76, '\0');
            model_name.assign(name_buf, null_pos);

            if (model_name.empty()) continue;

            std::array<double, 3> pos{};
            Rotation rot{};
            double scale = 1.0;
            extract_position_rotation(transform, pos, rot, scale);

            w.objects.push_back({
                static_cast<uint32_t>(obj_id), 0, model_name,
                transform, pos, rot, scale
            });
        }
    }

    w.stats.object_count = static_cast<int>(w.objects.size());
    w.stats.model_count = 0;

    build_model_index(w);

    w.warnings.push_back({"ROADS_UNSUPPORTED", "4WVR format does not contain road/net data"});

    return w;
}

// ---------------------------------------------------------------------------
// 1WVR
// ---------------------------------------------------------------------------

static void read_1wvr_nets(std::istream& r, WorldData& w) {
    for (;;) {
        char name_buf[24];
        if (!r.read(name_buf, 24)) return; // EOF is fine

        auto null_pos = std::find(name_buf, name_buf + 24, '\0');
        std::string net_name(name_buf, null_pos);

        if (net_name == "EndOfNets") {
            // Read remaining 40 bytes of the EndOfNets record
            char skip[40];
            r.read(skip, 40);
            break;
        }

        // 5 constant uint32s
        for (int i = 0; i < 5; i++) {
            read_u32(r);
        }

        uint32_t net_type = read_u32(r);
        auto triplet = read_f32_slice(r, 3);
        float net_scale = read_f32(r);

        RoadNet net;
        net.name = net_name;
        net.type = static_cast<int>(net_type);
        net.origin = {static_cast<double>(triplet[0]),
                      static_cast<double>(triplet[1]),
                      static_cast<double>(triplet[2])};
        net.scale = static_cast<double>(net_scale);

        for (;;) {
            float sx = read_f32(r);
            float sy = read_f32(r);

            if (sx == 0.0f && sy == 0.0f) break;

            auto sub_triplet = read_f32_slice(r, 3);
            float stepping = read_f32(r);
            read_u32(r); // constant1
            read_u32(r); // constant2

            SubNet sn;
            sn.x = static_cast<double>(sx) * 50.0;
            sn.y = static_cast<double>(sy) * 50.0;
            sn.triplet = {static_cast<double>(sub_triplet[0]),
                          static_cast<double>(sub_triplet[1]),
                          static_cast<double>(sub_triplet[2])};
            sn.stepping = static_cast<double>(stepping);
            net.subnets.push_back(sn);
        }

        w.roads.push_back(std::move(net));
    }

    w.stats.road_net_count = static_cast<int>(w.roads.size());
}

static bool str_contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(),
                           needle.begin(), needle.end(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           });
    return it != haystack.end();
}

static WorldData read_1wvr(std::istream& r, Options opts) {
    WorldData w;
    w.format = {"1WVR", 1};

    int cells_x = static_cast<int>(read_u32(r));
    int cells_y = static_cast<int>(read_u32(r));

    w.grid = {cells_x, cells_y, 50.0, cells_x, cells_y};

    size_t total_cells = static_cast<size_t>(cells_x) * static_cast<size_t>(cells_y);

    // Elevations: ushort[Ysize][Xsize]
    {
        auto elev_raw = read_u16_slice(r, total_cells);
        w.elevations.resize(total_cells);
        double min_e = std::numeric_limits<double>::max();
        double max_e = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < total_cells; i++) {
            double m = static_cast<double>(elev_raw[i]) * 0.05;
            w.elevations[i] = static_cast<float>(m);
            if (m < min_e) min_e = m;
            if (m > max_e) max_e = m;
        }
        w.bounds.min_elevation = min_e;
        w.bounds.max_elevation = max_e;
        w.bounds.world_size_x = static_cast<double>(cells_x) * w.grid.cell_size;
        w.bounds.world_size_y = static_cast<double>(cells_y) * w.grid.cell_size;
    }

    // TextureIndex: ushort[Ysize][Xsize]
    w.cell_texture_indexes = read_u16_slice(r, total_cells);

    // TextureFilenames: char[256][32]
    w.textures.reserve(256);
    for (int i = 0; i < 256; i++) {
        auto name = read_fixed_string(r, 32);
        w.textures.push_back({name, 0});
    }

    {
        int tex_count = 0;
        for (auto& t : w.textures) {
            if (!t.filename.empty()) tex_count++;
        }
        w.stats.texture_count = tex_count;
    }

    // Models: 64-byte records (position[3*f32] + heading[f32] + name[48])
    if (!opts.no_objects) {
        for (;;) {
            float pos_floats[3];
            if (!r.read(reinterpret_cast<char*>(pos_floats), 12)) break;

            float heading;
            if (!r.read(reinterpret_cast<char*>(&heading), 4)) break;

            char name_buf[48];
            if (!r.read(name_buf, 48)) break;

            auto null_pos = std::find(name_buf, name_buf + 48, '\0');
            std::string model_name(name_buf, null_pos);

            // Check if this is a valid model name (contains .p3d or .p3x)
            if (!str_contains_ci(model_name, ".p3d") &&
                !str_contains_ci(model_name, ".p3x")) {
                // Seek back 64 bytes -- this is the start of net data
                r.seekg(-64, std::ios::cur);
                break;
            }

            double pos_x = static_cast<double>(pos_floats[0]) * 50.0;
            double pos_z = static_cast<double>(pos_floats[2]) * 50.0;
            double pos_y = static_cast<double>(pos_floats[1]) * 50.0;
            double yaw_deg = -static_cast<double>(heading);

            ObjectRecord obj;
            obj.model_name = model_name;
            obj.position = {pos_x, pos_y, pos_z};
            obj.rotation = {yaw_deg, 0.0, 0.0};
            obj.scale = 1.0;
            w.objects.push_back(std::move(obj));
        }
    }

    w.stats.object_count = static_cast<int>(w.objects.size());

    build_model_index(w);

    // Parse nets/roads
    try {
        read_1wvr_nets(r, w);
    } catch (const std::exception& e) {
        w.warnings.push_back({
            "NET_PARSE_ERROR",
            std::format("error parsing nets/roads: {}", e.what())
        });
    }

    return w;
}

} // namespace armatools::wrp
