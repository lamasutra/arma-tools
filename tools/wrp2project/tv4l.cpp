#include "project.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// Forward from tv4p.cpp
void write_alb1_file(const std::string& path, const std::vector<uint8_t>& payload);

// ============================================================================
// ALB1 binary format helpers
// ============================================================================

// Module-level pointer to the active ProjectInfo, set by write_tv4l().
static ProjectInfo* g_proj = nullptr;

class Tv4lBuf {
public:
    std::vector<uint8_t> data;
    void write_u8(uint8_t v) { data.push_back(v); }
    void write_u16(uint16_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(), p, p + 2); }
    void write_u32(uint32_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(), p, p + 4); }
    void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }
    void write_f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); write_u32(u); }
    void write_f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); auto p = reinterpret_cast<const uint8_t*>(&u); data.insert(data.end(), p, p + 8); }

    static uint8_t tag_id(const std::string& name);
    static uint8_t class_id(const std::string& name);
    static uint32_t alloc_ptr() { return g_proj->alloc_ptr(); }

    void tag(const std::string& name, uint8_t tc) { write_u8(tag_id(name)); write_u8(0x00); write_u8(tc); }
    void u32_val(const std::string& name, uint32_t v) { tag(name, 0x05); write_u32(v); }
    void u32_alt(const std::string& name, uint32_t v) { tag(name, 0x06); write_u32(v); }
    void u32_cnt(const std::string& name, uint32_t v) { tag(name, 0x07); write_u32(v); }
    void f32_val(const std::string& name, float v) { tag(name, 0x0A); write_f32(v); }
    void chr(const std::string& name, uint8_t v) { tag(name, 0x01); write_u8(v); }
    void boolean(const std::string& name, bool v) { tag(name, 0x09); write_u8(v ? 1 : 0); }
    // ALB1 strings use u16 length prefix (little-endian)
    void str(const std::string& name, const std::string& s) {
        tag(name, 0x0B);
        if (s.size() > 0xFFFF) {
            throw std::runtime_error(std::format(
                "tv4l: string too long for u16 length prefix ({} bytes, tag '{}')", s.size(), name));
        }
        write_u16(static_cast<uint16_t>(s.size()));
        for (size_t i = 0; i < s.size(); i++) write_u8(static_cast<uint8_t>(s[i]));
    }
    void blob(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, 0x0C); write_u32(static_cast<uint32_t>(content.size()));
        data.insert(data.end(), content.begin(), content.end());
    }
    void nested(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, 0x0D); write_u32(static_cast<uint32_t>(content.size()));
        data.insert(data.end(), content.begin(), content.end());
    }
    void array_mark(const std::string& name) { tag(name, 0x0F); }
    void class_preamble(const std::string& cls) { write_u8(class_id(cls)); write_u8(0x00); write_u32(alloc_ptr()); }
    void class_preamble_ptr(const std::string& cls, uint32_t ptr) { write_u8(class_id(cls)); write_u8(0x00); write_u32(ptr); }
};

static const std::unordered_map<std::string, uint8_t>& l_tag_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"tags",0x02},{"classes",0x03},{"data",0x05},{"item",0x06},
        {"pair",0x07},{"key",0x08},{"mname",0x0A},{"malpha",0x1B},
        {"mlayerVersion",0x1C},{"mnPriority",0x1D},{"mbVisible",0x1E},
        {"mbLocked",0x1F},{"mbOpaque",0x20},{"objectCount",0x21},{"tree",0x22},
        {"mobjectIDcounter",0x23},{"mlayerID",0x24},
        {"libs",0x17},{"mUTMzone",0x15},{"mUTMzoneNumber",0x16},
    };
    return m;
}

static const std::unordered_map<std::string, uint8_t>& l_class_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"CLayer",0x05},
    };
    return m;
}

uint8_t Tv4lBuf::tag_id(const std::string& name) { return l_tag_ids().at(name); }
uint8_t Tv4lBuf::class_id(const std::string& name) { return l_class_ids().at(name); }

// ============================================================================
// DFS quadtree serialization compatible with TerrainBuilder
// ============================================================================

constexpr int TB_QTREE_MAX_DEPTH = 14;
constexpr int TB_QTREE_FULL_INNER_DEPTH = 8;
constexpr int TB_QTREE_LEAF_TARGET = 16;

struct TreeBBox {
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;

    TreeBBox child(int c) const {
        double mid_x = (min_x + max_x) / 2;
        double mid_y = (min_y + max_y) / 2;
        switch (c) {
        // TB-native child order (verified against cup-cain-e):
        // 0=SE, 1=NE, 2=SW, 3=NW
        case 0: return {mid_x, min_y, max_x, mid_y}; // SE
        case 1: return {mid_x, mid_y, max_x, max_y}; // NE
        case 2: return {min_x, min_y, mid_x, mid_y}; // SW
        case 3: return {min_x, mid_y, mid_x, max_y}; // NW
        default: return *this;
        }
    }
};

struct LeafObj {
    double x, y;
    float z;
    float yaw, pitch, roll, scale;
    uint32_t id;
};
struct LeafObjGroup { uint32_t template_hash; std::vector<LeafObj> objects; };
struct ObjEntry { LeafObj obj; uint32_t hash; };

static void buf_write_f64(std::vector<uint8_t>& buf, double v) {
    uint64_t u; std::memcpy(&u, &v, 8);
    auto p = reinterpret_cast<const uint8_t*>(&u); buf.insert(buf.end(), p, p + 8);
}
static void buf_write_f32(std::vector<uint8_t>& buf, float v) {
    uint32_t u; std::memcpy(&u, &v, 4);
    auto p = reinterpret_cast<const uint8_t*>(&u); buf.insert(buf.end(), p, p + 4);
}
static void buf_write_i32(std::vector<uint8_t>& buf, int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    auto p = reinterpret_cast<const uint8_t*>(&u); buf.insert(buf.end(), p, p + 4);
}
static void buf_write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    auto p = reinterpret_cast<const uint8_t*>(&v); buf.insert(buf.end(), p, p + 4);
}

static void buf_write_bbox(std::vector<uint8_t>& buf, const TreeBBox& bb) {
    buf_write_f64(buf, bb.min_y);
    buf_write_f64(buf, bb.min_x);
    buf_write_f64(buf, bb.max_y);
    buf_write_f64(buf, bb.max_x);
}

static int child_index_for(double x, double y, const TreeBBox& bb) {
    double mid_x = (bb.min_x + bb.max_x) / 2;
    double mid_y = (bb.min_y + bb.max_y) / 2;
    if (x >= mid_x)
        return (y < mid_y) ? 0 : 1; // right: SE/NE
    return (y < mid_y) ? 2 : 3;     // left: SW/NW
}

static float normalize_yaw_deg(double yaw) {
    double y = std::fmod(yaw, 360.0);
    if (y < 0.0) y += 360.0;
    return static_cast<float>(y);
}

static float normalize_angle_deg(double angle) {
    double a = std::fmod(angle, 360.0);
    if (a < 0.0) a += 360.0;
    return static_cast<float>(a);
}

static void write_leaf(std::vector<uint8_t>& buf, const TreeBBox& bbox, int depth,
                       const std::vector<ObjEntry>& entries) {
    std::unordered_map<uint32_t, std::vector<LeafObj>> by_hash;
    for (const auto& e : entries) by_hash[e.hash].push_back(e.obj);

    std::vector<uint32_t> hashes;
    hashes.reserve(by_hash.size());
    for (const auto& [h, _] : by_hash) hashes.push_back(h);
    std::sort(hashes.begin(), hashes.end());

    buf_write_bbox(buf, bbox);
    buf_write_i32(buf, depth);
    buf_write_i32(buf, hashes.empty() ? 0 : static_cast<int32_t>(hashes.front()));
    buf_write_i32(buf, static_cast<int32_t>(hashes.size()));

    for (uint32_t h : hashes) {
        const auto& objs = by_hash[h];
        buf_write_i32(buf, static_cast<int32_t>(objs.size()));
        buf_write_u32(buf, h);
        for (const auto& obj : objs) {
            // Object payload used by TB layer serializer in this stream (40 bytes).
            buf_write_f64(buf, obj.x);
            buf_write_f64(buf, obj.y);
            buf_write_f32(buf, obj.yaw);
            // TB stores pitch/roll in these legacy serializer slots.
            buf_write_f32(buf, obj.pitch);
            buf_write_f32(buf, obj.roll);
            buf_write_f32(buf, obj.scale);
            buf_write_f32(buf, obj.z);
            buf_write_u32(buf, obj.id);
        }
    }
}

static void write_inner(std::vector<uint8_t>& buf, const TreeBBox& bbox, int depth,
                        const std::vector<ObjEntry>& entries, int max_depth,
                        int leaf_target) {
    std::array<std::vector<ObjEntry>, 4> child_entries;
    for (const auto& e : entries) {
        int ci = child_index_for(e.obj.x, e.obj.y, bbox);
        if (ci < 0 || ci >= static_cast<int>(child_entries.size())) continue;
        child_entries[static_cast<size_t>(ci)].push_back(e);
    }

    const bool force_full_inner = (depth < TB_QTREE_FULL_INNER_DEPTH);
    const bool force_depth8_inner = (depth == TB_QTREE_FULL_INNER_DEPTH);

    uint8_t children_type = 0x01;
    uint8_t child_mask = 0;

    if (force_full_inner) {
        // Match TB dense topology in upper levels.
        children_type = 0x01;
        child_mask = 0x0F;
    } else if (force_depth8_inner && entries.empty()) {
        // TB uses explicit empty inner nodes at this level.
        children_type = 0xFF;
        child_mask = 0x00;
    } else {
        for (size_t c = 0; c < child_entries.size(); c++) {
            if (!child_entries[c].empty()) {
                child_mask |= static_cast<uint8_t>(1u << static_cast<unsigned>(c));
            }
        }
        bool children_are_leaves =
            (depth + 1 >= max_depth) || (entries.size() <= static_cast<size_t>(leaf_target));
        children_type = children_are_leaves ? 0x10 : 0x01;
    }

    buf.push_back(children_type);
    buf_write_bbox(buf, bbox);
    buf_write_i32(buf, depth);
    // TB-native TV4L stores 0 in this slot for inner nodes.
    buf_write_i32(buf, 0);
    buf.push_back(child_mask);

    for (size_t c = 0; c < child_entries.size(); c++) {
        uint8_t bit = static_cast<uint8_t>(1u << static_cast<unsigned>(c));
        if ((child_mask & bit) == 0) continue;

        TreeBBox cb = bbox.child(static_cast<int>(c));
        if (children_type == 0x10)
            write_leaf(buf, cb, depth + 1, child_entries[c]);
        else
            write_inner(buf, cb, depth + 1, child_entries[c], max_depth, leaf_target);
    }
}

static bool utm_easting_from_lon_lat(double lon_deg, double lat_deg, int zone, double& easting_out) {
    if (zone < 1 || zone > 60) return false;
    if (!std::isfinite(lon_deg) || !std::isfinite(lat_deg)) return false;

    constexpr double a = 6378137.0;                 // WGS84 major axis
    constexpr double f = 1.0 / 298.257223563;       // WGS84 flattening
    constexpr double k0 = 0.9996;
    constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;

    const double e2 = f * (2.0 - f);
    const double ep2 = e2 / (1.0 - e2);

    const double lat = lat_deg * deg_to_rad;
    const double lon = lon_deg * deg_to_rad;
    const double lon0 = ((static_cast<double>(zone) - 1.0) * 6.0 - 180.0 + 3.0) * deg_to_rad;

    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double tan_lat = std::tan(lat);

    const double N = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);
    const double T = tan_lat * tan_lat;
    const double C = ep2 * cos_lat * cos_lat;
    const double A = (lon - lon0) * cos_lat;

    const double M =
        a * ((1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 * e2 * e2 / 256.0) * lat
           - (3.0 * e2 / 8.0 + 3.0 * e2 * e2 / 32.0 + 45.0 * e2 * e2 * e2 / 1024.0) * std::sin(2.0 * lat)
           + (15.0 * e2 * e2 / 256.0 + 45.0 * e2 * e2 * e2 / 1024.0) * std::sin(4.0 * lat)
           - (35.0 * e2 * e2 * e2 / 3072.0) * std::sin(6.0 * lat));

    (void)M; // northing not required for tree bbox center

    easting_out =
        k0 * N * (A + (1.0 - T + C) * A * A * A / 6.0
               + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * ep2) * A * A * A * A * A / 120.0)
        + 500000.0;
    return std::isfinite(easting_out);
}

static TreeBBox compute_layer_tree_bbox(const ProjectInfo& p, const std::vector<LayerObject>& objects) {
    constexpr double HALF_X = 450000.0;
    constexpr double MIN_Y = 0.0;
    constexpr double MAX_Y = 1340000.0;

    // TB keeps default layer around origin even when object layers use projected easting center.
    if (objects.empty()) {
        return {-HALF_X, MIN_Y, HALF_X, MAX_Y};
    }

    // Match TB constructor behavior from reference C code:
    // x range = center_easting +/- 450000, y range = [0, 1340000].
    //
    // The center must come from project georeference, not from object extents
    // or map frame offsets. If metadata is unavailable, use deterministic
    // defaults that match wrp2project-generated config defaults.
    double cx = 0.0;
    int zone = 33;
    double lon = 14.0;
    double lat = -48.0;
    if (p.meta) {
        if (p.meta->map_zone > 0) zone = p.meta->map_zone;
        lon = p.meta->longitude;
        lat = p.meta->latitude;
    }

    bool have_center = utm_easting_from_lon_lat(lon, lat, zone, cx);
    if (!have_center) cx = p.offset_x + (p.world ? p.world->bounds.world_size_x * 0.5 : 0.0);

    return {cx - HALF_X, MIN_Y, cx + HALF_X, MAX_Y};
}

static std::vector<std::string> unique_model_names(const std::vector<LayerObject>& objects) {
    std::unordered_map<std::string, bool> seen;
    for (const auto& obj : objects) seen[obj.model_name] = true;
    std::vector<std::string> names;
    names.reserve(seen.size());
    for (const auto& [name, _] : seen) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

struct Tv4lLibEntry { std::string name; uint32_t id; };

static std::vector<uint8_t> build_layer_libs_blob1(const std::vector<Tv4lLibEntry>& libs) {
    if (libs.empty()) return {0, 0, 0, 0};
    Tv4lBuf buf;
    buf.write_u32(static_cast<uint32_t>(libs.size()));
    for (const auto& lib : libs) {
        buf.array_mark("pair");
        buf.str("key", lib.name);
        buf.u32_cnt("data", lib.id);
    }
    return buf.data;
}

static std::vector<uint8_t> build_layer_libs_blob2(const std::vector<std::string>& models,
                                                     const std::unordered_map<std::string, uint32_t>& model_lib_id,
                                                     const std::vector<Tv4lLibEntry>& libs) {
    Tv4lBuf buf;
    buf.write_u32(static_cast<uint32_t>(models.size()));
    uint32_t fallback = libs.empty() ? 0 : libs[0].id;
    for (const auto& name : models) {
        buf.array_mark("pair");
        buf.str("key", name);
        auto it = model_lib_id.find(name);
        buf.u32_cnt("data", (it != model_lib_id.end()) ? it->second : fallback);
    }
    return buf.data;
}

static std::vector<uint8_t> build_layer_tree(const std::vector<LayerObject>& objects,
                                               const std::vector<std::string>& models,
                                               const TreeBBox& root,
                                               uint32_t& serialized_count_out) {
    std::unordered_map<std::string, uint32_t> model_hash;
    std::unordered_map<std::string, std::string> model_name_ci;
    for (const auto& name : models) {
        model_hash[name] = armatools::tb::sdbm_hash(name);
        std::string low = name;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!model_name_ci.count(low)) model_name_ci[low] = name;
    }

    std::vector<ObjEntry> entries;
    entries.reserve(objects.size());
    // TB object IDs in TV4L start from 10000 (mobjectIDcounter keeps additional headroom).
    uint32_t next_id = 10000;

    for (const auto& obj : objects) {
        uint32_t h = 0;
        auto it = model_hash.find(obj.model_name);
        if (it != model_hash.end()) {
            h = it->second;
        } else {
            std::string low = obj.model_name;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto cit = model_name_ci.find(low);
            if (cit != model_name_ci.end()) {
                auto hit = model_hash.find(cit->second);
                if (hit != model_hash.end()) h = hit->second;
            }
        }

        entries.push_back({{
            obj.x,
            obj.y,
            static_cast<float>(obj.z),
            normalize_yaw_deg(obj.yaw),
            normalize_angle_deg(obj.pitch),
            normalize_angle_deg(obj.roll),
            static_cast<float>(obj.scale),
            next_id++
        }, h});
    }

    serialized_count_out = static_cast<uint32_t>(entries.size());

    std::vector<uint8_t> buf;
    buf.reserve(4 + entries.size() * 64);
    buf.resize(4, 0);

    // Root must be an inner node; for empty layers write a valid empty root.
    write_inner(buf, root, 0, entries, TB_QTREE_MAX_DEPTH, TB_QTREE_LEAF_TARGET);

    uint32_t payload_size = static_cast<uint32_t>(buf.size() - 4);
    std::memcpy(buf.data(), &payload_size, 4);
    return buf;
}

static std::string cat_file_name_l(const std::string& cat) {
    std::string s = cat;
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

// Derive UTM zone letter from latitude (simplified MGRS band lookup).
static char utm_zone_letter(double latitude) {
    // MGRS bands: C covers [-80,-72), D [-72,-64), ..., X [72,84)
    int band = static_cast<int>(std::floor((latitude + 80.0) / 8.0));
    const char* letters = "CDEFGHJKLMNPQRSTUVWX";
    if (band < 0) band = 0;
    if (band > 19) band = 19;
    return letters[band];
}

static void write_layer_tv4l(const std::string& layers_dir, const std::string& file_name,
                               const std::string& layer_name, const std::vector<LayerObject>& objects,
                               const std::vector<Tv4lLibEntry>& libs,
                               const std::unordered_map<std::string, uint32_t>& model_lib_id,
                               const std::vector<std::string>& models,
                               uint32_t layer_id, uint32_t layer_ptr, const TreeBBox& root_bbox,
                               char utm_letter, uint32_t utm_number) {
    uint32_t serialized_count = 0;
    auto tree_blob = build_layer_tree(objects, models, root_bbox, serialized_count);

    Tv4lBuf root;
    if (layer_ptr != 0) root.class_preamble_ptr("CLayer", layer_ptr);
    else root.class_preamble("CLayer");
    root.str("mname", layer_name);
    root.u32_val("mlayerVersion", 4);
    root.u32_alt("mnPriority", 0);
    root.boolean("mbVisible", true);
    root.boolean("mbLocked", false);
    root.boolean("mbOpaque", true);
    root.u32_cnt("objectCount", serialized_count);
    root.chr("mUTMzone", static_cast<uint8_t>(utm_letter));
    root.u32_val("mUTMzoneNumber", utm_number);
    root.blob("libs", build_layer_libs_blob1(libs));
    root.blob("libs", build_layer_libs_blob2(models, model_lib_id, libs));
    root.blob("tree", tree_blob);
    root.f32_val("malpha", 1.0f);
    // TB keeps a headroom gap in object IDs (commonly +10000 over current count).
    root.u32_val("mobjectIDcounter", serialized_count + 10000);
    root.u32_val("mlayerID", layer_id);

    write_alb1_file((fs::path(layers_dir) / (file_name + ".tv4l")).string(), root.data);
}

void write_tv4l(ProjectInfo& p) {
    g_proj = &p;
    std::string lower_name = p.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;
    std::string layers_dir = (fs::path(p.output_dir) / (map_name + ".Layers")).string();
    fs::create_directories(layers_dir);

    // Clean old tv4l files
    for (const auto& entry : fs::directory_iterator(layers_dir)) {
        if (entry.path().extension() == ".tv4l") fs::remove(entry.path());
    }

    auto default_bbox = compute_layer_tree_bbox(p, {});

    // Derive UTM zone from project metadata, default to N/33
    char utm_letter = 'N';
    uint32_t utm_number = 33;
    if (p.meta) {
        if (p.meta->map_zone > 0) utm_number = static_cast<uint32_t>(p.meta->map_zone);
        if (p.meta->latitude != 0) utm_letter = utm_zone_letter(p.meta->latitude);
    }

    // Keep active_layer_ptr for the selected non-default object layer (when present).
    // Default layer gets its own pointer so mactiveLayer does not bind to empty default.
    uint32_t default_layer_ptr = p.alloc_ptr();
    if (p.active_layer_ptr == 0) p.active_layer_ptr = default_layer_ptr;

    if (p.categories.empty()) {
        write_layer_tv4l(layers_dir, "default", "default", {}, {}, {}, {}, 1, default_layer_ptr, default_bbox, utm_letter, utm_number);
        return;
    }

    // Write an empty default layer first
    write_layer_tv4l(layers_dir, "default", "default", {}, {}, {}, {}, 1, default_layer_ptr, default_bbox, utm_letter, utm_number);

    // Write one layer per category with its objects
    uint32_t layer_id = 2;
    bool active_layer_bound = false;
    for (const auto& cat : p.categories) {
        const auto& objs = p.cat_objects[cat];
        std::string lib_name = p.cat_lib_names[cat];

        // Build lib entry for this category
        std::vector<Tv4lLibEntry> libs;
        std::unordered_map<std::string, uint32_t> model_lib_id;
        if (!lib_name.empty()) {
            uint32_t id = armatools::tb::sdbm_hash(lib_name);
            libs.push_back({lib_name, id});
            for (const auto& obj : objs)
                model_lib_id[obj.model_name] = id;
        }

        auto models = unique_model_names(objs);
        auto root_bbox = compute_layer_tree_bbox(p, objs);
        std::string safe = cat_file_name_l(cat);
        // When empty_layers is set, create the layer structure (libs, models)
        // but without objects — user will import from txt files.
        const auto& layer_objs = p.empty_layers ? std::vector<LayerObject>{} : objs;
        uint32_t layer_ptr = 0;
        if (!active_layer_bound) {
            layer_ptr = p.active_layer_ptr;
            active_layer_bound = true;
        }
        write_layer_tv4l(layers_dir, safe, safe, layer_objs, libs, model_lib_id,
                          models, layer_id, layer_ptr, root_bbox, utm_letter, utm_number);
        layer_id++;
    }
}
