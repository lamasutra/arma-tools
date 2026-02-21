#include "armatools/wrp.h"
#include "armatools/shp.h"
#include "armatools/config.h"
#include "armatools/tb.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "../common/cli_logger.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static double round3(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    return std::round(v * 1000) / 1000;
}

static json build_world_json(const armatools::wrp::WorldData& w) {
    json warnings = json::array();
    for (const auto& wn : w.warnings) {
        json wj = {{"code", wn.code}, {"message", wn.message}};
        warnings.push_back(std::move(wj));
    }

    json stats = {
        {"textureCount", w.stats.texture_count},
        {"modelCount", w.stats.model_count},
        {"objectCount", w.stats.object_count},
        {"peakCount", w.stats.peak_count},
        {"roadNetCount", w.stats.road_net_count},
    };
    if (w.stats.has_cell_flags) {
        stats["cellFlags"] = {
            {"forestCells", w.stats.cell_flags.forest_cells},
            {"roadwayCells", w.stats.cell_flags.roadway_cells},
            {"totalCells", w.stats.cell_flags.total_cells},
            {"surface", {
                {"ground", w.stats.cell_flags.surface.ground},
                {"tidal", w.stats.cell_flags.surface.tidal},
                {"coastline", w.stats.cell_flags.surface.coastline},
                {"sea", w.stats.cell_flags.surface.sea},
            }},
        };
    }

    return {
        {"schemaVersion", 1},
        {"format", {{"signature", w.format.signature}, {"version", w.format.version}}},
        {"grid", {{"cellsX", w.grid.cells_x}, {"cellsY", w.grid.cells_y},
                  {"cellSize", w.grid.cell_size}, {"terrainX", w.grid.terrain_x}, {"terrainY", w.grid.terrain_y}}},
        {"bounds", {{"minElevation", w.bounds.min_elevation}, {"maxElevation", w.bounds.max_elevation},
                    {"worldSizeX", w.bounds.world_size_x}, {"worldSizeY", w.bounds.world_size_y}}},
        {"stats", stats},
        {"warnings", warnings},
    };
}

static void write_json_file(const std::string& path, const json& doc, bool pretty) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("creating " + path);
    if (pretty) f << std::setw(2) << doc << '\n';
    else f << doc << '\n';
}

static void write_objects_jsonl(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    std::ofstream f(fs::path(dir) / "objects.jsonl");
    if (!f) throw std::runtime_error("creating objects.jsonl");

    for (const auto& obj : w.objects) {
        json rec = {
            {"sourceClass", obj.model_name},
            {"pos", {round3(obj.position[0]), round3(obj.position[1]), round3(obj.position[2])}},
            {"rot", {{"yaw", round3(obj.rotation.yaw)}, {"pitch", round3(obj.rotation.pitch)}, {"roll", round3(obj.rotation.roll)}}},
            {"scale", round3(obj.scale)},
            {"meta", json::object()},
        };
        if (obj.object_id != 0) rec["meta"]["id"] = obj.object_id;
        rec["meta"]["modelIndex"] = obj.model_index;

        if (pretty) f << std::setw(2) << rec << '\n';
        else f << rec << '\n';
    }
}

static void write_objects_tb(const armatools::wrp::WorldData& w, const std::string& dir,
                              double offset_x, double offset_z) {
    std::ofstream f(fs::path(dir) / "objects.txt");
    if (!f) throw std::runtime_error("creating objects.txt");

    for (const auto& obj : w.objects) {
        double x = obj.position[0] + offset_x;
        double y = obj.position[2] + offset_z;
        double z = obj.position[1];
        f << std::format("\"{}\" {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f}\n",
                         obj.model_name, x, y, z,
                         obj.rotation.yaw, obj.rotation.pitch, obj.rotation.roll,
                         obj.scale, obj.scale, obj.scale);
    }
}

static void write_classes_json(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    struct Acc { int count = 0; double sum_pos[3] = {0, 0, 0}; };
    std::unordered_map<std::string, Acc> classes;
    for (const auto& obj : w.objects) {
        auto& acc = classes[obj.model_name];
        acc.count++;
        acc.sum_pos[0] += obj.position[0];
        acc.sum_pos[1] += obj.position[1];
        acc.sum_pos[2] += obj.position[2];
    }

    json entries = json::array();
    for (auto& [name, acc] : classes) {
        double n = static_cast<double>(acc.count);
        entries.push_back({
            {"sourceClass", name},
            {"count", acc.count},
            {"centroid", {std::round(acc.sum_pos[0] / n * 100) / 100,
                          std::round(acc.sum_pos[1] / n * 100) / 100,
                          std::round(acc.sum_pos[2] / n * 100) / 100}},
        });
    }

    std::sort(entries.begin(), entries.end(), [](const json& a, const json& b) {
        if (a["count"] != b["count"]) return a["count"].get<int>() > b["count"].get<int>();
        return a["sourceClass"].get<std::string>() < b["sourceClass"].get<std::string>();
    });

    json doc = {{"schemaVersion", 1}, {"classes", entries}};
    write_json_file((fs::path(dir) / "classes.json").string(), doc, pretty);
}

static void write_tml(const armatools::wrp::WorldData& w, const std::string& dir) {
    std::set<std::string> seen;
    std::vector<std::string> models;
    for (const auto& obj : w.objects) {
        if (seen.insert(obj.model_name).second) models.push_back(obj.model_name);
    }
    for (const auto& m : w.models) {
        if (seen.insert(m).second) models.push_back(m);
    }
    std::sort(models.begin(), models.end());

    std::ofstream f(fs::path(dir) / "objects.tml");
    if (!f) throw std::runtime_error("creating objects.tml");
    armatools::tb::write_tml(f, "WRP_Objects", models, nullptr, armatools::tb::default_style());
}

static void write_roads_geojson(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    json features = json::array();
    for (const auto& net : w.roads) {
        if (net.subnets.empty()) continue;
        json coords = json::array();
        for (const auto& sn : net.subnets) {
            coords.push_back({sn.x, sn.y});
        }
        features.push_back({
            {"type", "Feature"},
            {"properties", {{"name", net.name}, {"type", net.type}, {"scale", net.scale}}},
            {"geometry", {{"type", "LineString"}, {"coordinates", coords}}},
        });
    }
    json fc = {{"type", "FeatureCollection"}, {"features", features}};
    write_json_file((fs::path(dir) / "roads.geojson").string(), fc, pretty);
}

static void write_outputs(const armatools::wrp::WorldData& w, const std::string& dir,
                           bool pretty, double offset_x, double offset_z) {
    fs::create_directories(dir);

    auto doc = build_world_json(w);
    write_json_file((fs::path(dir) / "world.json").string(), doc, pretty);

    if (!w.objects.empty()) {
        write_objects_jsonl(w, dir, pretty);
        write_objects_tb(w, dir, offset_x, offset_z);
    }

    write_classes_json(w, dir, pretty);
    write_tml(w, dir);

    if (!w.roads.empty()) {
        write_roads_geojson(w, dir, pretty);
    }
}

// Config detection helpers
static std::string find_config_cpp(const std::string& wrp_path) {
    fs::path dir = fs::path(wrp_path).parent_path();
    for (const auto& d : {dir, dir.parent_path()}) {
        auto p = d / "config.cpp";
        if (fs::exists(p)) return p.string();
    }
    return "";
}

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string parse_new_roads_shape(const std::string& config_path) {
    // The text parser is a stub, so this won't work yet.
    // Keep it for when the text parser is implemented.
    std::ifstream f(config_path);
    if (!f) return "";
    try {
        auto cfg = armatools::config::parse_text(f);
        // Search for CfgWorlds -> concrete class -> newRoadsShape
        for (const auto& ne : cfg.root.entries) {
            if (to_lower(ne.name) != "cfgworlds") continue;
            if (auto* ce = std::get_if<armatools::config::ClassEntryOwned>(&ne.entry)) {
                for (const auto& we : ce->cls->entries) {
                    if (auto* wce = std::get_if<armatools::config::ClassEntryOwned>(&we.entry)) {
                        if (wce->cls->external || wce->cls->deletion) continue;
                        for (const auto& e : wce->cls->entries) {
                            if (to_lower(e.name) == "newroadsshape") {
                                if (auto* se = std::get_if<armatools::config::StringEntry>(&e.entry)) {
                                    if (!se->value.empty()) return se->value;
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {}
    return "";
}

static std::string resolve_roads_shp_near(const std::string& base_dir, const std::string& new_roads_shape) {
    std::string p = new_roads_shape;
    // Trim leading backslashes
    while (!p.empty() && p[0] == '\\') p.erase(0, 1);
    std::replace(p.begin(), p.end(), '\\', '/');

    std::vector<std::string> parts;
    std::istringstream ss(p);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }

    for (const auto& dir : {base_dir, fs::path(base_dir).parent_path().string()}) {
        for (size_t i = 0; i < parts.size(); i++) {
            fs::path candidate = dir;
            for (size_t j = i; j < parts.size(); j++) candidate /= parts[j];
            if (fs::exists(candidate)) return candidate.string();
        }
    }
    return "";
}

static void print_usage() {
    std::cerr << "Usage: wrp_info [flags] <input.wrp> [output_dir]\n\n"
              << "Parses OFP/Resistance WRP files and outputs structured JSON.\n\n"
              << "Output files:\n"
              << "  world.json    - World metadata (format, grid, bounds, stats)\n"
              << "  objects.jsonl - One JSON object per line for each placed object\n"
              << "  objects.txt   - Terrain Builder text import format\n"
              << "  classes.json  - Summary of unique classes with counts\n"
              << "  roads.geojson - Road networks (1WVR only)\n\n"
              << "Flags:\n"
              << "  --pretty       Pretty-print JSON output\n"
              << "  --json         Write world.json to stdout instead of files\n"
              << "  --strict       Fail on unexpected data\n"
              << "  --no-objects   Skip objects output (faster)\n"
              << "  -v, --verbose  Enable verbose logging\n"
              << "  -vv, --debug   Enable debug logging and diagnostics\n"
              << "  -offset-x <n>  X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>  Z coordinate offset (default: 0)\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    bool strict = false;
    bool no_objects = false;
    double offset_x = 200000;
    double offset_z = 0;
    int verbosity = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--json") == 0) json_stdout = true;
        else if (std::strcmp(argv[i], "--strict") == 0) strict = true;
        else if (std::strcmp(argv[i], "--no-objects") == 0) no_objects = true;
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) offset_x = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0)
            verbosity = std::min(verbosity + 1, 2);
        else if (std::strcmp(argv[i], "-vv") == 0 || std::strcmp(argv[i], "--debug") == 0)
            verbosity = 2;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    armatools::cli::set_verbosity(verbosity);

    if (positional.empty()) {
        print_usage();
        return 1;
    }

    std::string input_path = positional[0];
    std::string output_dir;
    if (positional.size() >= 2) {
        output_dir = positional[1];
    } else {
        std::string base = fs::path(input_path).stem().string();
        output_dir = (fs::path(input_path).parent_path() / (base + "_info")).string();
    }

    if (output_dir == "-") json_stdout = true;

    armatools::cli::log_verbose("Reading", input_path);
    if (armatools::cli::debug_enabled()) {
        try {
            armatools::cli::log_debug("Input size (bytes):", fs::file_size(input_path));
        } catch (const std::exception&) {
            armatools::cli::log_debug("Input size unavailable for", input_path);
        }
    }

    std::ifstream f(input_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error: cannot open " << input_path << '\n';
        return 1;
    }

    armatools::wrp::Options opts{.strict = strict, .no_objects = no_objects || json_stdout};

    armatools::wrp::WorldData world;
    try {
        world = armatools::wrp::read(f, opts);
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << input_path << ": " << e.what() << '\n';
        return 1;
    }

    if (json_stdout) {
        auto doc = build_world_json(world);
        armatools::cli::log_verbose("Writing JSON to stdout");
        if (pretty) std::cout << std::setw(2) << doc << '\n';
        else std::cout << doc << '\n';
        return 0;
    }

    try {
        armatools::cli::log_verbose("Writing outputs to", output_dir);
        write_outputs(world, output_dir, pretty, offset_x, offset_z);
    } catch (const std::exception& e) {
        std::cerr << "Error: writing output: " << e.what() << '\n';
        return 1;
    }

    if (armatools::cli::verbose_enabled()) {
        armatools::cli::log_verbose("Textures:", world.stats.texture_count,
                                     "Models:", world.stats.model_count,
                                     "Objects:", world.stats.object_count);
    }
    if (armatools::cli::debug_enabled()) {
        armatools::cli::log_debug("Road nets:", world.stats.road_net_count,
                                  "Warnings:", world.warnings.size());
        if (!world.warnings.empty()) {
            for (const auto& warning : world.warnings) {
                armatools::cli::log_debug("Warning", warning.code, warning.message);
            }
        }
    }

    // Summary
    std::cerr << "Parsed: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << std::format("Grid: {}x{} cells ({:.0f}m cell size)\n",
                             world.grid.cells_x, world.grid.cells_y, world.grid.cell_size);
    std::cerr << std::format("World: {:.0f}x{:.0f}m, elevation {:.1f}..{:.1f}m\n",
                             world.bounds.world_size_x, world.bounds.world_size_y,
                             world.bounds.min_elevation, world.bounds.max_elevation);
    std::cerr << "Textures: " << world.stats.texture_count << ", Models: " << world.stats.model_count
              << ", Objects: " << world.stats.object_count << '\n';
    if (world.stats.road_net_count > 0) {
        std::cerr << "Road nets: " << world.stats.road_net_count << '\n';
    }
    if (!world.warnings.empty()) {
        std::cerr << "Warnings: " << world.warnings.size() << '\n';
        for (const auto& w : world.warnings) {
            std::cerr << "  [" << w.code << "] " << w.message << '\n';
        }
    }

    if (auto config_path = find_config_cpp(input_path); !config_path.empty()) {
        std::cerr << "Config: " << config_path << " (auto-detected)\n";
        if (auto nrs = parse_new_roads_shape(config_path); !nrs.empty()) {
            auto wrp_dir = fs::path(input_path).parent_path().string();
            if (auto shp_path = resolve_roads_shp_near(wrp_dir, nrs); !shp_path.empty()) {
                std::cerr << "Roads shape detected: " << shp_path << '\n';
                try {
                    auto bbox = armatools::shp::read_bbox(shp_path);
                    std::cerr << std::format("  BBox: X=[{:.0f}, {:.0f}] Y=[{:.0f}, {:.0f}]\n",
                                             bbox.x_min, bbox.x_max, bbox.y_min, bbox.y_max);
                    double map_size_x = world.bounds.world_size_x;
                    if (map_size_x > 0 && bbox.x_min > map_size_x) {
                        double detected_offset = std::floor((bbox.x_max - map_size_x) / 1000) * 1000;
                        std::cerr << std::format("  Offset: X={:.0f} (map size {:.0f})\n", detected_offset, map_size_x);
                    }
                } catch (...) {}
            }
        }
    }

    std::cerr << "Output: " << output_dir << '\n';
    return 0;
}
