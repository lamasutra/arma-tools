#include "project.h"
#include "armatools/roadnet.h"
#include "armatools/shp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Forward declarations from tv4p.cpp
void write_alb1_file(const std::string& path, const std::vector<uint8_t>& payload);

// Module-level pointer to the active ProjectInfo, set by write_tv4s().
static ProjectInfo* g_proj = nullptr;

// Minimal Tv4pBuf duplicate for tv4s (same binary format)
// We duplicate just enough to avoid header dependency on tv4p internals.
class Tv4sBuf {
public:
    std::vector<uint8_t> data;
    void write_u8(uint8_t v) { data.push_back(v); }
    void write_u32(uint32_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(), p, p + 4); }
    void write_f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); write_u32(u); }
    void write_f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); auto p = reinterpret_cast<const uint8_t*>(&u); data.insert(data.end(), p, p + 8); }

    // Tag/class ID tables (same as tv4p.cpp)
    static uint8_t tag_id(const std::string& name);
    static uint8_t class_id(const std::string& name);
    static uint32_t alloc_ptr() { return g_proj->alloc_ptr(); }

    void tag(const std::string& name, uint8_t tc) { write_u8(tag_id(name)); write_u8(0x00); write_u8(tc); }
    void u32_val(const std::string& name, uint32_t v) { tag(name, 0x05); write_u32(v); }
    void u32_alt(const std::string& name, uint32_t v) { tag(name, 0x06); write_u32(v); }
    void u32_cnt(const std::string& name, uint32_t v) { tag(name, 0x07); write_u32(v); }
    void f32_val(const std::string& name, float v) { tag(name, 0x0A); write_f32(v); }
    void boolean(const std::string& name, bool v) { tag(name, 0x09); write_u8(v ? 1 : 0); }
    void color(const std::string& name, uint32_t v) { tag(name, 0x08); write_u32(v); }
    void null_ref(const std::string& name) { tag(name, 0x13); }
    void array_mark(const std::string& name) { tag(name, 0x0F); }
    void str(const std::string& name, const std::string& s) {
        tag(name, 0x0B);
        if (s.size() > 0xFFFF) {
            throw std::runtime_error(std::format(
                "tv4s: string too long for u16 length prefix ({} bytes, tag '{}')", s.size(), name));
        }
        uint16_t n = static_cast<uint16_t>(s.size());
        auto p = reinterpret_cast<const uint8_t*>(&n);
        data.insert(data.end(), p, p + 2);
        for (char c : s) write_u8(static_cast<uint8_t>(c));
    }
    void blob(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, 0x0C); write_u32(static_cast<uint32_t>(content.size()));
        data.insert(data.end(), content.begin(), content.end());
    }
    void nested(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, 0x0D); write_u32(static_cast<uint32_t>(content.size()));
        data.insert(data.end(), content.begin(), content.end());
    }
    void point3d(const std::string& name, double x, double y, double z) {
        tag(name, 0x15); write_u8(3); write_f64(x); write_f64(y); write_f64(z);
    }
    void class_preamble(const std::string& cls) { write_u8(class_id(cls)); write_u8(0x00); write_u32(alloc_ptr()); }
    void class_preamble_ptr(const std::string& cls, uint32_t ptr) { write_u8(class_id(cls)); write_u8(0x00); write_u32(ptr); }
};

// Simplified tag/class tables (duplicated from tv4p.cpp to avoid coupling)
static const std::unordered_map<std::string, uint8_t>& s_tag_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"tags",0x02},{"classes",0x03},{"data",0x05},{"item",0x06},
        {"pair",0x07},{"key",0x08},{"mname",0x0A},{"mvalue",0x0B},{"mtype",0x0C},
        {"mrawDataTable",0x0D},{"mrawData",0x0E},{"malpha",0x1B},
        {"mnPriority",0x1D},{"mbVisible",0x1E},{"mbLocked",0x1F},{"mbOpaque",0x20},
        {"objectCount",0x21},{"mnCoreVerticesCount",0x25},{"mfRadius",0x26},
        {"mpVertices",0x27},{"mdata",0x28},{"mfAzimuth",0x29},
        {"mdeleted",0x2A},{"moutlineColor",0x2B},{"mfillColor",0x2C},
        {"mpattern",0x2D},{"moutlineWidth",0x2E},
        {"mareas",0x30},{"msurfExportMapframeName",0x31},{"msurfExportSurfName",0x32},
        {"center",0x98},{"mcolor",0x99},
    };
    return m;
}

static const std::unordered_map<std::string, uint8_t>& s_class_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"SRawData",0x01},{"CRawDataContainer",0x02},{"CAreaLayer",0x09},
        {"CPolylineArea",0x08},{"CVertex",0x1D},
    };
    return m;
}

uint8_t Tv4sBuf::tag_id(const std::string& name) { return s_tag_ids().at(name); }
uint8_t Tv4sBuf::class_id(const std::string& name) { return s_class_ids().at(name); }

static std::vector<uint8_t> build_vertex(double x, double y, double z) {
    Tv4sBuf v;
    v.class_preamble("CVertex");
    v.point3d("center", x, y, z);
    v.color("mcolor", 0x60FFFFFF);
    return v.data;
}

static std::vector<uint8_t> build_road_attributes(const armatools::roadnet::Polyline& pl, uint32_t object_id) {
    std::string fields = "ID;LENGTH;MAP;ORDER;ROADTYPE;SEGMENTS;TERRAIN;WIDTH;__ID;";

    struct Attr { std::string key, name, value; uint32_t mtype; };
    std::vector<Attr> attrs = {
        {"ID", "ID", std::format("{}", pl.props.id), 1},
        {"LENGTH", "LENGTH", std::format("{:.1f}", pl.length), 2},
        {"MAP", "MAP", pl.props.map_type, 0},
        {"ORDER", "ORDER", std::format("{}", pl.props.order), 1},
        {"ROADTYPE", "ROADTYPE", std::string(pl.type), 0},
        {"SEGMENTS", "SEGMENTS", std::format("{}", pl.seg_count), 1},
        {"TERRAIN", "TERRAIN", std::format("{:.0f}", pl.props.terrain), 1},
        {"WIDTH", "WIDTH", std::format("{:.0f}", pl.props.width), 1},
        {"__ID", "__ID", std::format("{}", object_id), 1},
    };

    Tv4sBuf raw_data;
    raw_data.write_u32(static_cast<uint32_t>(attrs.size()));
    for (const auto& a : attrs) {
        raw_data.array_mark("pair");
        raw_data.str("key", a.key);
        Tv4sBuf entry;
        entry.class_preamble("SRawData");
        entry.str("mname", a.name);
        entry.str("mvalue", a.value);
        entry.u32_val("mtype", a.mtype);
        raw_data.nested("data", entry.data);
    }

    Tv4sBuf container;
    container.class_preamble("CRawDataContainer");
    container.str("mrawDataTable", fields);
    container.blob("mrawData", raw_data.data);
    return container.data;
}

static std::vector<uint8_t> build_polyline_area(const armatools::roadnet::Polyline& pl,
                                                  double offset_x, double offset_z, uint32_t object_id) {
    Tv4sBuf area;
    area.class_preamble("CPolylineArea");
    area.u32_alt("mnCoreVerticesCount", static_cast<uint32_t>(pl.points.size()));
    area.f32_val("mfRadius", static_cast<float>(pl.props.width / 2));

    Tv4sBuf vblob;
    vblob.write_u32(static_cast<uint32_t>(pl.points.size()));
    for (const auto& pt : pl.points) {
        auto vertex = build_vertex(pt[0] + offset_x, pt[1] + offset_z, 0);
        vblob.nested("item", vertex);
    }
    area.blob("mpVertices", vblob.data);
    area.nested("mdata", build_road_attributes(pl, object_id));
    area.f32_val("mfAzimuth", 0);
    area.boolean("mdeleted", false);
    area.color("moutlineColor", 0xFFFFFF00);
    area.color("mfillColor", 0xAAFF00FF);
    area.color("mpattern", 0xFFFFFFFF);
    area.f32_val("moutlineWidth", 1.0f);
    area.str("mname", "roads_polyline");
    return area.data;
}

static std::string road_type_from_id(int id) {
    switch (id) {
    case 1: return "highway"; case 2: return "asphalt";
    case 3: return "concrete"; case 4: return "dirt"; default: return "road";
    }
}
static std::string map_type_from_id(int id) {
    switch (id) {
    case 1: return "main road"; case 2: return "road";
    case 3: case 4: return "track"; default: return "road";
    }
}
static double width_from_id(int id) {
    switch (id) {
    case 1: return 14; case 2: return 10; case 3: return 7;
    case 4: return 8; case 5: return 1.6; default: return 6;
    }
}

static std::vector<armatools::roadnet::Polyline> polylines_from_shp(const std::string& shp_path,
                                                                      double offset_x, double offset_z) {
    auto src = armatools::shp::open(shp_path);
    std::vector<armatools::roadnet::Polyline> polylines;
    for (const auto& rec : src.records) {
        for (const auto& part : rec.parts) {
            if (part.size() < 2) continue;
            std::vector<std::array<double, 2>> points;
            double length = 0;
            for (size_t i = 0; i < part.size(); i++) {
                points.push_back({part[i].x - offset_x, part[i].y - offset_z});
                if (i > 0) {
                    double dx = points[i][0] - points[i - 1][0];
                    double dy = points[i][1] - points[i - 1][1];
                    length += std::sqrt(dx * dx + dy * dy);
                }
            }

            int id = armatools::shp::attr_int(rec.attrs, "ID");
            double width = armatools::shp::attr_float64(rec.attrs, "WIDTH");
            int order = armatools::shp::attr_int(rec.attrs, "ORDER");
            int segments = armatools::shp::attr_int(rec.attrs, "SEGMENTS");
            std::string road_type, map_type;
            auto rt = rec.attrs.find("ROADTYPE"); if (rt != rec.attrs.end()) road_type = rt->second;
            auto mt = rec.attrs.find("MAP"); if (mt != rec.attrs.end()) map_type = mt->second;
            if (road_type.empty()) road_type = road_type_from_id(id);
            if (map_type.empty()) map_type = map_type_from_id(id);
            if (width == 0) width = width_from_id(id);
            double terrain = armatools::shp::attr_float64(rec.attrs, "TERRAIN");
            if (terrain == 0) terrain = width + 2;

            polylines.push_back({points, road_type,
                                  {id, order, width, terrain, map_type},
                                  length, segments, {}, {}, {}});
        }
    }
    return polylines;
}

static void write_empty_area_tv4s(const std::string& path, uint32_t area_ptr) {
    Tv4sBuf root;
    if (area_ptr != 0)
        root.class_preamble_ptr("CAreaLayer", area_ptr);
    else
        root.class_preamble("CAreaLayer");
    root.str("mname", "default area");
    root.u32_alt("mnPriority", 0);
    root.boolean("mbVisible", true);
    root.boolean("mbLocked", false);
    root.boolean("mbOpaque", true);
    root.blob("mareas", {0, 0, 0, 0});
    root.f32_val("malpha", 1.0f);
    root.str("msurfExportMapframeName", "");
    root.str("msurfExportSurfName", "");
    write_alb1_file(path, root.data);
}

static void write_road_tv4s(ProjectInfo& p, const std::string& shapes_dir) {
    std::vector<armatools::roadnet::Polyline> polylines;

    if (!p.roads_shp.empty()) {
        polylines = polylines_from_shp(p.roads_shp, p.offset_x, p.offset_z);
    } else {
        if (!p.world->road_links.empty())
            polylines = armatools::roadnet::extract_from_road_links(p.world->road_links);
        if (polylines.empty() && !p.world->objects.empty())
            polylines = armatools::roadnet::extract_from_objects(p.world->objects);
    }

    if (polylines.empty()) {
        write_empty_area_tv4s((fs::path(shapes_dir) / "roads.tv4s").string(), p.active_area_ptr);
        return;
    }

    Tv4sBuf areas_blob;
    areas_blob.write_u32(static_cast<uint32_t>(polylines.size()));
    uint32_t id_counter = 1000;
    for (const auto& pl : polylines) {
        if (pl.points.size() < 2) continue;
        id_counter++;
        auto entry = build_polyline_area(pl, p.offset_x, p.offset_z, id_counter);
        areas_blob.nested("item", entry);
    }

    Tv4sBuf root;
    root.class_preamble_ptr("CAreaLayer", p.active_area_ptr);
    root.str("mname", "roads");
    root.u32_alt("mnPriority", 1);
    root.boolean("mbVisible", true);
    root.boolean("mbLocked", false);
    root.boolean("mbOpaque", true);
    root.blob("mareas", areas_blob.data);
    root.f32_val("malpha", 1.0f);
    root.str("msurfExportMapframeName", "");
    root.str("msurfExportSurfName", "");
    write_alb1_file((fs::path(shapes_dir) / "roads.tv4s").string(), root.data);
}

void write_tv4s(ProjectInfo& p) {
    g_proj = &p;
    std::string lower_name = p.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;
    std::string shapes_dir = (fs::path(p.output_dir) / (map_name + ".Shapes")).string();
    fs::create_directories(shapes_dir);

    if (p.active_area_ptr == 0) p.active_area_ptr = p.alloc_ptr();
    write_road_tv4s(p, shapes_dir);
    write_empty_area_tv4s((fs::path(shapes_dir) / "default area.tv4s").string(), 0);
}
