#include "project.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// ALB1 binary format helpers
// ============================================================================

// ALB1 type codes
static constexpr uint8_t tc_char     = 0x01;
static constexpr uint8_t tc_u32      = 0x05;
static constexpr uint8_t tc_u32_alt  = 0x06;
static constexpr uint8_t tc_u32_cnt  = 0x07;
static constexpr uint8_t tc_color    = 0x08;
static constexpr uint8_t tc_bool     = 0x09;
static constexpr uint8_t tc_f32      = 0x0A;
static constexpr uint8_t tc_str      = 0x0B;
static constexpr uint8_t tc_blob     = 0x0C;
static constexpr uint8_t tc_nested   = 0x0D;
static constexpr uint8_t tc_obj_ref  = 0x0E;
static constexpr uint8_t tc_array    = 0x0F;
static constexpr uint8_t tc_null_ref = 0x13;
static constexpr uint8_t tc_f64      = 0x14;
static constexpr uint8_t tc_bbox     = 0x15;

// Tag name -> ID mapping
static const std::unordered_map<std::string, uint8_t>& tag_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"tags",0x02},{"classes",0x03},{"data",0x05},{"item",0x06},
        {"pair",0x07},{"key",0x08},{"empty",0x09},{"mname",0x0A},
        {"mvalue",0x0B},{"mtype",0x0C},{"mrawDataTable",0x0D},{"mrawData",0x0E},
        {"version",0x0F},{"build",0x10},{"base",0x11},{"leaf",0x12},
        {"mworldfWorldWidth",0x13},{"mworldfWorldHeight",0x14},
        {"mUTMzone",0x15},{"mUTMzoneNumber",0x16},
        {"libs",0x17},{"objManager",0x18},{"world",0x19},{"mbookmarks",0x1A},
        {"malpha",0x1B},{"mlayerVersion",0x1C},{"mnPriority",0x1D},
        {"mbVisible",0x1E},{"mbLocked",0x1F},{"mbOpaque",0x20},
        {"objectCount",0x21},{"tree",0x22},{"mobjectIDcounter",0x23},
        {"mlayerID",0x24},{"mnCoreVerticesCount",0x25},{"mfRadius",0x26},
        {"mpVertices",0x27},{"mdata",0x28},{"mfAzimuth",0x29},
        {"mdeleted",0x2A},{"moutlineColor",0x2B},{"mfillColor",0x2C},
        {"mpattern",0x2D},{"moutlineWidth",0x2E},{"mnVertexCount",0x2F},
        {"mareas",0x30},{"msurfExportMapframeName",0x31},{"msurfExportSurfName",0x32},
        {"name",0x33},{"count",0x34},{"x",0x35},{"y",0x36},{"z",0x37},
        {"mlTotalObjectCount",0x38},{"externLayers",0x39},{"externAreaLayers",0x3A},
        {"mactiveLayer",0x3B},{"mactiveAreaLayer",0x3C},
        {"namedSelections",0x3D},{"mroadbase",0x3E},{"mroadelement",0x3F},
        {"mapFrame",0x40},{"keyPoint",0x41},{"mobjectsDelta",0x42},
        {"mlayers",0x43},{"mareaLayers",0x44},{"satsurfcrunchwidth",0x45},
        {"wrpdirexport",0x46},{"wrpfilecfg",0x47},{"texgridfile",0x48},
        {"surfaceimported",0x49},{"satelliteImported",0x4A},
        {"gridX",0x4B},{"gridZ",0x4C},{"gridOriginal",0x4D},{"gridsize",0x4E},
        {"normalMapSuffix",0x4F},{"backgroundImage",0x50},
        {"tileSat",0x51},{"tileSurf",0x52},{"tileNorm",0x53},
        {"texoverlap",0x54},{"texcell",0x55},{"segIndex",0x56},
        {"satGridCellSize",0x57},{"imageryWidth",0x58},{"imageryResolution",0x59},
        {"mcount",0x5A},{"mgridMaskTile",0x5B},{"mgridMaskAllowed",0x5C},
        {"mgridMaskGeneratedLastTime",0x5D},
        {"size",0x5E},{"lock",0x5F},{"pos",0x60},{"cacheterrain",0x61},
        {"config",0x62},{"needToRebuildTerrain",0x63},{"needToGenerateLayers",0x64},
        {"mpos",0x65},{"mselected",0x66},{"mframe",0x67},
        {"text",0x68},{"properties",0x69},{"fillcolor",0x6A},{"outlinecolor",0x6B},
        {"id",0x6C},{"style",0x6D},{"visible",0x6E},{"angle",0x6F},
        {"mkey",0x70},{"keyparts",0x71},{"normalparts",0x72},
        {"color",0x73},{"outline",0x74},{"drawasfullline",0x75},
        {"maxangle",0x76},{"maxbankof",0x77},{"straight",0x78},
        {"curve",0x79},{"special",0x7A},{"terminator",0x7B},
        {"objectfilefilename",0x7C},{"canbank",0x7D},{"radius",0x7E},
        {"type",0x7F},{"min",0x80},{"max",0x81},{"standart",0x82},{"align",0x83},
        {"connecta",0x84},{"connectb",0x85},{"connectc",0x86},{"connectd",0x87},
        {"mroad",0x88},{"mcross",0x89},{"mroadGetRoads",0x8A},
        {"position",0x8B},{"orientation",0x8C},{"elevation",0x8D},
        {"mposition",0x8E},{"roadid",0x8F},{"keypointtype",0x90},
        {"keypointname",0x91},
        {"directiona",0x92},{"directionb",0x93},{"directionc",0x94},{"directiond",0x95},
        {"mID",0x96},{"mcenter",0x97},{"center",0x98},{"mcolor",0x99},
        {"mcontents",0x9A},{"view",0x9B},{"filename",0x9C},
        {"locked",0x9D},{"lockedToMapFrame",0x9E},{"mid",0x9F},
        {"viewdouble",0xA0},{"malphaBlend",0xA1},{"misVisible",0xA2},
        {"mlayerIndex",0xA3},
    };
    return m;
}

// Class name -> ID mapping
static const std::unordered_map<std::string, uint8_t>& class_ids() {
    static const std::unordered_map<std::string, uint8_t> m = {
        {"SRawData",0x01},{"CRawDataContainer",0x02},
        {"CVisitor4Project",0x03},{"CAlphaManagedItem",0x04},
        {"CLayer",0x05},{"CPointArea",0x06},
        {"CPolygonalArea",0x07},{"CPolylineArea",0x08},
        {"CAreaLayer",0x09},{"SNamedSelection",0x0A},
        {"SObjectCenterDelta",0x0B},{"CVisitor4ObjectManager",0x0C},
        {"SMapFrameConfiguration",0x0D},{"SMapFrame",0x0E},
        {"CMapFrame",0x0F},{"SKeyPoint",0x10},
        {"CKeyPoint",0x11},{"SRoadDefinition",0x12},
        {"SRoadStraight",0x13},{"SRoadCurve",0x14},
        {"SRoadSpecial",0x15},{"SRoadTerminator",0x16},
        {"SCrossDefinition",0x17},{"CRoadBase",0x18},
        {"CRoadElement",0x19},{"SKeyRoadElement",0x1A},
        {"SRoadElement",0x1B},{"CAreaVertex",0x1C},
        {"CVertex",0x1D},{"CWorld",0x1E},
        {"CWorldContent",0x1F},{"CWorldContentManager",0x20},
    };
    return m;
}

// Module-level pointer to the active ProjectInfo, set by write_tv4p().
// All Tv4pBuf instances use this for pointer allocation.
static ProjectInfo* g_proj = nullptr;

// Binary buffer with typed ALB1 write methods
class Tv4pBuf {
public:
    std::vector<uint8_t> data;

    uint32_t alloc_ptr() { return g_proj->alloc_ptr(); }

    void write_u8(uint8_t v) { data.push_back(v); }
    void write_u16(uint16_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(), p, p + 2); }
    void write_u32(uint32_t v) { auto p = reinterpret_cast<const uint8_t*>(&v); data.insert(data.end(), p, p + 4); }
    void write_f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); write_u32(u); }
    void write_f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); auto p = reinterpret_cast<const uint8_t*>(&u); data.insert(data.end(), p, p + 8); }
    void write_bytes(const uint8_t* buf, size_t len) { data.insert(data.end(), buf, buf + len); }
    void write_bytes(const std::vector<uint8_t>& v) { data.insert(data.end(), v.begin(), v.end()); }

    void tag(const std::string& name, uint8_t tc) {
        write_u8(tag_ids().at(name));
        write_u8(0x00);
        write_u8(tc);
    }

    void u32_val(const std::string& name, uint32_t v) { tag(name, tc_u32); write_u32(v); }
    void u32_alt(const std::string& name, uint32_t v) { tag(name, tc_u32_alt); write_u32(v); }
    void u32_cnt(const std::string& name, uint32_t v) { tag(name, tc_u32_cnt); write_u32(v); }
    void f32_val(const std::string& name, float v) { tag(name, tc_f32); write_f32(v); }
    void f64_val(const std::string& name, double v) { tag(name, tc_f64); write_f64(v); }
    void chr(const std::string& name, uint8_t v) { tag(name, tc_char); write_u8(v); }
    void boolean(const std::string& name, bool v) { tag(name, tc_bool); write_u8(v ? 1 : 0); }
    void color(const std::string& name, uint32_t v) { tag(name, tc_color); write_u32(v); }
    void null_ref(const std::string& name) { tag(name, tc_null_ref); }
    void array_mark(const std::string& name) { tag(name, tc_array); }

    void str(const std::string& name, const std::string& s) {
        tag(name, tc_str);
        if (s.size() > 0xFFFF) {
            throw std::runtime_error(std::format(
                "tv4p: string too long for u16 length prefix ({} bytes, tag '{}')", s.size(), name));
        }
        write_u16(static_cast<uint16_t>(s.size()));
        for (char c : s) write_u8(static_cast<uint8_t>(c));
    }

    void blob(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, tc_blob);
        write_u32(static_cast<uint32_t>(content.size()));
        write_bytes(content);
    }

    void nested(const std::string& name, const std::vector<uint8_t>& content) {
        tag(name, tc_nested);
        write_u32(static_cast<uint32_t>(content.size()));
        write_bytes(content);
    }

    void obj_ref(const std::string& name, const std::string& cls_name) {
        tag(name, tc_obj_ref);
        write_u8(class_ids().at(cls_name));
        write_u8(0x00);
        write_u32(alloc_ptr());
    }

    void obj_ref_ptr(const std::string& name, const std::string& cls_name, uint32_t ptr) {
        tag(name, tc_obj_ref);
        write_u8(class_ids().at(cls_name));
        write_u8(0x00);
        write_u32(ptr);
    }

    void bbox(const std::string& name, double x1, double y1, double x2, double y2) {
        tag(name, tc_bbox);
        write_u8(4);
        write_f64(x1); write_f64(y1); write_f64(x2); write_f64(y2);
    }

    void point3d(const std::string& name, double x, double y, double z) {
        tag(name, tc_bbox);
        write_u8(3);
        write_f64(x); write_f64(y); write_f64(z);
    }

    void class_preamble(const std::string& cls_name) {
        write_u8(class_ids().at(cls_name));
        write_u8(0x00);
        write_u32(alloc_ptr());
    }

    void class_preamble_ptr(const std::string& cls_name, uint32_t ptr) {
        write_u8(class_ids().at(cls_name));
        write_u8(0x00);
        write_u32(ptr);
    }
};

static void ensure_layer_pointers(ProjectInfo& p) {
    if (p.active_layer_ptr == 0) p.active_layer_ptr = p.alloc_ptr();
    if (p.active_area_ptr == 0) p.active_area_ptr = p.alloc_ptr();
}

// Write string table (tags or classes)
static void write_string_table(std::vector<uint8_t>& out, uint8_t preamble_tag,
                                const std::unordered_map<std::string, uint8_t>& entries) {
    out.push_back(preamble_tag);
    out.push_back(0x00);
    out.push_back(0x0F);

    struct E { uint8_t id; std::string name; };
    std::vector<E> sorted;
    for (const auto& [name, id] : entries)
        sorted.push_back({id, name});
    std::sort(sorted.begin(), sorted.end(), [](const E& a, const E& b) { return a.id < b.id; });

    uint32_t count = static_cast<uint32_t>(sorted.size());
    auto p = reinterpret_cast<const uint8_t*>(&count);
    out.insert(out.end(), p, p + 4);

    for (const auto& e : sorted) {
        out.push_back(e.id);
        out.push_back(0x00);
        uint16_t slen = static_cast<uint16_t>(e.name.size());
        auto sp = reinterpret_cast<const uint8_t*>(&slen);
        out.insert(out.end(), sp, sp + 2);
        for (char c : e.name) out.push_back(static_cast<uint8_t>(c));
    }
}

// ============================================================================
// TV4P builder functions
// ============================================================================

static std::vector<uint8_t> empty_blob_4() { return {0, 0, 0, 0}; }

static std::string cat_file_name(const std::string& cat) {
    std::string s = cat;
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

static std::vector<uint8_t> build_grid_mask_blob(int n, bool value) {
    Tv4pBuf buf;
    buf.write_u32(static_cast<uint32_t>(n));
    for (int i = 0; i < n; i++) buf.boolean("item", value);
    return buf.data;
}

static std::vector<uint8_t> build_content_entry(uint32_t id, const std::string& name,
                                                  const std::string& filename, bool locked,
                                                  uint32_t content_type, uint32_t layer_index,
                                                  const double bbox_vals[4]) {
    Tv4pBuf inner;
    inner.class_preamble("CWorldContent");
    inner.str("name", name);
    inner.str("filename", filename);
    inner.boolean("locked", locked);
    inner.boolean("lockedToMapFrame", false);
    inner.u32_val("type", content_type);
    inner.u32_alt("mid", id);
    inner.bbox("viewdouble", bbox_vals[0], bbox_vals[1], bbox_vals[2], bbox_vals[3]);
    inner.f32_val("malphaBlend", 1.0f);
    inner.boolean("misVisible", true);
    inner.u32_val("mlayerIndex", layer_index);
    return inner.data;
}

static std::vector<uint8_t> build_contents_blob(ProjectInfo& p, const double bbox_vals[4]) {
    std::string src_prefix = "P:\\" + p.p_drive_dir() + "\\source\\";

    struct ContentItem {
        uint32_t id;
        std::string name, filename;
        bool locked;
        uint32_t content_type, layer_index;
    };
    std::vector<ContentItem> items = {
        {1, "Map-heightfield", "", true, 0, 0},
        {2, src_prefix + "heightmap.asc", src_prefix + "heightmap.asc", false, 0, 1},
    };

    Tv4pBuf blob;
    blob.write_u32(static_cast<uint32_t>(items.size()));
    for (const auto& it : items) {
        blob.array_mark("pair");
        blob.u32_val("key", it.id);
        auto entry = build_content_entry(it.id, it.name, it.filename, it.locked,
                                          it.content_type, it.layer_index, bbox_vals);
        blob.nested("data", entry);
    }
    return blob.data;
}

static std::vector<uint8_t> build_smap_frame_config(ProjectInfo& p) {
    auto& w = *p.world;
    int hm_size = p.hm_width > 0 ? p.hm_width : w.grid.cells_x;
    int grid_size = w.grid.cells_x;
    double grid_cell_size = w.bounds.world_size_x / grid_size;
    int imagery_width = static_cast<int>(w.bounds.world_size_x);

    uint32_t texcell = 512;
    uint32_t texoverlap = 128;
    int tiles_per_axis = static_cast<int>(std::ceil(static_cast<double>(imagery_width) / (texcell - texoverlap)));
    int mcount = tiles_per_axis * tiles_per_axis;

    uint32_t seg_index = 3;
    uint32_t sat_grid_cell_size = seg_index * static_cast<uint32_t>(imagery_width) / static_cast<uint32_t>(grid_size);

    std::string dir_path = p.p_drive_dir();
    std::string wrp_dir = "p:\\" + dir_path;
    std::string wrp_file_cfg = dir_path + "\\source\\layers.cfg";

    Tv4pBuf cfg;
    cfg.class_preamble("SMapFrameConfiguration");
    cfg.str("wrpdirexport", wrp_dir);
    cfg.str("wrpfilecfg", wrp_file_cfg);
    cfg.str("texgridfile", "");
    cfg.boolean("surfaceimported", false);
    cfg.boolean("satelliteImported", false);
    cfg.f64_val("gridX", grid_cell_size);
    cfg.f64_val("gridZ", grid_cell_size);
    cfg.f64_val("gridOriginal", static_cast<double>(sat_grid_cell_size));
    cfg.u32_val("gridsize", static_cast<uint32_t>(grid_size));
    cfg.str("normalMapSuffix", "");
    cfg.str("backgroundImage", "");
    cfg.str("tileSat", "");
    cfg.str("tileSurf", "");
    cfg.str("tileNorm", "");
    cfg.u32_val("texoverlap", texoverlap);
    cfg.u32_val("texcell", texcell);
    cfg.u32_val("segIndex", seg_index);
    cfg.u32_val("satGridCellSize", sat_grid_cell_size);
    cfg.u32_val("imageryWidth", static_cast<uint32_t>(imagery_width));
    cfg.f64_val("imageryResolution", 1.0);
    cfg.u32_val("mcount", static_cast<uint32_t>(mcount));
    cfg.blob("mgridMaskTile", build_grid_mask_blob(mcount, false));
    cfg.blob("mgridMaskAllowed", build_grid_mask_blob(mcount, true));
    cfg.blob("mgridMaskGeneratedLastTime", build_grid_mask_blob(mcount, false));

    (void)hm_size;
    return cfg.data;
}

static std::vector<uint8_t> build_smap_frame(ProjectInfo& p) {
    int hm_size = p.hm_width > 0 ? p.hm_width : p.world->grid.cells_x;
    double bv[4] = {
        p.offset_x, p.offset_z,
        p.offset_x + p.world->bounds.world_size_x,
        p.offset_z + p.world->bounds.world_size_y,
    };

    auto config_payload = build_smap_frame_config(p);

    Tv4pBuf frame;
    frame.class_preamble("SMapFrame");
    frame.str("name", "Map");
    frame.u32_val("size", static_cast<uint32_t>(hm_size));
    frame.boolean("lock", false);
    frame.str("cacheterrain", "Map-heightfield");
    frame.nested("config", config_payload);
    frame.boolean("needToRebuildTerrain", false);
    frame.boolean("needToGenerateLayers", false);
    frame.bbox("mpos", bv[0], bv[1], bv[2], bv[3]);
    return frame.data;
}

static std::vector<uint8_t> build_map_frame(ProjectInfo& p) {
    auto frame_payload = build_smap_frame(p);
    Tv4pBuf mframe_blob;
    mframe_blob.write_u32(1);
    mframe_blob.nested("item", frame_payload);

    Tv4pBuf mf;
    mf.class_preamble("CMapFrame");
    mf.u32_val("mselected", 0);
    mf.blob("mframe", mframe_blob.data);
    return mf.data;
}

static uint32_t total_object_count(const ProjectInfo& p) {
    uint32_t total = 0;
    for (const auto& [_, objs] : p.cat_objects) total += static_cast<uint32_t>(objs.size());
    return total;
}

static std::vector<std::string> used_model_basenames(const ProjectInfo& p) {
    std::unordered_map<std::string, bool> seen;
    for (const auto& [_, objs] : p.cat_objects) {
        for (const auto& obj : objs) {
            if (!obj.model_name.empty()) seen[obj.model_name] = true;
        }
    }
    std::vector<std::string> out;
    out.reserve(seen.size());
    for (const auto& [name, _] : seen) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

static std::string normalize_model_path(const std::string& path_or_base) {
    std::string s = path_or_base;
    std::replace(s.begin(), s.end(), '/', '\\');
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (low.size() < 4 || low.substr(low.size() - 4) != ".p3d") s += ".p3d";
    return s;
}

static std::string resolve_model_path_for_delta(const ProjectInfo& p, const std::string& base) {
    auto it = p.model_path_by_base.find(base);
    if (it != p.model_path_by_base.end() && !it->second.empty())
        return normalize_model_path(it->second);
    return normalize_model_path(base);
}

static std::vector<uint8_t> build_mobjects_delta_blob(const ProjectInfo& p) {
    auto models = used_model_basenames(p);
    Tv4pBuf blob;
    blob.write_u32(static_cast<uint32_t>(models.size()));
    for (const auto& base : models) {
        Tv4pBuf delta;
        delta.class_preamble("SObjectCenterDelta");
        delta.f64_val("x", 0.0);
        delta.f64_val("z", 0.0);

        blob.array_mark("pair");
        blob.str("key", resolve_model_path_for_delta(p, base));
        blob.nested("data", delta.data);
    }
    return blob.data;
}

static std::vector<uint8_t> build_obj_manager(ProjectInfo& p) {
    ensure_layer_pointers(p);
    Tv4pBuf om;
    om.class_preamble("CVisitor4ObjectManager");
    om.u32_cnt("mlTotalObjectCount", total_object_count(p));
    om.null_ref("externLayers");
    om.null_ref("externAreaLayers");
    om.obj_ref_ptr("mactiveLayer", "CLayer", p.active_layer_ptr);
    om.obj_ref_ptr("mactiveAreaLayer", "CAreaLayer", p.active_area_ptr);

    om.blob("mareas", empty_blob_4());
    om.blob("namedSelections", empty_blob_4());

    // mroadbase
    Tv4pBuf roadbase;
    roadbase.class_preamble("CRoadBase");
    roadbase.blob("mroad", empty_blob_4());
    roadbase.blob("mcross", empty_blob_4());
    om.nested("mroadbase", roadbase.data);

    // mroadelement
    Tv4pBuf roadelem;
    roadelem.class_preamble("CRoadElement");
    roadelem.blob("mroadGetRoads", empty_blob_4());
    om.nested("mroadelement", roadelem.data);

    om.nested("mapFrame", build_map_frame(p));

    // keyPoint
    Tv4pBuf kp;
    kp.class_preamble("CKeyPoint");
    kp.blob("mkey", empty_blob_4());
    om.nested("keyPoint", kp.data);

    om.blob("mrawData", empty_blob_4());
    om.blob("mobjectsDelta", build_mobjects_delta_blob(p));

    return om.data;
}

static std::vector<uint8_t> build_world(ProjectInfo& p) {
    double bv[4] = {
        p.offset_x,
        p.offset_z + p.world->bounds.world_size_y,
        p.offset_x + p.world->bounds.world_size_x,
        p.offset_z,
    };
    auto contents_blob = build_contents_blob(p, bv);

    Tv4pBuf wcm;
    wcm.class_preamble("CWorldContentManager");
    wcm.blob("mcontents", contents_blob);

    Tv4pBuf world;
    world.class_preamble("CWorld");
    world.nested("mcontents", wcm.data);
    return world.data;
}

static std::vector<uint8_t> build_libs_blob(const ProjectInfo& p) {
    std::vector<std::string> libs;
    for (const auto& cat : p.categories)
        libs.push_back("TemplateLibs\\" + cat_file_name(cat) + ".tml");

    Tv4pBuf blob;
    blob.write_u32(static_cast<uint32_t>(libs.size()));
    for (const auto& lib : libs) blob.str("item", lib);
    return blob.data;
}

// Derive UTM zone letter from latitude (simplified MGRS band lookup).
static char utm_zone_letter(double latitude) {
    int band = static_cast<int>(std::floor((latitude + 80.0) / 8.0));
    const char* letters = "CDEFGHJKLMNPQRSTUVWX";
    if (band < 0) band = 0;
    if (band > 19) band = 19;
    return letters[band];
}

static std::vector<uint8_t> build_data_payload(ProjectInfo& p) {
    std::string lower_name = p.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;

    Tv4pBuf root;
    root.class_preamble("CVisitor4Project");
    root.u32_val("version", 21);
    root.u32_val("build", 112);
    root.f32_val("base", 10000.0f);
    root.f32_val("leaf", 100.0f);
    root.str("mname", map_name);
    root.f32_val("mworldfWorldWidth", 0);
    root.f32_val("mworldfWorldHeight", 0);
    char utm_letter = 'N';
    uint32_t utm_number = 33;
    if (p.meta) {
        if (p.meta->map_zone > 0) utm_number = static_cast<uint32_t>(p.meta->map_zone);
        if (p.meta->latitude != 0) utm_letter = utm_zone_letter(p.meta->latitude);
    }
    root.chr("mUTMzone", static_cast<uint8_t>(utm_letter));
    root.u32_val("mUTMzoneNumber", utm_number);
    root.blob("libs", build_libs_blob(p));
    root.nested("objManager", build_obj_manager(p));
    root.nested("world", build_world(p));
    root.blob("mbookmarks", empty_blob_4());
    return root.data;
}

void write_tv4p(ProjectInfo& p) {
    g_proj = &p;
    std::string lower_name = p.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;
    std::string out_path = (fs::path(p.output_dir) / (map_name + ".tv4p")).string();

    std::vector<uint8_t> file;

    // Header: "ALB1" + version(1) + subversion(21) + reserved(0) = 16 bytes
    file.push_back('A'); file.push_back('L'); file.push_back('B'); file.push_back('1');
    auto push_u32 = [&](uint32_t v) {
        auto pp = reinterpret_cast<const uint8_t*>(&v);
        file.insert(file.end(), pp, pp + 4);
    };
    push_u32(1);
    push_u32(21);
    push_u32(0);

    write_string_table(file, tag_ids().at("tags"), tag_ids());
    write_string_table(file, tag_ids().at("classes"), class_ids());

    auto payload = build_data_payload(p);
    file.insert(file.end(), payload.begin(), payload.end());

    std::ofstream out(out_path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create " + out_path);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

// Also export the ALB1 file writer and Tv4pBuf for tv4s/tv4l
void write_alb1_file(const std::string& path, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> file;
    file.push_back('A'); file.push_back('L'); file.push_back('B'); file.push_back('1');
    auto push_u32 = [&](uint32_t v) {
        auto pp = reinterpret_cast<const uint8_t*>(&v);
        file.insert(file.end(), pp, pp + 4);
    };
    push_u32(1);
    push_u32(0);
    push_u32(0);

    write_string_table(file, tag_ids().at("tags"), tag_ids());
    write_string_table(file, tag_ids().at("classes"), class_ids());

    file.insert(file.end(), payload.begin(), payload.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create " + path);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}
