#pragma once

#include "replacement_map.h"

#include "armatools/wrp.h"
#include "armatools/roadobj.h"
#include "armatools/tb.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// LayerObject holds data for placing one object in a TV4L layer.
struct LayerObject {
    double x = 0, y = 0, z = 0; // Buldozer/UTM coords (with offset applied)
    double yaw = 0;              // rotation in degrees
    double pitch = 0;            // rotation in degrees
    double roll = 0;             // rotation in degrees
    double scale = 1.0;
    std::string model_name;      // P3D basename (no extension)
};

// MapMetadata holds metadata extracted from a derap'd config.cpp.
struct MapMetadata {
    std::string world_name;
    std::string description;
    std::string author;
    std::string new_roads_shape;
    int map_size = 0;
    int map_zone = 0;
    double longitude = 0;
    double latitude = 0;
    int elevation_offset = 0;
    std::string start_time;
    std::string start_date;
};

// ProjectInfo holds all parameters needed by the generator functions.
struct ProjectInfo {
    std::string name;
    std::string prefix;
    double offset_x = 200000;
    double offset_z = 0;
    std::string output_dir;
    armatools::wrp::WorldData* world = nullptr;
    armatools::roadobj::RoadMap* road_map = nullptr;
    std::string roads_shp;       // path to existing roads .shp to import
    MapMetadata* meta = nullptr;
    std::string drive_root;
    std::string db_path;
    std::string p_drive_path;    // relative path from P: drive root to outputDir
    std::string style_path;
    int split_size = 10000;
    bool extract_models = false;
    bool empty_layers = false;   // generate TV4L layers without objects (for txt import)
    ReplacementMap* replace_map = nullptr;

    // Effective heightmap (after optional upscale)
    int hm_width = 0;
    int hm_height = 0;
    std::vector<float> hm_elevations;

    // Object data populated by write_objects()
    std::vector<std::string> categories;
    std::unordered_map<std::string, std::vector<LayerObject>> cat_objects;
    std::unordered_map<std::string, std::string> cat_lib_names;
    std::unordered_map<std::string, std::string> model_path_by_base;

    // Shared pointers for TV4P <-> TV4L/TV4S cross-references
    uint32_t active_layer_ptr = 0;
    uint32_t active_area_ptr = 0;

    // Shared ALB1 pointer allocator — used by tv4p and tv4l so that
    // cross-referenced CLayer / CAreaLayer pointers stay in the same space.
    uint32_t next_alb1_ptr_counter = 0x10000;
    uint32_t alloc_ptr() { next_alb1_ptr_counter += 8; return next_alb1_ptr_counter; }

    // P:-drive relative directory for this project.
    std::string p_drive_dir() const;
};

// --- Generator function declarations ---

// heightmap
void init_heightmap(ProjectInfo& p, int scale);
void write_heightmap_asc(ProjectInfo& p);

// config generation
void write_config_cpp(ProjectInfo& p);
void write_cfg_surfaces(ProjectInfo& p);
void write_cfg_clutter(ProjectInfo& p);
void write_named_locations(ProjectInfo& p);

// layers
void write_layers_cfg(ProjectInfo& p);
std::string layer_class_name(const std::string& prefix, int index, const std::string& tex_filename);

// roads
void write_roads_lib(ProjectInfo& p);

// objects
void write_objects(ProjectInfo& p);

// shapes
void write_road_shapes(ProjectInfo& p);
void write_forest_shapes(ProjectInfo& p);

// metadata
MapMetadata* read_map_metadata(const std::string& path);
std::string resolve_new_roads_shape(const std::string& drive_root, const std::string& new_roads_shape);

// TV4P project file
void write_tv4p(ProjectInfo& p);

// TV4S shape layers
void write_tv4s(ProjectInfo& p);

// TV4L object layers
void write_tv4l(ProjectInfo& p);

// V4D raster files
void write_v4d(ProjectInfo& p);

// Model & texture extraction
void extract_models(ProjectInfo& p);
