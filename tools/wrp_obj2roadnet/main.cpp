#include "armatools/wrp.h"
#include "armatools/roadnet.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>

using json = nlohmann::ordered_json;

static double round_n(double v, int decimals) {
    double p = 1.0;
    for (int i = 0; i < decimals; i++) p *= 10;
    return std::floor(v * p + 0.5) / p;
}

static void write_geojson(std::ostream& w, const std::vector<armatools::roadnet::Polyline>& polylines,
                           double offset_x, double offset_z, bool pretty) {
    const auto& props_map = armatools::roadnet::ofp_road_props();

    json features = json::array();
    for (const auto& pl : polylines) {
        if (pl.points.size() < 2) continue;

        json coords = json::array();
        for (const auto& pt : pl.points) {
            coords.push_back({round_n(pt[0] + offset_x, 2), round_n(pt[1] + offset_z, 2)});
        }

        json props_json = json::object();
        auto it = props_map.find(pl.type);
        if (it != props_map.end()) {
            props_json["ID"] = it->second.id;
            props_json["ORDER"] = it->second.order;
            props_json["WIDTH"] = it->second.width;
            props_json["TERRAIN"] = it->second.terrain;
            props_json["MAP"] = it->second.map_type;
        }
        props_json["ROADTYPE"] = pl.type;
        props_json["SEGMENTS"] = pl.seg_count;
        props_json["LENGTH"] = round_n(pl.length, 1);

        features.push_back({
            {"type", "Feature"},
            {"properties", props_json},
            {"geometry", {{"type", "LineString"}, {"coordinates", coords}}},
        });
    }

    json fc = {{"type", "FeatureCollection"}, {"features", features}};
    if (pretty) w << std::setw(2) << fc << '\n';
    else w << fc << '\n';
}

static void print_usage() {
    std::cerr << "Usage: wrp_obj2roadnet [flags] <input.wrp> <output.geojson>\n\n"
              << "Extracts the road network from placed road segment objects in a WRP file\n"
              << "and outputs a GeoJSON FeatureCollection with Arma 3 SHP-compatible attributes.\n\n"
              << "Output attributes (DBF-compatible, <=10 chars):\n"
              << "  ID        roadslib.cfg road type ID\n"
              << "  ORDER     rendering z-order (lower = on top)\n"
              << "  ROADTYPE  OFP surface type name\n"
              << "  WIDTH     road width in meters\n"
              << "  TERRAIN   terrain integration range\n"
              << "  MAP       Arma 3 map type\n\n"
              << "Convert to SHP: ogr2ogr -f \"ESRI Shapefile\" roads.shp output.geojson\n\n"
              << "Flags:\n"
              << "  --pretty          Pretty-print GeoJSON output\n"
              << "  -offset-x <n>    X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>    Z coordinate offset (default: 0)\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    double offset_x = 200000;
    double offset_z = 0;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) offset_x = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() < 2) {
        print_usage();
        return 1;
    }

    std::string input_path = positional[0];
    std::string output_path = positional[1];

    std::ifstream f(input_path, std::ios::binary);
    if (!f) {
        std::cerr << "Error: cannot open " << input_path << '\n';
        return 1;
    }

    armatools::wrp::WorldData world;
    try {
        world = armatools::wrp::read(f, {});
    } catch (const std::exception& e) {
        std::cerr << "Error: parsing " << input_path << ": " << e.what() << '\n';
        return 1;
    }

    if (world.objects.empty()) {
        std::cerr << "Error: no objects in " << input_path << '\n';
        return 1;
    }

    auto polylines = armatools::roadnet::extract_from_objects(world.objects);

    if (polylines.empty()) {
        std::cerr << "Error: no road segments found in " << input_path << '\n';
        return 1;
    }

    // Collect stats
    std::unordered_map<std::string, int> type_counts;
    std::unordered_map<std::string, double> type_lengths;
    double total_length = 0;
    for (const auto& pl : polylines) {
        type_counts[pl.type]++;
        type_lengths[pl.type] += pl.length;
        total_length += pl.length;
    }

    // Write output
    std::ostream* out = nullptr;
    std::ofstream out_file;
    if (output_path == "-") {
        out = &std::cout;
    } else {
        out_file.open(output_path);
        if (!out_file) {
            std::cerr << "Error: cannot create " << output_path << '\n';
            return 1;
        }
        out = &out_file;
    }

    write_geojson(*out, polylines, offset_x, offset_z, pretty);

    // Stats to stderr
    std::cerr << "Source: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << "Polylines: " << polylines.size() << '\n';
    for (const auto& rt : armatools::roadnet::ofp_type_order) {
        if (type_counts.count(rt) && type_counts[rt] > 0) {
            std::cerr << std::format("  {}: {} polylines, {:.0f}m total\n", rt, type_counts[rt], type_lengths[rt]);
        }
    }
    std::cerr << std::format("Total road length: {:.0f}m ({:.1f}km)\n", total_length, total_length / 1000);
    if (offset_x != 0 || offset_z != 0) {
        std::cerr << std::format("Coordinate offset: X+{:.0f} Z+{:.0f}\n", offset_x, offset_z);
    }
    if (output_path != "-") {
        std::cerr << "Output: " << output_path << '\n';
    }

    return 0;
}
