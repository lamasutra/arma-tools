#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace armatools::wrp {

struct Rotation { double yaw = 0, pitch = 0, roll = 0; };

struct TextureEntry { std::string filename; uint8_t color = 0; };

struct ObjectRecord {
    uint32_t object_id = 0;
    int model_index = 0;
    std::string model_name;
    std::array<float, 12> transform{};
    std::array<double, 3> position{};
    Rotation rotation;
    double scale = 1.0;
};

struct RoadLink {
    std::vector<std::array<float, 3>> positions;
    std::vector<uint8_t> connection_types;
    int32_t object_id = 0;
    std::string p3d_path;
    std::array<float, 12> transform{};
    std::array<double, 3> position{};
    Rotation rotation;
    double scale = 1.0;
};

struct SubNet {
    double x = 0, y = 0;
    std::array<double, 3> triplet{};
    double stepping = 0;
};

struct RoadNet {
    std::string name;
    int type = 0;
    std::array<double, 3> origin{};
    double scale = 1.0;
    std::vector<SubNet> subnets;
};

struct SurfaceCounts { int ground = 0, tidal = 0, coastline = 0, sea = 0; };

struct CellFlagsInfo {
    int forest_cells = 0, roadway_cells = 0, total_cells = 0;
    SurfaceCounts surface;
};

struct Warning { std::string code, message; };

struct FormatInfo { std::string signature; int version = 0; };

struct GridInfo { int cells_x = 0, cells_y = 0; double cell_size = 0; int terrain_x = 0, terrain_y = 0; };

struct BoundsInfo { double min_elevation = 0, max_elevation = 0, world_size_x = 0, world_size_y = 0; };

struct StatsInfo {
    int texture_count = 0, model_count = 0, object_count = 0;
    int peak_count = 0, road_net_count = 0;
    CellFlagsInfo cell_flags;
    bool has_cell_flags = false;
};

struct Options { bool strict = false; bool no_objects = false; bool no_mapinfo = false; };

struct WorldData {
    FormatInfo format;
    GridInfo grid;
    BoundsInfo bounds;
    StatsInfo stats;
    std::vector<Warning> warnings;

    std::vector<TextureEntry> textures;
    std::vector<std::string> models;
    std::vector<ObjectRecord> objects;
    std::vector<RoadNet> roads;

    int app_id = 0;
    std::vector<std::vector<RoadLink>> road_links;

    std::vector<float> elevations;
    std::vector<std::array<float, 3>> peaks;
    std::vector<uint32_t> cell_bit_flags;
    std::vector<uint8_t> cell_env_sounds;
    std::vector<uint16_t> cell_texture_indexes;
    std::vector<uint32_t> cell_ext_flags;
    std::vector<uint8_t> map_info;
};

// read auto-detects format and parses a WRP file.
WorldData read(std::istream& r, Options opts = {});

// extract_position_rotation extracts position, rotation, and scale from a 4x3 transform matrix.
void extract_position_rotation(const std::array<float, 12>& m,
                                std::array<double, 3>& pos, Rotation& rot, double& scale);

} // namespace armatools::wrp
