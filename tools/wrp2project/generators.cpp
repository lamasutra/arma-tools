#include "project.h"

#include "armatools/config.h"
#include "armatools/forestshape.h"
#include "armatools/objcat.h"
#include "armatools/roadnet.h"
#include "armatools/shp.h"
#include "armatools/surface.h"
#include "armatools/pbo.h"
#include "armatools/pboindex.h"
#include "armatools/armapath.h"
#include "../common/cli_logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

// ============================================================================
// Metadata
// ============================================================================

static std::string find_string(const armatools::config::ConfigClass& cls, const std::string& name) {
    for (const auto& ne : cls.entries) {
        std::string lhs = ne.name;
        std::transform(lhs.begin(), lhs.end(), lhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string rhs = name;
        std::transform(rhs.begin(), rhs.end(), rhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lhs == rhs) {
            if (auto* se = std::get_if<armatools::config::StringEntry>(&ne.entry))
                return se->value;
        }
    }
    return "";
}

static int find_int(const armatools::config::ConfigClass& cls, const std::string& name) {
    for (const auto& ne : cls.entries) {
        std::string lhs = ne.name;
        std::transform(lhs.begin(), lhs.end(), lhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string rhs = name;
        std::transform(rhs.begin(), rhs.end(), rhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lhs == rhs) {
            if (auto* ie = std::get_if<armatools::config::IntEntry>(&ne.entry))
                return ie->value;
            if (auto* fe = std::get_if<armatools::config::FloatEntry>(&ne.entry))
                return static_cast<int>(fe->value);
        }
    }
    return 0;
}

static double find_float(const armatools::config::ConfigClass& cls, const std::string& name) {
    for (const auto& ne : cls.entries) {
        std::string lhs = ne.name;
        std::transform(lhs.begin(), lhs.end(), lhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string rhs = name;
        std::transform(rhs.begin(), rhs.end(), rhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lhs == rhs) {
            if (auto* fe = std::get_if<armatools::config::FloatEntry>(&ne.entry))
                return fe->value;
            if (auto* ie = std::get_if<armatools::config::IntEntry>(&ne.entry))
                return static_cast<double>(ie->value);
        }
    }
    return 0;
}

MapMetadata* read_map_metadata(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        armatools::cli::log_warning("cannot open config", path);
        return nullptr;
    }

    armatools::config::Config cfg;
    try {
        cfg = armatools::config::parse_text(f);
    } catch (const std::exception& e) {
        armatools::cli::log_warning("parsing config:", e.what());
        return nullptr;
    }

    // Find CfgWorlds
    const armatools::config::ConfigClass* worlds = nullptr;
    for (const auto& ne : cfg.root.entries) {
        std::string lhs = ne.name;
        std::transform(lhs.begin(), lhs.end(), lhs.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lhs == "cfgworlds") {
            if (auto* ce = std::get_if<armatools::config::ClassEntryOwned>(&ne.entry)) {
                worlds = ce->cls.get();
                break;
            }
        }
    }
    if (!worlds) {
        armatools::cli::log_warning("CfgWorlds not found in config");
        return nullptr;
    }

    // Find concrete world class (has worldName property)
    for (const auto& ne : worlds->entries) {
        auto* ce = std::get_if<armatools::config::ClassEntryOwned>(&ne.entry);
        if (!ce) continue;
        const auto* cls = ce->cls.get();
        if (cls->external || cls->deletion) continue;
        if (find_string(*cls, "worldName").empty()) continue;

        auto* meta = new MapMetadata();
        meta->world_name = find_string(*cls, "worldName");
        meta->description = find_string(*cls, "description");
        meta->author = find_string(*cls, "author");
        meta->new_roads_shape = find_string(*cls, "newRoadsShape");
        meta->map_size = find_int(*cls, "mapSize");
        meta->map_zone = find_int(*cls, "mapZone");
        meta->longitude = find_float(*cls, "longitude");
        meta->latitude = find_float(*cls, "latitude");
        meta->elevation_offset = find_int(*cls, "elevationOffset");
        meta->start_time = find_string(*cls, "startTime");
        meta->start_date = find_string(*cls, "startDate");
        return meta;
    }

    armatools::cli::log_warning("no concrete world class found in CfgWorlds");
    return nullptr;
}

std::string resolve_new_roads_shape(const std::string& drive_root, const std::string& new_roads_shape) {
    std::string p = new_roads_shape;
    if (!p.empty() && p[0] == '\\') p.erase(0, 1);
    std::replace(p.begin(), p.end(), '\\', '/');
    std::string full = (fs::path(drive_root) / p).string();
    if (fs::exists(full)) return full;
    return "";
}

static std::string meta_string_or(const MapMetadata* m, std::string MapMetadata::*field, const std::string& def) {
    if (m && !(m->*field).empty()) return m->*field;
    return def;
}

static int meta_int_or(const MapMetadata* m, int MapMetadata::*field, int def) {
    if (m && m->*field != 0) return m->*field;
    return def;
}

static std::string meta_float_str(const MapMetadata* m, double MapMetadata::*field, double def) {
    double v = (m && m->*field != 0) ? m->*field : def;
    return std::format("{:g}", v);
}

// ============================================================================
// Heightmap
// ============================================================================

static std::vector<float> resample_elevations(const std::vector<float>& src,
                                                int src_w, int src_h, int dst_w, int dst_h) {
    std::vector<float> dst(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h));
    for (int dy = 0; dy < dst_h; dy++) {
        double sy = static_cast<double>(dy) * static_cast<double>(src_h - 1) / static_cast<double>(dst_h - 1);
        int y0 = static_cast<int>(sy);
        int y1 = std::min(y0 + 1, src_h - 1);
        double fy = sy - y0;

        for (int dx = 0; dx < dst_w; dx++) {
            double sx = static_cast<double>(dx) * static_cast<double>(src_w - 1) / static_cast<double>(dst_w - 1);
            int x0 = static_cast<int>(sx);
            int x1 = std::min(x0 + 1, src_w - 1);
            double fx = sx - x0;

            double v00 = src[static_cast<size_t>(y0 * src_w + x0)];
            double v10 = src[static_cast<size_t>(y0 * src_w + x1)];
            double v01 = src[static_cast<size_t>(y1 * src_w + x0)];
            double v11 = src[static_cast<size_t>(y1 * src_w + x1)];
            double v = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy) + v01 * (1 - fx) * fy + v11 * fx * fy;
            dst[static_cast<size_t>(dy * dst_w + dx)] = static_cast<float>(v);
        }
    }
    return dst;
}

void init_heightmap(ProjectInfo& p, int scale) {
    auto& w = *p.world;
    if (w.elevations.empty()) return;

    int src_w = w.grid.terrain_x;
    int src_h = w.grid.terrain_y;
    if (static_cast<int>(w.elevations.size()) != src_w * src_h) {
        src_w = w.grid.cells_x;
        src_h = w.grid.cells_y;
    }
    if (static_cast<int>(w.elevations.size()) != src_w * src_h) {
        throw std::runtime_error(std::format("elevation data size {} does not match grid {}x{}",
                                              w.elevations.size(), src_w, src_h));
    }

    if (scale <= 1) {
        p.hm_width = src_w;
        p.hm_height = src_h;
        p.hm_elevations = w.elevations;
        return;
    }

    if (scale != 2 && scale != 4 && scale != 8 && scale != 16) {
        throw std::runtime_error(std::format("unsupported heightmap scale factor {} (must be 2, 4, 8, or 16)", scale));
    }

    int dst_w = src_w * scale;
    int dst_h = src_h * scale;
    armatools::cli::log_plain(std::format("Heightmap: upscaling {}x{} -> {}x{} ({}x)", src_w, src_h, dst_w, dst_h, scale));

    p.hm_width = dst_w;
    p.hm_height = dst_h;
    p.hm_elevations = resample_elevations(w.elevations, src_w, src_h, dst_w, dst_h);
}

void write_heightmap_asc(ProjectInfo& p) {
    if (p.hm_elevations.empty()) throw std::runtime_error("no elevation data in WRP");

    int width = p.hm_width;
    int height = p.hm_height;
    double cell_size = p.world->bounds.world_size_x / width;

    std::string path = (fs::path(p.output_dir) / "source" / "heightmap.asc").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << std::format("ncols         {}\n", width);
    f << std::format("nrows         {}\n", height);
    f << std::format("xllcorner     {:.6f}\n", p.offset_x);
    f << std::format("yllcorner     {:.6f}\n", p.offset_z);
    f << std::format("cellsize      {:.6f}\n", cell_size);
    f << "NODATA_value  -9999\n";

    // ESRI ASCII Grid: top-to-bottom; WRP: row 0 = south. Write reversed.
    for (int row = height - 1; row >= 0; row--) {
        int offset = row * width;
        for (int col = 0; col < width; col++) {
            if (col > 0) f << ' ';
            f << std::format("{:.4f}", p.hm_elevations[static_cast<size_t>(offset + col)]);
        }
        f << '\n';
    }
}

// ============================================================================
// Config generation
// ============================================================================

void write_config_cpp(ProjectInfo& p) {
    auto& w = *p.world;
    std::string map_class = "map_" + p.name;
    int map_size = static_cast<int>(w.bounds.world_size_x);
    double terrain_grid_size = (p.hm_width > 0)
        ? w.bounds.world_size_x / p.hm_width
        : w.bounds.world_size_x / w.grid.cells_x;
    double grid_cell_size = w.grid.cell_size;

    std::string path = (fs::path(p.output_dir) / "config.cpp").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << std::format(R"(class CfgPatches {{
    class {} {{
        units[] = {{}};
        weapons[] = {{}};
        requiredVersion = 1.0;
        requiredAddons[] = {{"A3_Map_Stratis"}};
    }};
}};

)", map_class);

    f << std::format(R"(class CfgWorldList {{
    class {} {{}};
}};

)", map_class);

    f << std::format(R"(class CfgWorlds {{
    class DefaultWorld {{}};
    class CAWorld : DefaultWorld {{}};
    class {} : CAWorld {{
        description = "{}";
        worldName = "{}\{}.wrp";
        author = "wrp2project";
        pictureMap = "{}\data\pictureMap_ca.paa";

        // Terrain grid
        mapSize = {};
        mapZone = {};
        newRoadsShape = "{}\data\roads\roads.shp";
        centerPosition[] = {{{}, {}}};
        ilsDirection[] = {{0, 0.08, 1}};
        ilsPosition[] = {{0, 0}};
        ilsTaxiIn[] = {{}};
        ilsTaxiOff[] = {{}};

        // Grid settings
        startTime = "{}";
        startDate = "{}";
        longitude = {};
        latitude = {};

)", map_class, p.name, map_class, map_class, map_class,
       map_size,
       meta_int_or(p.meta, &MapMetadata::map_zone, 0),
       map_class, map_size / 2, map_size / 2,
       meta_string_or(p.meta, &MapMetadata::start_time, "10:00"),
       meta_string_or(p.meta, &MapMetadata::start_date, "15/6/2035"),
       meta_float_str(p.meta, &MapMetadata::longitude, 14.0),
       meta_float_str(p.meta, &MapMetadata::latitude, -48.0));

    f << R"(        class Grid {
            class Zoom1 {
                zoomMax = 0.15;
                format = "XY";
                formatX = "000";
                formatY = "000";
                stepX = 100;
                stepY = -100;
            };
            class Zoom2 {
                zoomMax = 0.85;
                format = "XY";
                formatX = "00";
                formatY = "00";
                stepX = 1000;
                stepY = -1000;
            };
            class Zoom3 {
                zoomMax = 1e30;
                format = "XY";
                formatX = "0";
                formatY = "0";
                stepX = 10000;
                stepY = -10000;
            };
        };

)";

    f << std::format(R"(        // Elevation & terrain grid
        class Elevation {{
            minE = {:.1f};
            minEcliptic = -10;
        }};

        gridOffsetY = {:.6f};
        terrainGridSize = {:.6f};
        gridCellSize = {:.6f};

)", w.bounds.min_elevation, p.offset_z, terrain_grid_size, grid_cell_size);

    f << std::format(R"(        // Included configs
        #include "cfgSurfaces.hpp"
        #include "cfgClutter.hpp"
        #include "Map_{}.hpp"

        // Satellite & outside texture
        satMapTexture = "{}\data\s_satout_co.paa";

        class OutsideTerrain {{
            satellite = "{}\data\s_satout_co.paa";
            enableTerrainSynth = 0;
            class Layers {{
                class Layer0 {{
                    nopx = "{}\data\L_middle_mco.paa";
                    texture = "";
                }};
            }};
        }};
    }};
}};
)", p.name, map_class, map_class, map_class);
}

// ============================================================================
// cfgSurfaces / cfgClutter
// ============================================================================

struct SurfaceProps {
    double rough;
    double dust;
    std::string sound_environ;
    std::string sound_hit;
};

static SurfaceProps surface_properties(armatools::surface::Category cat) {
    using C = armatools::surface::Category;
    switch (cat) {
    case C::Road:     return {0.05, 0.3, "road", "concrete"};
    case C::Water:    return {0.0,  0.0, "water", "water"};
    case C::Forest:   return {0.1,  0.1, "forest", "soft_ground"};
    case C::Farmland: return {0.08, 0.4, "grass", "soft_ground"};
    case C::Rock:     return {0.12, 0.2, "gravel", "rock"};
    case C::Dirt:     return {0.1,  0.5, "dirt", "soft_ground"};
    default:          return {0.08, 0.2, "grass", "soft_ground"};
    }
}

void write_cfg_surfaces(ProjectInfo& p) {
    auto& w = *p.world;
    std::string path = (fs::path(p.output_dir) / "cfgSurfaces.hpp").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << "// Surface types generated from WRP textures\n\n";
    f << "class CfgSurfaces {\n";
    f << "    class Default {};\n\n";

    for (size_t i = 0; i < w.textures.size(); i++) {
        auto ci = armatools::surface::classify(w.textures[i].filename);
        std::string cn = layer_class_name(p.prefix, static_cast<int>(i), w.textures[i].filename);
        auto props = surface_properties(ci.category);

        f << std::format("    class {} : Default {{\n", cn);
        f << std::format("        files = \"{}_*\";\n", cn);
        f << std::format("        rough = {:.2f};\n", props.rough);
        f << std::format("        dust = {:.2f};\n", props.dust);
        f << std::format("        soundEnviron = \"{}\";\n", props.sound_environ);
        f << std::format("        soundHit = \"{}\";\n", props.sound_hit);
        f << std::format("        character = \"{}Character\";\n", cn);
        f << std::format("        // source: {}\n", w.textures[i].filename);
        f << "    };\n";
    }
    f << "};\n\n";

    f << "class CfgSurfaceCharacters {\n";
    for (size_t i = 0; i < w.textures.size(); i++) {
        std::string cn = layer_class_name(p.prefix, static_cast<int>(i), w.textures[i].filename);
        f << std::format("    class {}Character {{\n", cn);
        f << "        probability[] = {0.5, 0};\n";
        f << "        names[] = {\"DefaultClutter\"};\n";
        f << "    };\n";
    }
    f << "};\n";
}

void write_cfg_clutter(ProjectInfo& p) {
    std::string path = (fs::path(p.output_dir) / "cfgClutter.hpp").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << R"(// Ground clutter definitions

class CfgClutter {
    class DefaultClutter {
        model = "";
        affectedByWind = 0;
        swLighting = 0;
        scaleMin = 0.5;
        scaleMax = 1.0;
    };
};
)";
}

void write_named_locations(ProjectInfo& p) {
    std::string path = (fs::path(p.output_dir) / std::format("Map_{}.hpp", p.name)).string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << std::format(R"(// Named locations for {}

class Names {{
    // Add named locations here, e.g.:
    // class Town1 {{
    //     name = "Example Town";
    //     position[] = {{1000, 1000}};
    //     type = "NameCity";
    //     radiusA = 200;
    //     radiusB = 200;
    //     angle = 0;
    // }};
}};
)", p.name);
}

// ============================================================================
// Layers
// ============================================================================

std::string layer_class_name(const std::string& prefix, int index, const std::string& tex_filename) {
    std::string base = tex_filename;
    auto sep = base.find_last_of("\\/");
    if (sep != std::string::npos) base = base.substr(sep + 1);
    for (const auto& ext : {".rvmat", ".paa", ".tga", ".png"})
        if (base.size() > std::strlen(ext) && base.substr(base.size() - std::strlen(ext)) == ext)
            base.erase(base.size() - std::strlen(ext));
    for (const auto& suf : {"_nopx", "_co", "_mco", "_lco", "_dt"})
        if (base.size() > std::strlen(suf) && base.substr(base.size() - std::strlen(suf)) == suf)
            base.erase(base.size() - std::strlen(suf));

    std::string cleaned;
    for (char c : base) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            cleaned += c;
        else
            cleaned += '_';
    }
    if (cleaned.empty()) cleaned = std::format("tex{}", index);
    return prefix + "_" + cleaned;
}

static uint8_t add_byte(uint8_t a, uint8_t b) {
    int sum = static_cast<int>(a) + static_cast<int>(b);
    return static_cast<uint8_t>(std::min(sum, 255));
}

static armatools::surface::RGB vary_color(armatools::surface::RGB c, int n) {
    uint8_t shift = static_cast<uint8_t>((n * 17) % 40);
    return {add_byte(c.r, shift), add_byte(c.g, static_cast<uint8_t>(shift + 7)),
            add_byte(c.b, static_cast<uint8_t>(shift + 13))};
}

void write_layers_cfg(ProjectInfo& p) {
    auto& w = *p.world;
    std::string path = (fs::path(p.output_dir) / "source" / "layers.cfg").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    f << "// Texture-to-color mapping generated from WRP textures\n";
    f << "// Use with mask.tif surface type painting in Terrain Builder\n\n";
    f << "class Legend {\n";

    // Track color usage for varying duplicates
    struct RGBHash {
        size_t operator()(armatools::surface::RGB c) const {
            return (static_cast<size_t>(c.r) << 16) | (static_cast<size_t>(c.g) << 8) | c.b;
        }
    };
    struct RGBEq {
        bool operator()(armatools::surface::RGB a, armatools::surface::RGB b) const {
            return a.r == b.r && a.g == b.g && a.b == b.b;
        }
    };
    std::unordered_map<armatools::surface::RGB, int, RGBHash, RGBEq> color_use;

    for (size_t i = 0; i < w.textures.size(); i++) {
        auto ci = armatools::surface::classify(w.textures[i].filename);
        auto color = ci.color;
        int n = color_use[color]++;
        if (n > 0) color = vary_color(color, n);

        std::string cn = layer_class_name(p.prefix, static_cast<int>(i), w.textures[i].filename);
        f << std::format("    class {} {{\n", cn);
        f << std::format("        color[] = {{{}, {}, {}}};\n", color.r, color.g, color.b);
        f << std::format("        // source: {}\n", w.textures[i].filename);
        f << "    };\n";
    }
    f << "};\n";
}

// ============================================================================
// Roads
// ============================================================================

struct RoadTypeDefaults {
    int width;
    std::string str_tex;
    std::string ter_tex;
    std::string mat;
    std::string map_label;
};

static const std::unordered_map<std::string, RoadTypeDefaults>& known_road_types() {
    static const std::unordered_map<std::string, RoadTypeDefaults> table = {
        {"MainRoad", {8, R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road_end_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road.rvmat)", "main road"}},
        {"Road",     {6, R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road_end_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_main_road.rvmat)", "road"}},
        {"Track",    {4, R"(\a3\roads_f\Roads\data\surf_roadtarmac_path_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_path_end_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_path.rvmat)", "track"}},
        {"Trail",    {2, R"(\a3\roads_f\Roads\data\surf_roadtarmac_path_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_path_end_co.paa)",
                         R"(\a3\roads_f\Roads\data\surf_roadtarmac_path.rvmat)", "trail"}},
    };
    return table;
}

static void write_road_type_entry(std::ofstream& f, const std::string& type_name, const std::string& parent) {
    auto& table = known_road_types();
    auto it = table.find(type_name);
    RoadTypeDefaults props = (it != table.end()) ? it->second : table.at("Road");
    bool known = (it != table.end());
    if (!known) props.map_label = type_name;

    if (parent.empty())
        f << std::format("    class {} {{\n", type_name);
    else
        f << std::format("\n    class {} : {} {{\n", type_name, parent);

    f << std::format("        width = {};\n", props.width);
    f << std::format("        mainStrTex  = \"{}\";\n", props.str_tex);
    f << std::format("        mainTerTex  = \"{}\";\n", props.ter_tex);
    f << std::format("        mainMat     = \"{}\";\n", props.mat);
    f << std::format("        map         = \"{}\";\n", props.map_label);
    f << "        AIPathOffset = 0.5;\n";
    if (!known)
        f << std::format("        // TODO: adjust width, textures, and map label for custom type \"{}\"\n", type_name);
    f << "    };\n";
}

void write_roads_lib(ProjectInfo& p) {
    std::unordered_map<std::string, int> used_types;
    for (const auto& obj : p.world->objects) {
        auto rt = p.road_map->classify(obj.model_name);
        if (rt) used_types[*rt]++;
    }

    std::vector<std::string> types;
    for (const auto& [t, _] : used_types) types.push_back(t);
    std::sort(types.begin(), types.end());

    std::string path = (fs::path(p.output_dir) / "data" / "roads" / "RoadsLib.cfg").string();
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);

    int total = 0;
    for (const auto& [_, c] : used_types) total += c;

    f << std::format("// Road type definitions for {}\n", p.name);
    if (!types.empty())
        f << std::format("// Generated from {} road objects across {} types\n", total, types.size());
    else
        f << "// No road objects detected -- skeleton with default types\n";
    f << "\nclass RoadTypesLib {\n";

    if (types.empty()) {
        write_road_type_entry(f, "Road", "");
    } else {
        std::string base_type = types[0];
        for (size_t i = 0; i < types.size(); i++) {
            write_road_type_entry(f, types[i], i > 0 ? base_type : "");
        }
    }
    f << "};\n";

    if (!types.empty()) {
        std::ostringstream os;
        os << "Road objects: " << total << " total";
        for (const auto& t : types) os << ", " << t << "=" << used_types[t];
        armatools::cli::log_plain(os.str());
    }
}

// ============================================================================
// Objects
// ============================================================================

static std::string cat_file_name(const std::string& cat) {
    std::string s = cat;
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}

static std::string tml_library_name(const std::string& category) {
    return "WRP_" + category;
}

// Build a map from full model path to a unique display name (basename with dedup suffix).
// If multiple paths share the same basename, they get _2, _3, etc.
static std::unordered_map<std::string, std::string> build_dedup_names(
        const std::unordered_map<std::string, bool>& model_set,
        const std::unordered_map<std::string, std::string>& case_map) {
    // First pass: collect all basenames and which paths map to them
    std::unordered_map<std::string, std::vector<std::string>> base_to_paths;
    for (const auto& [path, _] : model_set) {
        std::string base = armatools::tb::p3d_base_name(path);
        std::string base_low = base;
        std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Apply case correction if available
        auto cit = case_map.find(base_low);
        if (cit != case_map.end()) base = cit->second;
        base_to_paths[base].push_back(path);
    }

    // Second pass: assign unique names
    std::unordered_map<std::string, std::string> result;
    for (auto& [base, paths] : base_to_paths) {
        std::sort(paths.begin(), paths.end());
        for (size_t i = 0; i < paths.size(); i++) {
            if (i == 0) {
                result[paths[i]] = base;
            } else {
                result[paths[i]] = base + "_" + std::to_string(i + 1);
            }
        }
    }
    return result;
}

static std::vector<std::string> sorted_keys(const std::unordered_map<std::string, bool>& m) {
    std::vector<std::string> keys;
    keys.reserve(m.size());
    for (const auto& [k, _] : m) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    return keys;
}

static armatools::tb::CategoryStyle classify_style(const std::string& cat) {
    std::string low = cat;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    struct Rule { std::string keyword; std::string shape; int32_t fill; };
    static const Rule rules[] = {
        {"tree", "elipse", -16744448}, {"bush", "elipse", -16760832},
        {"plant", "elipse", -16744448}, {"vegetation", "elipse", -16744448},
        {"crop", "elipse", -32768}, {"clutter", "elipse", -8323200},
        {"rock", "elipse", -8355712}, {"road", "rectangle", -8355712},
        {"water", "rectangle", -16776961},
    };
    for (const auto& r : rules) {
        if (low.find(r.keyword) != std::string::npos) {
            return {r.shape, r.fill, -16777216};
        }
    }
    return armatools::tb::default_style();
}

static std::unordered_map<std::string, armatools::tb::CategoryStyle> load_styles(const std::string& path) {
    // Default styles
    std::unordered_map<std::string, armatools::tb::CategoryStyle> base = {
        {"trees", {"elipse", -16744448, -16777216}},
        {"bushes", {"elipse", -16760832, -16777216}},
        {"plants", {"elipse", -16744448, -16777216}},
        {"vegetation", {"elipse", -16744448, -16777216}},
        {"crops", {"elipse", -32768, -16777216}},
        {"clutter", {"elipse", -8323200, -16777216}},
        {"rocks", {"elipse", -8355712, -16777216}},
        {"roads", {"rectangle", -8355712, -16777216}},
        {"water", {"rectangle", -16776961, -16777216}},
    };

    if (path.empty() || !fs::exists(path)) return base;

    std::ifstream f(path);
    if (!f) return base;
    try {
        json j = json::parse(f);
        for (auto& [k, v] : j.items()) {
            armatools::tb::CategoryStyle s;
            if (v.contains("shape")) s.shape = v["shape"].get<std::string>();
            if (v.contains("fill")) s.fill = v["fill"].get<int32_t>();
            if (v.contains("outline")) s.outline = v["outline"].get<int32_t>();
            base[k] = s;
        }
    } catch (...) {}
    return base;
}

static void write_objects_txt_file(const std::string& path,
                                    const std::vector<armatools::wrp::ObjectRecord>& objects,
                                    const ProjectInfo& p) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot create " + path);
    for (const auto& obj : objects) {
        std::string name = armatools::tb::p3d_base_name(obj.model_name);
        f << std::format("\"{}\";{:.6f};{:.6f};{:.6f};{:.6f};{:.6f};{:.6f};{:.6f}\n",
                          name, obj.position[0] + p.offset_x, obj.position[2] + p.offset_z,
                          obj.rotation.yaw, obj.rotation.pitch, obj.rotation.roll,
                          obj.scale, obj.position[1]);
    }
}

static void build_model_meta(const ProjectInfo& p, const std::vector<std::string>& models,
                              std::unordered_map<std::string, armatools::tb::ModelMeta>& meta) {
    if (!p.db_path.empty()) {
        try {
            auto db = armatools::pboindex::DB::open(p.db_path);
            auto bboxes = db.query_model_bboxes();
            for (const auto& model : models) {
                if (meta.count(model)) continue;
                std::string key = armatools::armapath::to_slash_lower(model);
                auto it = bboxes.find(key);
                if (it == bboxes.end()) continue;
                const auto& bb = it->second;
                armatools::tb::ModelMeta m;
                m.height = bb.mi_max[1];
                m.bb_radius = bb.mi_max[2];
                m.bb_hscale = (bb.mi_max[2] != 0) ? bb.mi_max[0] / bb.mi_max[2] : 1.0f;
                if (bb.vis_max[0] != 0 || bb.vis_max[1] != 0 || bb.vis_max[2] != 0) {
                    m.bbox_min = {bb.vis_min[0], bb.vis_min[1], bb.vis_min[2]};
                    m.bbox_max = {bb.vis_max[0], bb.vis_max[1], bb.vis_max[2]};
                    m.bbox_center = {bb.vis_center[0], bb.vis_center[1], bb.vis_center[2]};
                } else {
                    m.bbox_min = {bb.bbox_min[0], bb.bbox_min[1], bb.bbox_min[2]};
                    m.bbox_max = {bb.bbox_max[0], bb.bbox_max[1], bb.bbox_max[2]};
                    m.bbox_center = {bb.bbox_center[0], bb.bbox_center[1], bb.bbox_center[2]};
                }
                meta[model] = m;
            }
        } catch (const std::exception& e) {
            armatools::cli::log_warning("querying model bboxes:", e.what());
        }
    }
    if (meta.size() > 0) {
        armatools::cli::log_plain(std::format("Model metadata: resolved bounding boxes for {}/{} models",
                                               meta.size(), models.size()));
    }
}

void write_objects(ProjectInfo& p) {
    auto& w = *p.world;

    // Apply replacements before categorization
    if (p.replace_map) {
        int replaced = 0;
        for (auto& obj : w.objects) {
            auto [new_name, found] = p.replace_map->lookup(obj.model_name);
            if (found && rmap_to_lower(new_name) != "unmatched") {
                // For multi-match (";"-separated), use the first candidate
                auto semi = new_name.find(';');
                if (semi != std::string::npos) new_name = new_name.substr(0, semi);
                obj.model_name = new_name;
                replaced++;
            }
        }
        for (auto& m : w.models) {
            auto [new_name, found] = p.replace_map->lookup(m);
            if (found && rmap_to_lower(new_name) != "unmatched") {
                auto semi = new_name.find(';');
                if (semi != std::string::npos) new_name = new_name.substr(0, semi);
                m = new_name;
            }
        }
        if (replaced > 0)
            armatools::cli::log_plain(std::format("Replacements: applied {} substitutions ({} rules)",
                                              replaced, p.replace_map->len()));
    }

    std::unordered_map<std::string, std::vector<armatools::wrp::ObjectRecord>> cat_objects;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> cat_model_set;

    for (const auto& obj : w.objects) {
        if (obj.model_name.empty()) continue;
        if (p.road_map->is_road(obj.model_name)) continue;
        std::string cat = armatools::objcat::category(obj.model_name);
        cat_objects[cat].push_back(obj);
        cat_model_set[cat][obj.model_name] = true;
    }

    for (const auto& m : w.models) {
        if (p.road_map->is_road(m)) continue;
        std::string cat = armatools::objcat::category(m);
        cat_model_set[cat][m] = true;
    }

    std::vector<std::string> cats;
    for (const auto& [cat, _] : cat_objects) cats.push_back(cat);
    std::sort(cats.begin(), cats.end());

    // Collect all models for metadata resolution
    std::unordered_map<std::string, bool> all_model_set;
    std::unordered_map<std::string, std::string> model_path_by_base;
    for (const auto& [_, ms] : cat_model_set) {
        for (const auto& [m, __] : ms) {
            all_model_set[m] = true;
            std::string base = armatools::tb::p3d_base_name(m);
            if (!model_path_by_base.count(base)) model_path_by_base[base] = m;
        }
    }
    auto all_models = sorted_keys(all_model_set);
    std::unordered_map<std::string, armatools::tb::ModelMeta> meta;
    build_model_meta(p, all_models, meta);

    // Build case correction map from pboindex DB (lowercase basename -> original-case basename)
    std::unordered_map<std::string, std::string> case_map;
    if (!p.db_path.empty()) {
        try {
            auto db = armatools::pboindex::DB::open(p.db_path);
            auto model_paths = db.query_model_paths();
            for (const auto& [full_path, original_name] : model_paths) {
                // original_name is basename without extension in original case
                std::string lower_name = original_name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                // Only set if not already set (first occurrence wins)
                if (!case_map.count(lower_name))
                    case_map[lower_name] = original_name;
            }
            if (!case_map.empty())
                armatools::cli::log_plain(std::format("Template names: resolved original case for {} model basenames",
                                                      case_map.size()));
        } catch (const std::exception& e) {
            armatools::cli::log_warning("querying model paths for case correction:", e.what());
        }
    }

    // Build per-category dedup name maps (full model path -> unique display name)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> cat_dedup_names;
    int total_dupes = 0;
    for (const auto& cat : cats) {
        auto dedup = build_dedup_names(cat_model_set[cat], case_map);
        // Count duplicates
        for (const auto& [path, name] : dedup) {
            std::string base = armatools::tb::p3d_base_name(path);
            std::string base_low = base;
            std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto cit = case_map.find(base_low);
            std::string corrected = (cit != case_map.end()) ? cit->second : base;
            if (name != corrected) total_dupes++;
        }
        cat_dedup_names[cat] = std::move(dedup);
    }
    if (total_dupes > 0)
        armatools::cli::log_plain(std::format("Template names: {} duplicate basenames resolved with suffixes",
                                               total_dupes));

    auto styles = load_styles(p.style_path);
    for (const auto& cat : cats) {
        if (!styles.count(cat)) styles[cat] = classify_style(cat);
    }

    // Write per-category files and track TML library names
    std::unordered_map<std::string, std::string> cat_lib_names;
    for (const auto& cat : cats) {
        auto models = sorted_keys(cat_model_set[cat]);
        std::string safe = cat_file_name(cat);

        // Write objects txt (with splitting)
        const auto& objs = cat_objects[cat];
        if (p.split_size <= 0 || static_cast<int>(objs.size()) <= p.split_size) {
            write_objects_txt_file((fs::path(p.output_dir) / "source" / ("objects_" + safe + ".txt")).string(), objs, p);
        } else {
            int chunk = 1;
            for (size_t start = 0; start < objs.size(); start += static_cast<size_t>(p.split_size)) {
                size_t end = std::min(start + static_cast<size_t>(p.split_size), objs.size());
                std::string suffix = (chunk > 1) ? std::format("_{}", chunk) : "";
                std::vector<armatools::wrp::ObjectRecord> chunk_objs(objs.begin() + static_cast<long>(start),
                                                                      objs.begin() + static_cast<long>(end));
                write_objects_txt_file(
                    (fs::path(p.output_dir) / "source" / ("objects_" + safe + suffix + ".txt")).string(), chunk_objs, p);
                chunk++;
            }
        }

        // Write TML with dedup name overrides
        std::string tml_path = (fs::path(p.output_dir) / "TemplateLibs" / (safe + ".tml")).string();
        std::ofstream tml(tml_path);
        if (!tml) throw std::runtime_error("cannot create " + tml_path);
        std::string lib_name = tml_library_name(cat);
        const auto* dedup = &cat_dedup_names[cat];
        armatools::tb::write_tml(tml, lib_name, models, &meta, styles[cat], dedup);
        cat_lib_names[cat] = lib_name;
    }

    // Store for tv4p/tv4l — use dedup names for LayerObject.model_name
    p.categories = cats;
    p.cat_lib_names = cat_lib_names;
    p.model_path_by_base = model_path_by_base;
    p.cat_objects.clear();
    for (const auto& cat : cats) {
        auto& lo_vec = p.cat_objects[cat];
        const auto& dedup = cat_dedup_names[cat];
        for (const auto& obj : cat_objects[cat]) {
            LayerObject lo;
            lo.x = obj.position[0] + p.offset_x;
            lo.y = obj.position[2] + p.offset_z;
            lo.z = obj.position[1];
            lo.yaw = obj.rotation.yaw;
            lo.pitch = obj.rotation.pitch;
            lo.roll = obj.rotation.roll;
            lo.scale = obj.scale;
            // Use dedup name if available, otherwise fall back to case-corrected basename
            auto dit = dedup.find(obj.model_name);
            if (dit != dedup.end()) {
                lo.model_name = dit->second;
            } else {
                std::string base = armatools::tb::p3d_base_name(obj.model_name);
                std::string base_low = base;
                std::transform(base_low.begin(), base_low.end(), base_low.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                auto cit = case_map.find(base_low);
                lo.model_name = (cit != case_map.end()) ? cit->second : base;
            }
            lo_vec.push_back(lo);
        }
    }

    for (const auto& cat : cats) {
        armatools::cli::log_plain(std::format("  {}: {} objects, {} models",
                                              cat, cat_objects[cat].size(), cat_model_set[cat].size()));
    }

    // Keep tv4p.mactiveLayer pointing to a real object layer when available.
    // TB-generated projects often use a non-default active layer; if we keep this
    // on empty default, TB may show 0 objects as active/loaded.
    if (!cats.empty() && p.active_layer_ptr == 0) {
        p.active_layer_ptr = p.alloc_ptr();
    }
}

// ============================================================================
// Shapes
// ============================================================================

static std::string road_type_from_id(int id) {
    switch (id) {
    case 1: return "highway";
    case 2: return "asphalt";
    case 3: return "concrete";
    case 4: return "dirt";
    default: return "road";
    }
}

static std::string map_type_from_id(int id) {
    switch (id) {
    case 1: return "main road";
    case 2: return "road";
    case 3: case 4: return "track";
    default: return "road";
    }
}

static double width_from_id(int id) {
    switch (id) {
    case 1: return 14;
    case 2: return 10;
    case 3: return 7;
    case 4: return 8;
    case 5: return 1.6;
    default: return 6;
    }
}

void write_road_shapes(ProjectInfo& p) {
    std::vector<armatools::roadnet::Polyline> polylines;

    if (!p.roads_shp.empty()) {
        // Import from existing shapefile
        auto src = armatools::shp::open(p.roads_shp);
        if (src.records.empty()) return;

        auto base_path = (fs::path(p.output_dir) / "data" / "roads" / "roads").string();
        std::vector<armatools::shp::Field> fields = {
            {"ID", 'N', 4, 0}, {"ORDER", 'N', 4, 0}, {"ROADTYPE", 'C', 20, 0},
            {"WIDTH", 'N', 6, 1}, {"TERRAIN", 'N', 6, 1}, {"MAP", 'C', 20, 0},
            {"SEGMENTS", 'N', 6, 0}, {"LENGTH", 'N', 10, 1},
        };
        auto w = armatools::shp::Writer::create(base_path, armatools::shp::ShapeType::poly_line, fields);

        for (const auto& rec : src.records) {
            for (const auto& part : rec.parts) {
                if (part.size() < 2) continue;
                std::vector<armatools::shp::Point> points(part.begin(), part.end());

                int id = armatools::shp::attr_int(rec.attrs, "ID");
                double width = armatools::shp::attr_float64(rec.attrs, "WIDTH");
                int order = armatools::shp::attr_int(rec.attrs, "ORDER");
                int segments = armatools::shp::attr_int(rec.attrs, "SEGMENTS");
                std::string road_type;
                auto rt = rec.attrs.find("ROADTYPE");
                if (rt != rec.attrs.end()) road_type = rt->second;
                std::string map_type;
                auto mt = rec.attrs.find("MAP");
                if (mt != rec.attrs.end()) map_type = mt->second;

                double length = 0;
                for (size_t i = 1; i < points.size(); i++) {
                    double dx = points[i].x - points[i - 1].x;
                    double dy = points[i].y - points[i - 1].y;
                    length += std::sqrt(dx * dx + dy * dy);
                }

                if (road_type.empty()) road_type = road_type_from_id(id);
                if (map_type.empty()) map_type = map_type_from_id(id);
                if (width == 0) width = width_from_id(id);

                std::vector<armatools::shp::AttrValue> attrs = {
                    static_cast<int64_t>(id), static_cast<int64_t>(order), road_type,
                    width, width + 2, map_type,
                    static_cast<int64_t>(segments), length,
                };
                w->write_poly_line({points}, attrs);
            }
        }
        w->close();
        armatools::cli::log_plain(std::format("Roads: imported {} records from {}",
                                              src.records.size(), fs::path(p.roads_shp).filename().string()));
        return;
    }

    // Extract from WRP
    if (!p.world->road_links.empty()) {
        polylines = armatools::roadnet::extract_from_road_links(p.world->road_links);
    }
    if (polylines.empty() && !p.world->objects.empty()) {
        polylines = armatools::roadnet::extract_from_objects(p.world->objects);
    }
    if (polylines.empty()) return;

    auto base_path = (fs::path(p.output_dir) / "data" / "roads" / "roads").string();
    std::vector<armatools::shp::Field> fields = {
        {"ID", 'N', 4, 0}, {"ORDER", 'N', 4, 0}, {"ROADTYPE", 'C', 20, 0},
        {"WIDTH", 'N', 6, 1}, {"TERRAIN", 'N', 6, 1}, {"MAP", 'C', 20, 0},
        {"SEGMENTS", 'N', 6, 0}, {"LENGTH", 'N', 10, 1},
    };
    auto w = armatools::shp::Writer::create(base_path, armatools::shp::ShapeType::poly_line, fields);

    double total_length = 0;
    std::unordered_map<std::string, int> type_counts;

    for (const auto& pl : polylines) {
        if (pl.points.size() < 2) continue;
        std::vector<armatools::shp::Point> points;
        for (const auto& pt : pl.points)
            points.push_back({pt[0] + p.offset_x, pt[1] + p.offset_z});

        std::vector<armatools::shp::AttrValue> attrs = {
            static_cast<int64_t>(pl.props.id), static_cast<int64_t>(pl.props.order),
            std::string(pl.type), pl.props.width, pl.props.terrain, pl.props.map_type,
            static_cast<int64_t>(pl.seg_count), pl.length,
        };
        w->write_poly_line({points}, attrs);
        total_length += pl.length;
        type_counts[pl.type]++;
    }
    w->close();

    armatools::cli::log_plain(std::format("Roads: {} polylines, {:.0f}m total",
                                          polylines.size(), total_length));
}

void write_forest_shapes(ProjectInfo& p) {
    if (p.world->objects.empty()) return;

    auto polygons = armatools::forestshape::extract_from_objects(p.world->objects);
    if (polygons.empty()) return;

    auto base_path = (fs::path(p.output_dir) / "source" / "forest").string();
    std::vector<armatools::shp::Field> fields = {
        {"ID", 'N', 6, 0}, {"TYPE", 'C', 10, 0}, {"CELLS", 'N', 8, 0}, {"AREA", 'N', 12, 0},
    };
    auto w = armatools::shp::Writer::create(base_path, armatools::shp::ShapeType::polygon, fields);

    double total_area = 0;
    for (const auto& poly : polygons) {
        if (poly.exterior.size() < 4) continue;
        std::vector<std::vector<armatools::shp::Point>> rings;
        auto offset_ring = [&](const std::vector<std::array<double, 2>>& ring) {
            std::vector<armatools::shp::Point> pts;
            for (const auto& pt : ring)
                pts.push_back({pt[0] + p.offset_x, pt[1] + p.offset_z});
            return pts;
        };
        rings.push_back(offset_ring(poly.exterior));
        for (const auto& hole : poly.holes)
            rings.push_back(offset_ring(hole));

        std::vector<armatools::shp::AttrValue> attrs = {
            static_cast<int64_t>(poly.id), std::string(poly.type),
            static_cast<int64_t>(poly.cell_count), static_cast<int64_t>(static_cast<int>(poly.area)),
        };
        w->write_polygon(rings, attrs);
        total_area += poly.area;
    }
    w->close();
    armatools::cli::log_plain(std::format("Forest: {} polygons, {:.2f} km^2",
                                          polygons.size(), total_area / 1e6));
}

// ============================================================================
// Model & texture extraction
// ============================================================================

static bool extract_pbo_entry(const std::string& pbo_path, const std::string& entry_name,
                               const fs::path& dest_path) {
    try {
        std::ifstream pbo_f(pbo_path, std::ios::binary);
        if (!pbo_f) return false;

        auto pbo = armatools::pbo::read(pbo_f);
        // Find the entry by case-insensitive comparison
        std::string entry_lower = armatools::armapath::to_slash_lower(entry_name);
        for (const auto& entry : pbo.entries) {
            std::string ename_lower = armatools::armapath::to_slash_lower(entry.filename);
            if (ename_lower == entry_lower) {
                fs::create_directories(dest_path.parent_path());
                std::ofstream out(dest_path, std::ios::binary);
                if (!out) return false;
                armatools::pbo::extract_file(pbo_f, entry, out);
                return true;
            }
        }
    } catch (...) {}
    return false;
}

static std::string normalize_virtual_rel_path(const std::string& p) {
    std::string s = armatools::armapath::to_slash(p);
    // Drop drive-letter prefix (e.g. "P:/...") so join with drive_root stays relative.
    if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':')
        s.erase(0, 2);
    while (!s.empty() && (s.front() == '/' || s.front() == '\\')) s.erase(0, 1);
    return s;
}

void extract_models(ProjectInfo& p) {
    if (!p.extract_models) return;
    if (p.drive_root.empty()) {
        armatools::cli::log_warning("--extract-models requires --drive, skipping");
        return;
    }
    if (p.db_path.empty()) {
        armatools::cli::log_warning("--extract-models requires --db, skipping");
        return;
    }

    // Open DB and build index
    auto db = armatools::pboindex::DB::open(p.db_path);
    auto idx = db.index();

    // Collect all unique model paths from WRP
    std::unordered_map<std::string, bool> model_set;
    for (const auto& obj : p.world->objects) {
        if (!obj.model_name.empty())
            model_set[obj.model_name] = true;
    }
    for (const auto& m : p.world->models) {
        if (!m.empty())
            model_set[m] = true;
    }

    std::vector<std::string> models;
    models.reserve(model_set.size());
    for (const auto& [m, _] : model_set) models.push_back(m);
    std::sort(models.begin(), models.end());

    // Query textures for all models
    auto model_textures = db.query_model_textures(models);

    // Collect unique texture paths
    std::unordered_map<std::string, bool> texture_set;
    for (const auto& [model, textures] : model_textures) {
        for (const auto& tex : textures)
            texture_set[tex] = true;
    }

    int models_extracted = 0, models_skipped = 0, models_failed = 0;
    int textures_extracted = 0, textures_skipped = 0, textures_failed = 0;

    // Extract models
    for (size_t i = 0; i < models.size(); i++) {
        const auto& model_path = models[i];
        std::string norm = armatools::armapath::to_slash_lower(model_path);
        std::string rel_norm = normalize_virtual_rel_path(norm);

        // Destination on drive
        fs::path dest = fs::path(p.drive_root) / armatools::armapath::to_os(rel_norm);
        if (fs::exists(dest)) {
            models_skipped++;
            continue;
        }

        armatools::pboindex::ResolveResult rr;
        if (!idx.resolve(model_path, rr)) {
            armatools::cli::log_warning("cannot find PBO for", model_path);
            models_failed++;
            continue;
        }

        if (extract_pbo_entry(rr.pbo_path, rr.entry_name, dest)) {
            models_extracted++;
        } else {
            armatools::cli::log_warning("failed to extract", model_path);
            models_failed++;
        }

        if ((models_extracted + models_skipped + models_failed) % 50 == 0) {
            armatools::cli::log_raw(
                std::format("\rExtracting models: {}/{} (skipped {} existing)",
                            models_extracted + models_skipped + models_failed,
                            models.size(), models_skipped));
        }
    }
    if (!models.empty())
        armatools::cli::log_plain(std::format("\rExtracting models: {}/{} (skipped {} existing, {} failed)",
                                          models_extracted + models_skipped, models.size(),
                                          models_skipped, models_failed));

    // Extract textures
    std::vector<std::string> textures;
    textures.reserve(texture_set.size());
    for (const auto& [t, _] : texture_set) textures.push_back(t);
    std::sort(textures.begin(), textures.end());

    for (size_t i = 0; i < textures.size(); i++) {
        const auto& tex_path = textures[i];
        std::string rel_tex = normalize_virtual_rel_path(tex_path);

        fs::path dest = fs::path(p.drive_root) / armatools::armapath::to_os(rel_tex);
        if (fs::exists(dest)) {
            textures_skipped++;
            continue;
        }

        armatools::pboindex::ResolveResult rr;
        if (!idx.resolve(tex_path, rr)) {
            textures_failed++;
            continue;
        }

        if (extract_pbo_entry(rr.pbo_path, rr.entry_name, dest)) {
            textures_extracted++;
        } else {
            textures_failed++;
        }

        if ((textures_extracted + textures_skipped + textures_failed) % 100 == 0) {
            armatools::cli::log_raw(
                std::format("\rExtracting textures: {}/{} (skipped {} existing)",
                            textures_extracted + textures_skipped + textures_failed,
                            textures.size(), textures_skipped));
        }
    }
    if (!textures.empty())
        armatools::cli::log_plain(std::format("\rExtracting textures: {}/{} (skipped {} existing, {} failed)",
                                          textures_extracted + textures_skipped, textures.size(),
                                          textures_skipped, textures_failed));
}
