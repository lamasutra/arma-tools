#include "armatools/wrp.h"
#include "armatools/forestshape.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <string>

using json = nlohmann::ordered_json;

static double round_n(double v, int decimals) {
    double p = 1.0;
    for (int i = 0; i < decimals; i++) p *= 10;
    return std::floor(v * p + 0.5) / p;
}

static json offset_ring(const std::vector<std::array<double, 2>>& ring, double offset_x, double offset_z) {
    json coords = json::array();
    for (const auto& pt : ring) {
        coords.push_back({round_n(pt[0] + offset_x, 2), round_n(pt[1] + offset_z, 2)});
    }
    return coords;
}

static void write_geojson(std::ostream& w, const std::vector<armatools::forestshape::Polygon>& polygons,
                           double offset_x, double offset_z, bool pretty) {
    json features = json::array();
    for (const auto& poly : polygons) {
        if (poly.exterior.size() < 4) continue;

        json rings = json::array();
        rings.push_back(offset_ring(poly.exterior, offset_x, offset_z));
        for (const auto& hole : poly.holes) {
            rings.push_back(offset_ring(hole, offset_x, offset_z));
        }

        json props = {
            {"ID", poly.id},
            {"TYPE", poly.type},
            {"CELLS", poly.cell_count},
            {"AREA", round_n(poly.area, 0)},
        };

        features.push_back({
            {"type", "Feature"},
            {"properties", props},
            {"geometry", {{"type", "Polygon"}, {"coordinates", rings}}},
        });
    }

    json fc = {{"type", "FeatureCollection"}, {"features", features}};
    if (pretty) w << std::setw(2) << fc << '\n';
    else w << fc << '\n';
}

static void print_usage() {
    std::cerr << "Usage: wrp_obj2forestshape [flags] <input.wrp> <output.geojson>\n\n"
              << "Extracts forest area polygons from OFP forest block objects and outputs\n"
              << "a GeoJSON FeatureCollection for Terrain Processor.\n\n"
              << "OFP forest blocks (les ctverec, les trojuhelnik) are placed on a 50m grid.\n"
              << "Adjacent cells are merged into contiguous forest polygons.\n\n"
              << "Output attributes (DBF-compatible, <=10 chars):\n"
              << "  ID        sequential polygon ID\n"
              << "  TYPE      forest type (mixed, conifer)\n"
              << "  CELLS     number of 50m grid cells\n"
              << "  AREA      approximate area in m^2\n\n"
              << "Convert to SHP: ogr2ogr -f \"ESRI Shapefile\" forest.shp output.geojson\n\n"
              << "Flags:\n"
              << "  --pretty          Pretty-print GeoJSON output\n"
              << "  -offset-x <n>    X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>    Z coordinate offset (default: 0)\n"
              << "  -index <n>       Export only polygon at this 0-based index (default: all)\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    double offset_x = 200000;
    double offset_z = 0;
    int shape_index = -1;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) offset_x = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-index") == 0 && i + 1 < argc) shape_index = std::stoi(argv[++i]);
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

    auto polygons = armatools::forestshape::extract_from_objects(world.objects);

    if (polygons.empty()) {
        std::cerr << "Error: no forest objects found in " << input_path << '\n';
        return 1;
    }

    // Sort by area descending and assign IDs
    std::sort(polygons.begin(), polygons.end(), [](const auto& a, const auto& b) {
        return a.area > b.area;
    });
    for (size_t i = 0; i < polygons.size(); i++) {
        polygons[i].id = static_cast<int>(i) + 1;
    }

    std::cerr << "Source: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << "Polygons: " << polygons.size() << " forest areas\n";

    double total_area = 0;
    for (const auto& p : polygons) total_area += p.area;
    std::cerr << std::format("Total forest area: {:.2f} km^2 ({:.0f} m^2)\n", total_area / 1e6, total_area);

    // Filter to single polygon by index
    if (shape_index >= 0) {
        if (shape_index >= static_cast<int>(polygons.size())) {
            std::cerr << std::format("Error: index {} out of range (0..{})\n", shape_index, polygons.size() - 1);
            return 1;
        }
        auto si = static_cast<size_t>(shape_index);
        std::cerr << std::format("Exporting shape index {} (ID={}, type={}, cells={}, area={:.0f} m^2)\n",
                                  shape_index, polygons[si].id, polygons[si].type,
                                  polygons[si].cell_count, polygons[si].area);
        polygons = {polygons[si]};
    }

    if (offset_x != 0 || offset_z != 0) {
        std::cerr << std::format("Coordinate offset: X+{:.0f} Z+{:.0f}\n", offset_x, offset_z);
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

    write_geojson(*out, polygons, offset_x, offset_z, pretty);

    if (output_path != "-") {
        std::cerr << "Output: " << output_path << '\n';
    }

    return 0;
}
