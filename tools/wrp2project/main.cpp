#include "project.h"

#include "armatools/shp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string expand_user_path(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    // Expand only "~" and "~/..."; keep "~user/..." untouched.
    if (path.size() > 1 && path[1] != '/' && path[1] != '\\') return path;

    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || !*home) home = std::getenv("USERPROFILE");
    if ((!home || !*home)) {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* hpath = std::getenv("HOMEPATH");
        if (drive && hpath) {
            return std::string(drive) + std::string(hpath) + path.substr(1);
        }
    }
#endif
    if (!home || !*home) return path;

    if (path.size() == 1) return std::string(home);
    if (path[1] == '/' || path[1] == '\\') return std::string(home) + path.substr(1);
    return path;
}

std::string ProjectInfo::p_drive_dir() const {
    if (!p_drive_path.empty()) return p_drive_path;
    // Try to infer from output directory (look for "/P/" segment)
    std::string s = fs::path(fs::weakly_canonical(output_dir)).generic_string();
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto i = low.find("/p/");
    if (i != std::string::npos) {
        std::string rel = s.substr(i + 3);
        std::replace(rel.begin(), rel.end(), '/', '\\');
        if (!rel.empty()) return rel;
    }
    std::string map_name = "map_";
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return map_name + lower_name;
}

static double detect_offset_from_shp(const std::string& shp_path, double map_size_x) {
    try {
        auto bbox = armatools::shp::read_bbox(shp_path);
        if (map_size_x <= 0 || bbox.x_min <= map_size_x) return 0;
        double detected = bbox.x_max - map_size_x;
        detected = std::floor(detected / 1000) * 1000;
        return detected;
    } catch (...) {
        return 0;
    }
}

static void print_usage() {
    std::cerr << "Usage: wrp2project [flags] <input.wrp> [output_dir]\n\n"
              << "Generates a Terrain Builder project directory from a WRP file.\n\n"
              << "Output structure:\n"
              << "  map_Name/\n"
              << "    config.cpp, cfgSurfaces.hpp, cfgClutter.hpp, Map_Name.hpp\n"
              << "    data/roads/RoadsLib.cfg\n"
              << "    source/heightmap.asc, layers.cfg\n"
              << "    TemplateLibs/<category>.tml\n"
              << "    source/objects_<category>.txt\n"
              << "    map_name.tv4p\n\n"
              << "Flags:\n"
              << "  --name <s>        Terrain name (default: derived from WRP filename)\n"
              << "  -offset-x <n>     X coordinate offset (default: 200000)\n"
              << "  -offset-z <n>     Z coordinate offset (default: 0)\n"
              << "  --prefix <s>      Layer name prefix (default: derived from name)\n"
              << "  --roads <f>       Road type mapping file (TSV: pattern<TAB>RoadType)\n"
              << "  --roads-shp <f>   Import roads from existing .shp file\n"
              << "  --config <f>      Import metadata from derap'd config.cpp\n"
              << "  --drive <d>       Project drive root for P3D paths (e.g., /mnt/p)\n"
              << "  --db <f>          a3db database for model bounding boxes\n"
              << "  --split <n>       Max objects per text import file (default: 10000, 0=no split)\n"
              << "  --style <f>       JSON file mapping categories to TML shape/color styles\n"
              << "  --hm-scale <n>    Heightmap upscale factor (1, 2, 4, 8, 16)\n"
              << "  --extract-models  Extract P3D models and textures to drive\n"
              << "  --empty-layers    Generate TV4L layers without objects (for txt import)\n"
              << "  --replace <f>     Apply model name replacements from TSV file\n"
              << "  --pretty          Pretty-print JSON output\n";
}

int main(int argc, char* argv[]) {
    std::string name_flag;
    double offset_x = 200000;
    double offset_z = 0;
    bool offset_x_explicit = false;
    std::string prefix_flag;
    std::string roads_file;
    std::string roads_shp;
    std::string config_file;
    std::string drive;
    std::string db_path;
    int split_size = 10000;
    std::string style_path;
    int hm_scale = 1;
    bool extract_models = false;
    bool empty_layers = false;
    std::string replace_file;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) name_flag = argv[++i];
        else if (std::strcmp(argv[i], "-offset-x") == 0 && i + 1 < argc) { offset_x = std::stod(argv[++i]); offset_x_explicit = true; }
        else if (std::strcmp(argv[i], "-offset-z") == 0 && i + 1 < argc) offset_z = std::stod(argv[++i]);
        else if (std::strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) prefix_flag = argv[++i];
        else if (std::strcmp(argv[i], "--roads") == 0 && i + 1 < argc) roads_file = argv[++i];
        else if (std::strcmp(argv[i], "--roads-shp") == 0 && i + 1 < argc) roads_shp = argv[++i];
        else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) config_file = argv[++i];
        else if (std::strcmp(argv[i], "--drive") == 0 && i + 1 < argc) drive = argv[++i];
        else if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) db_path = argv[++i];
        else if (std::strcmp(argv[i], "--split") == 0 && i + 1 < argc) split_size = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) style_path = argv[++i];
        else if (std::strcmp(argv[i], "--hm-scale") == 0 && i + 1 < argc) hm_scale = std::stoi(argv[++i]);
        else if (std::strcmp(argv[i], "--extract-models") == 0) extract_models = true;
        else if (std::strcmp(argv[i], "--empty-layers") == 0) empty_layers = true;
        else if (std::strcmp(argv[i], "--replace") == 0 && i + 1 < argc) replace_file = argv[++i];
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
    input_path = expand_user_path(input_path);

    // Derive terrain name from filename if not specified
    std::string terrain_name = name_flag;
    if (terrain_name.empty()) {
        terrain_name = fs::path(input_path).stem().string();
        if (!terrain_name.empty()) {
            terrain_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(terrain_name[0])));
        }
    }

    std::cerr << "Creating project for " << terrain_name << " (" << fs::path(input_path).filename().string() << ")\n";

    std::string layer_prefix = prefix_flag;
    if (layer_prefix.empty()) {
        layer_prefix = terrain_name;
        std::transform(layer_prefix.begin(), layer_prefix.end(), layer_prefix.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    // Output directory
    std::string output_dir;
    if (positional.size() >= 2) {
        output_dir = positional[1];
    } else {
        output_dir = (fs::path(input_path).parent_path() / ("project_" + terrain_name)).string();
    }
    output_dir = expand_user_path(output_dir);
    roads_file = expand_user_path(roads_file);
    roads_shp = expand_user_path(roads_shp);
    config_file = expand_user_path(config_file);
    drive = expand_user_path(drive);
    db_path = expand_user_path(db_path);
    style_path = expand_user_path(style_path);
    replace_file = expand_user_path(replace_file);

    // Load road map
    armatools::roadobj::RoadMap roads = roads_file.empty()
        ? armatools::roadobj::default_map()
        : armatools::roadobj::load_map(roads_file);

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

    // Create directory structure
    std::string lower_name = terrain_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string map_name = "map_" + lower_name;

    std::vector<std::string> dirs = {
        output_dir,
        (fs::path(output_dir) / "data" / "roads").string(),
        (fs::path(output_dir) / "source").string(),
        (fs::path(output_dir) / "TemplateLibs").string(),
        (fs::path(output_dir) / "source" / "TerrainBuilder").string(),
        (fs::path(output_dir) / (map_name + ".Layers")).string(),
        (fs::path(output_dir) / (map_name + ".Shapes")).string(),
        (fs::path(output_dir) / (map_name + ".Cache")).string(),
    };
    for (const auto& d : dirs) {
        fs::create_directories(d);
    }

    // Compute P:-drive relative path
    std::string p_drive_path;
    if (!drive.empty()) {
        auto abs_out = fs::weakly_canonical(output_dir);
        auto abs_drive = fs::weakly_canonical(drive);
        auto rel = fs::relative(abs_out, abs_drive);
        p_drive_path = rel.generic_string();
        std::replace(p_drive_path.begin(), p_drive_path.end(), '/', '\\');
    }

    // Import config metadata if provided
    MapMetadata* meta = nullptr;
    std::unique_ptr<MapMetadata> meta_ptr;
    if (!config_file.empty()) {
        meta = read_map_metadata(config_file);
        if (meta) {
            meta_ptr.reset(meta);
            std::cerr << std::format("Config: {} (world={}, mapSize={}, lon={:.3f}, lat={:.3f}, elev={})\n",
                                      config_file, meta->world_name, meta->map_size,
                                      meta->longitude, meta->latitude, meta->elevation_offset);
        }
    }

    // Auto-discover roads SHP from config metadata
    std::string resolved_roads_shp = roads_shp;
    if (resolved_roads_shp.empty() && meta && !meta->new_roads_shape.empty() && !drive.empty()) {
        resolved_roads_shp = resolve_new_roads_shape(drive, meta->new_roads_shape);
        if (!resolved_roads_shp.empty()) {
            std::cerr << "Roads SHP: auto-discovered " << resolved_roads_shp << " (from config newRoadsShape)\n";
        }
    }

    // Early offset detection from roads SHP
    double final_offset_x = offset_x;
    if (!resolved_roads_shp.empty()) {
        double detected = detect_offset_from_shp(resolved_roads_shp, world.bounds.world_size_x);
        if (detected != final_offset_x && detected != 0) {
            if (offset_x_explicit) {
                std::cerr << std::format("Note: SHP suggests offset X={:.0f} but using explicit -offset-x={:.0f}\n",
                                          detected, final_offset_x);
            } else {
                std::cerr << std::format("Offset X: using {:.0f} (detected from roads SHP, was {:.0f})\n",
                                          detected, final_offset_x);
                final_offset_x = detected;
            }
        }
    }

    // Load replacement map
    ReplacementMap rmap;
    if (!replace_file.empty()) {
        try {
            rmap = load_replacements(replace_file);
            std::cerr << "Loaded " << rmap.len() << " replacement rules from " << replace_file << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n';
            return 1;
        }
    }

    ProjectInfo proj;
    proj.name = terrain_name;
    proj.prefix = layer_prefix;
    proj.offset_x = final_offset_x;
    proj.offset_z = offset_z;
    proj.output_dir = output_dir;
    proj.world = &world;
    proj.road_map = &roads;
    proj.roads_shp = resolved_roads_shp;
    proj.meta = meta;
    proj.drive_root = drive;
    proj.db_path = db_path;
    proj.p_drive_path = p_drive_path;
    proj.style_path = style_path;
    proj.split_size = split_size;
    proj.extract_models = extract_models;
    proj.empty_layers = empty_layers;
    if (!replace_file.empty()) proj.replace_map = &rmap;

    // Initialize heightmap (with optional upscale)
    init_heightmap(proj, hm_scale);

    // Generate all output files
    struct Step {
        const char* desc;
        void (*fn)(ProjectInfo&);
    };
    Step steps[] = {
        {"heightmap.asc", write_heightmap_asc},
        {"config.cpp", write_config_cpp},
        {"cfgSurfaces.hpp", write_cfg_surfaces},
        {"cfgClutter.hpp", write_cfg_clutter},
        {"Map_Name.hpp", write_named_locations},
        {"layers.cfg", write_layers_cfg},
        {"RoadsLib.cfg", write_roads_lib},
        {"roads.shp", write_road_shapes},
        {"forest.shp", write_forest_shapes},
        {"objects", write_objects},
        {"tv4p", write_tv4p},
        {"v4d", write_v4d},
        {"tv4s", write_tv4s},
#if defined(WRP2PROJECT_WITH_TV4L)
        {"tv4l", write_tv4l},
#endif
        {"extract-models", ::extract_models},
    };
    int num_steps = static_cast<int>(std::size(steps));

    for (int i = 0; i < num_steps; i++) {
        std::cerr << std::format("[{}/{}] {}\n", i + 1, num_steps, steps[i].desc);
        try {
            steps[i].fn(proj);
        } catch (const std::exception& e) {
            std::cerr << "Error: writing " << steps[i].desc << ": " << e.what() << '\n';
            return 1;
        }
    }

    // Summary
    std::cerr << "wrp2project: " << input_path << " (" << world.format.signature << " v" << world.format.version << ")\n";
    std::cerr << "Terrain: " << terrain_name << " (prefix: " << layer_prefix << ")\n";
    std::cerr << std::format("Grid: {}x{} cells, cell size {:.0f}m\n",
                              world.grid.cells_x, world.grid.cells_y, world.grid.cell_size);
    std::cerr << std::format("World: {:.0f}x{:.0f}m, elevation {:.1f}..{:.1f}m\n",
                              world.bounds.world_size_x, world.bounds.world_size_y,
                              world.bounds.min_elevation, world.bounds.max_elevation);
    std::cerr << std::format("Textures: {}, Models: {}, Objects: {}\n",
                              world.stats.texture_count, world.stats.model_count, world.stats.object_count);
    std::cerr << std::format("Heightmap: {}x{} (cell {:.1f}m)\n",
                              proj.hm_width, proj.hm_height,
                              world.bounds.world_size_x / static_cast<double>(proj.hm_width));
    std::cerr << std::format("Offset: X+{:.0f} Z+{:.0f}\n", proj.offset_x, proj.offset_z);
    std::cerr << "Output: " << output_dir << '\n';

    return 0;
}
