#include "armatools/wrp.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static double round3(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    return std::round(v * 1000) / 1000;
}

static void write_json_file(const std::string& path, const json& doc, bool pretty) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("creating " + path);
    if (pretty) f << std::setw(2) << doc << '\n';
    else f << doc << '\n';
}

static json build_world_json(const armatools::wrp::WorldData& w) {
    json textures = json::array();
    for (size_t i = 0; i < w.textures.size(); i++) {
        json t = {{"index", static_cast<int>(i)}, {"filename", w.textures[i].filename}};
        if (w.textures[i].color != 0) t["color"] = w.textures[i].color;
        textures.push_back(std::move(t));
    }

    json models = json::array();
    for (size_t i = 0; i < w.models.size(); i++) {
        models.push_back({{"index", static_cast<int>(i)}, {"filename", w.models[i]}});
    }

    json warnings = json::array();
    for (const auto& wn : w.warnings) {
        json wj = {{"code", wn.code}, {"message", wn.message}};
        warnings.push_back(std::move(wj));
    }

    json peaks = json::array();
    for (const auto& p : w.peaks) {
        peaks.push_back({p[0], p[1], p[2]});
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
        {"textures", textures},
        {"models", models},
        {"peaks", peaks},
        {"warnings", warnings},
    };
}

static void write_elevations_json(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    json doc = {
        {"width", w.grid.cells_x},
        {"height", w.grid.cells_y},
        {"cellSize", w.grid.cell_size},
        {"unit", "meters"},
        {"data", w.elevations},
    };
    write_json_file((fs::path(dir) / "elevations.json").string(), doc, pretty);
}

static void write_cells_json(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    json doc = {
        {"width", w.grid.cells_x},
        {"height", w.grid.cells_y},
        {"bitFlags", w.cell_bit_flags.empty() ? json::array() : json(w.cell_bit_flags)},
        {"envSounds", w.cell_env_sounds.empty() ? json::array() : json(w.cell_env_sounds)},
        {"textureIndexes", w.cell_texture_indexes.empty() ? json::array() : json(w.cell_texture_indexes)},
    };
    write_json_file((fs::path(dir) / "cells.json").string(), doc, pretty);
}

static void write_objects_jsonl(const armatools::wrp::WorldData& w, const std::string& dir, bool pretty) {
    std::ofstream f(fs::path(dir) / "objects.jsonl");
    if (!f) throw std::runtime_error("creating objects.jsonl");

    for (const auto& obj : w.objects) {
        json transform = json::array();
        for (float v : obj.transform) transform.push_back(v);

        json rec = {
            {"modelIndex", obj.model_index},
            {"modelName", obj.model_name},
            {"transform", transform},
            {"pos", {round3(obj.position[0]), round3(obj.position[1]), round3(obj.position[2])}},
            {"rot", {{"yaw", round3(obj.rotation.yaw)}, {"pitch", round3(obj.rotation.pitch)}, {"roll", round3(obj.rotation.roll)}}},
            {"scale", round3(obj.scale)},
        };
        if (obj.object_id != 0) rec["objectID"] = obj.object_id;

        if (pretty) f << std::setw(2) << rec << '\n';
        else f << rec << '\n';
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

struct DumpOptions {
    bool pretty = false;
    bool no_cells = false;
    bool no_objects = false;
    bool no_elevations = false;
};

static void write_outputs(const armatools::wrp::WorldData& w, const std::string& dir, DumpOptions opts) {
    fs::create_directories(dir);

    auto doc = build_world_json(w);
    write_json_file((fs::path(dir) / "world.json").string(), doc, opts.pretty);

    if (!opts.no_elevations && !w.elevations.empty()) {
        write_elevations_json(w, dir, opts.pretty);
    }

    if (!opts.no_cells && (!w.cell_bit_flags.empty() || !w.cell_env_sounds.empty() || !w.cell_texture_indexes.empty())) {
        write_cells_json(w, dir, opts.pretty);
    }

    if (!opts.no_objects && !w.objects.empty()) {
        write_objects_jsonl(w, dir, opts.pretty);
        write_classes_json(w, dir, opts.pretty);
    }

    if (!w.roads.empty()) {
        write_roads_geojson(w, dir, opts.pretty);
    }
}

static void print_usage() {
    std::cerr << "Usage: wrp_dump [flags] <input.wrp> [output_dir]\n\n"
              << "Full dump of OFP/Resistance WRP files to structured JSON.\n\n"
              << "Output files:\n"
              << "  world.json       - Complete metadata\n"
              << "  elevations.json  - Full height grid in meters\n"
              << "  cells.json       - Per-cell data\n"
              << "  objects.jsonl    - One JSON object per line\n"
              << "  classes.json     - Unique classes with counts\n"
              << "  roads.geojson    - Road networks (1WVR only)\n\n"
              << "Flags:\n"
              << "  --pretty          Pretty-print JSON output\n"
              << "  --json            Write world.json to stdout instead of files\n"
              << "  --no-cells        Skip cells.json\n"
              << "  --no-objects      Skip objects.jsonl and classes.json\n"
              << "  --no-elevations   Skip elevations.json\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool json_stdout = false;
    bool no_cells = false;
    bool no_objects = false;
    bool no_elevations = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--json") == 0) json_stdout = true;
        else if (std::strcmp(argv[i], "--no-cells") == 0) no_cells = true;
        else if (std::strcmp(argv[i], "--no-objects") == 0) no_objects = true;
        else if (std::strcmp(argv[i], "--no-elevations") == 0) no_elevations = true;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

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
        output_dir = (fs::path(input_path).parent_path() / (base + "_dump")).string();
    }

    if (output_dir == "-") json_stdout = true;

    std::ifstream f(input_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error: cannot open " << input_path << '\n';
        return 1;
    }

    armatools::wrp::Options opts{.no_objects = no_objects || json_stdout};

    armatools::wrp::WorldData world;
    try {
        world = armatools::wrp::read(f, opts);
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << input_path << ": " << e.what() << '\n';
        return 1;
    }

    if (json_stdout) {
        auto doc = build_world_json(world);
        if (pretty) std::cout << std::setw(2) << doc << '\n';
        else std::cout << doc << '\n';
        return 0;
    }

    try {
        write_outputs(world, output_dir, {pretty, no_cells, no_objects, no_elevations});
    } catch (const std::exception& e) {
        std::cerr << "Error: writing output: " << e.what() << '\n';
        return 1;
    }

    // Summary
    std::cerr << "Dumped: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
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
    std::cerr << "Output: " << output_dir << '\n';
    return 0;
}
