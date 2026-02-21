#include "armatools/wrp.h"
#include "armatools/roadobj.h"
#include "armatools/tb.h"
#include "../wrp2project/replacement_map.h"

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
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}


// --- Stats ---

struct MappingEntry { std::string from, to; int count = 0; };
struct UnmappedEntry { std::string source_class; int count = 0; };

struct ReplacementStats {
    int total_objects = 0;
    int skipped_roads = 0;
    int replaced_objects = 0;
    int kept_objects = 0;
    int replacement_rules = 0;
    std::vector<MappingEntry> mappings;
    std::vector<UnmappedEntry> unmapped;
};

static ReplacementStats compute_stats(const std::vector<armatools::wrp::ObjectRecord>& objects,
                                       const ReplacementMap& rmap) {
    struct MK { std::string from, to; bool operator==(const MK& o) const { return from == o.from && to == o.to; } };
    struct MKHash { size_t operator()(const MK& k) const { return std::hash<std::string>()(k.from) ^ std::hash<std::string>()(k.to); }};
    std::unordered_map<MK, int, MKHash> mapping_counts;
    std::unordered_map<std::string, int> unmapped_counts;

    int replaced = 0;
    for (const auto& obj : objects) {
        auto [new_name, found] = rmap.lookup(obj.model_name);
        if (found && to_lower(new_name) != "unmatched") {
            replaced++;
            mapping_counts[{obj.model_name, new_name}]++;
        } else {
            unmapped_counts[obj.model_name]++;
        }
    }

    std::vector<MappingEntry> mappings;
    for (const auto& [k, c] : mapping_counts) {
        mappings.push_back({k.from, k.to, c});
    }
    std::sort(mappings.begin(), mappings.end(), [](const auto& a, const auto& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.from < b.from;
    });

    std::vector<UnmappedEntry> unmapped;
    for (const auto& [name, c] : unmapped_counts) {
        unmapped.push_back({name, c});
    }
    std::sort(unmapped.begin(), unmapped.end(), [](const auto& a, const auto& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.source_class < b.source_class;
    });

    return {static_cast<int>(objects.size()), 0, replaced,
            static_cast<int>(objects.size()) - replaced, rmap.len(), mappings, unmapped};
}

// --- Output writers ---

static void write_objects_tb(std::ostream& w, const std::vector<armatools::wrp::ObjectRecord>& objects,
                              const ReplacementMap& rmap, double offset_x, double offset_z) {
    for (const auto& obj : objects) {
        std::string name = obj.model_name;
        auto [new_name, found] = rmap.lookup(name);
        if (found && to_lower(new_name) != "unmatched") name = new_name;

        double x = obj.position[0] + offset_x;
        double y = obj.position[2] + offset_z;
        double z = obj.position[1];
        w << std::format("\"{}\" {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f} {:.6f}\n",
                         name, x, y, z, obj.rotation.yaw, obj.rotation.pitch, obj.rotation.roll,
                         obj.scale, obj.scale, obj.scale);
    }
}

static void write_classes_json(std::ostream& w, const std::vector<armatools::wrp::ObjectRecord>& objects,
                                const ReplacementMap& rmap, bool pretty) {
    struct Acc { int count = 0; double sum[3] = {}; };
    std::unordered_map<std::string, Acc> classes;
    for (const auto& obj : objects) {
        std::string name = obj.model_name;
        auto [new_name, found] = rmap.lookup(name);
        if (found && to_lower(new_name) != "unmatched") name = new_name;
        auto& acc = classes[name];
        acc.count++;
        acc.sum[0] += obj.position[0];
        acc.sum[1] += obj.position[1];
        acc.sum[2] += obj.position[2];
    }

    json entries = json::array();
    for (auto& [name, acc] : classes) {
        double n = static_cast<double>(acc.count);
        entries.push_back({
            {"sourceClass", name},
            {"count", acc.count},
            {"centroid", {std::round(acc.sum[0] / n * 100) / 100,
                          std::round(acc.sum[1] / n * 100) / 100,
                          std::round(acc.sum[2] / n * 100) / 100}},
        });
    }
    std::sort(entries.begin(), entries.end(), [](const json& a, const json& b) {
        if (a["count"] != b["count"]) return a["count"].get<int>() > b["count"].get<int>();
        return a["sourceClass"].get<std::string>() < b["sourceClass"].get<std::string>();
    });

    json doc = {{"schemaVersion", 1}, {"classes", entries}};
    if (pretty) w << std::setw(2) << doc << '\n';
    else w << doc << '\n';
}

static void write_stats_json(std::ostream& w, const ReplacementStats& stats, bool pretty) {
    json mappings = json::array();
    for (const auto& m : stats.mappings) {
        mappings.push_back({{"from", m.from}, {"to", m.to}, {"count", m.count}});
    }
    json unmapped = json::array();
    for (const auto& u : stats.unmapped) {
        unmapped.push_back({{"sourceClass", u.source_class}, {"count", u.count}});
    }
    json doc = {
        {"totalObjects", stats.total_objects},
        {"skippedRoads", stats.skipped_roads},
        {"replacedObjects", stats.replaced_objects},
        {"keptObjects", stats.kept_objects},
        {"replacementRules", stats.replacement_rules},
        {"mappings", mappings},
        {"unmapped", unmapped},
    };
    if (pretty) w << std::setw(2) << doc << '\n';
    else w << doc << '\n';
}

// --- Unique models ---

static std::vector<std::string> unique_models(const std::vector<armatools::wrp::ObjectRecord>& objects) {
    std::set<std::string> seen;
    std::vector<std::string> models;
    for (const auto& obj : objects) {
        auto lower = to_lower(obj.model_name);
        if (seen.insert(lower).second) models.push_back(obj.model_name);
    }
    std::sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
        return to_lower(a) < to_lower(b);
    });
    return models;
}

static void append_unmatched_to_file(const std::string& path, const ReplacementMap& rmap, int count) {
    std::ofstream f(path, std::ios::app);
    if (!f) {
        std::cerr << "Warning: could not append unmatched models to " << path << '\n';
        return;
    }
    size_t start = (count >= static_cast<int>(rmap.entries.size())) ? 0 : rmap.entries.size() - static_cast<size_t>(count);
    for (size_t i = start; i < rmap.entries.size(); i++) {
        if (to_lower(rmap.entries[i].new_model) == "unmatched") {
            f << rmap.entries[i].old_model << "\tunmatched\n";
        }
    }
}

static void print_usage() {
    std::cerr << "Usage: wrp_objreplace [flags] <replacements.txt> <input.wrp> <output_dir>\n\n"
              << "Applies model name replacements to WRP objects and writes Terrain Builder files.\n\n"
              << "Output files:\n"
              << "  objects.txt           Terrain Builder text import format\n"
              << "  objects.tml           Terrain Builder template library\n"
              << "  classes.json          Class summary with replaced names\n"
              << "  replacement_stats.json  Replacement statistics\n\n"
              << "Flags:\n"
              << "  --pretty              Pretty-print JSON output\n"
              << "  --keep-roads          Keep road objects (skipped by default)\n"
              << "  -offset-x <n>        X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>        Z coordinate offset (default: 0)\n"
              << "  -roads <file>        Road type mapping file (TSV)\n";
}

int main(int argc, char* argv[]) {
    bool pretty = false;
    bool keep_roads = false;
    double offset_x = 200000;
    double offset_z = 0;
    std::string roads_file;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--keep-roads") == 0) keep_roads = true;
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) offset_x = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "-roads") == 0 && i + 1 < argc) roads_file = argv[++i];
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.size() < 3) {
        print_usage();
        return 1;
    }

    std::string replacements_path = positional[0];
    std::string input_path = positional[1];
    std::string output_dir = positional[2];

    // Load road map
    armatools::roadobj::RoadMap roads;
    if (!roads_file.empty()) {
        try {
            roads = armatools::roadobj::load_map(roads_file);
            std::cerr << "Road map: " << roads_file << " (" << roads.types().size() << " types)\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: loading road map " << roads_file << ": " << e.what() << '\n';
            return 1;
        }
    } else {
        roads = armatools::roadobj::default_map();
    }

    // Load replacement map
    ReplacementMap rmap;
    try {
        rmap = load_replacements(replacements_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    std::cerr << "Loaded " << rmap.len() << " replacement rules from " << replacements_path << '\n';

    // Parse WRP
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
        std::cerr << "Error: no objects found in " << input_path << '\n';
        return 1;
    }

    // Filter road objects
    auto objects = world.objects;
    int skipped_roads = 0;
    if (!keep_roads) {
        std::vector<armatools::wrp::ObjectRecord> filtered;
        filtered.reserve(objects.size());
        for (const auto& obj : objects) {
            if (roads.is_road(obj.model_name)) {
                skipped_roads++;
            } else {
                filtered.push_back(obj);
            }
        }
        objects = std::move(filtered);
    }

    // Auto-append unmatched
    int append_count = 0;
    for (const auto& u : unique_models(objects)) {
        auto [_, found] = rmap.lookup(u);
        if (!found) {
            rmap.add_entry(u, "unmatched");
            append_count++;
        }
    }
    if (append_count > 0) {
        append_unmatched_to_file(replacements_path, rmap, append_count);
        std::cerr << "Appended " << append_count << " unmatched models to " << replacements_path << '\n';
    }

    // Stats
    auto stats = compute_stats(objects, rmap);
    stats.skipped_roads = skipped_roads;

    // Create output dir
    fs::create_directories(output_dir);

    // Write objects.txt
    {
        std::ofstream out(fs::path(output_dir) / "objects.txt");
        if (!out) { std::cerr << "Error: creating objects.txt\n"; return 1; }
        write_objects_tb(out, objects, rmap, offset_x, offset_z);
    }

    // Write objects.tml
    {
        std::set<std::string> seen;
        std::vector<std::string> models;
        for (const auto& obj : objects) {
            std::string name = obj.model_name;
            auto [new_name, found] = rmap.lookup(name);
            if (found && to_lower(new_name) != "unmatched") name = new_name;
            if (seen.insert(to_lower(name)).second) models.push_back(name);
        }
        std::sort(models.begin(), models.end());

        std::ofstream out(fs::path(output_dir) / "objects.tml");
        if (!out) { std::cerr << "Error: creating objects.tml\n"; return 1; }
        armatools::tb::write_tml(out, "WRP_Objects", models, nullptr, armatools::tb::default_style());
    }

    // Write classes.json
    {
        std::ofstream out(fs::path(output_dir) / "classes.json");
        if (!out) { std::cerr << "Error: creating classes.json\n"; return 1; }
        write_classes_json(out, objects, rmap, pretty);
    }

    // Write replacement_stats.json
    {
        std::ofstream out(fs::path(output_dir) / "replacement_stats.json");
        if (!out) { std::cerr << "Error: creating replacement_stats.json\n"; return 1; }
        write_stats_json(out, stats, pretty);
    }

    // Summary
    std::cerr << "Parsed: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    if (skipped_roads > 0) {
        std::cerr << std::format("Objects: {} in WRP, {} roads skipped, {} remaining\n",
                                  world.objects.size(), skipped_roads, objects.size());
    }
    std::cerr << std::format("Objects: {} total, {} replaced, {} kept original\n",
                              stats.total_objects, stats.replaced_objects, stats.kept_objects);

    if (!stats.unmapped.empty()) {
        size_t limit = std::min(size_t{10}, stats.unmapped.size());
        std::cerr << "Top unmapped classes (" << stats.unmapped.size() << " total):\n";
        for (size_t i = 0; i < limit; i++) {
            std::cerr << std::format("  {:5d}  {}\n", stats.unmapped[i].count, stats.unmapped[i].source_class);
        }
        if (stats.unmapped.size() > limit) {
            std::cerr << "  ... and " << stats.unmapped.size() - limit << " more\n";
        }
    }
    std::cerr << "Output: " << output_dir << '\n';

    return 0;
}
