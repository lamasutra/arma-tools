#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <armatools/wrp.h>

namespace armatools::roadnet {

// RoadType identifies the surface material.
using RoadType = std::string;

// OFP road types
inline const RoadType type_asphalt     = "asphalt";
inline const RoadType type_silnice     = "silnice";
inline const RoadType type_cobblestone = "cobblestone";
inline const RoadType type_path        = "path";
inline const RoadType type_bridge      = "bridge";

// OPRW road types
inline const RoadType type_highway  = "highway";
inline const RoadType type_concrete = "concrete";
inline const RoadType type_dirt     = "dirt";
inline const RoadType type_road     = "road";

// RoadProps holds Arma 3-compatible road attributes.
struct RoadProps {
    int id = 0;
    int order = 0;
    double width = 0;
    double terrain = 0;
    std::string map_type;
};

// Polyline is a traced road stretch.
struct Polyline {
    std::vector<std::array<double, 2>> points;
    RoadType type;
    RoadProps props;
    double length = 0;
    int seg_count = 0;
    std::string start_kind; // "dead_end", "intersection", "loop", "type_change"
    std::string end_kind;
    std::string p3d_path;   // only set for OPRW links
};

// OFP type display order.
inline const std::vector<RoadType> ofp_type_order = {
    type_asphalt, type_silnice, type_cobblestone, type_path, type_bridge
};

// OPRW type display order.
inline const std::vector<RoadType> oprw_type_order = {
    type_highway, type_asphalt, type_concrete, type_dirt, type_road
};

// OFP and OPRW road property tables.
const std::unordered_map<RoadType, RoadProps>& ofp_road_props();
const std::unordered_map<RoadType, RoadProps>& oprw_road_props();

// ExtractFromObjects extracts road polylines from OFP placed objects.
std::vector<Polyline> extract_from_objects(const std::vector<wrp::ObjectRecord>& objects);

// ExtractFromRoadLinks extracts road polylines from OPRW v12+ RoadLinks.
std::vector<Polyline> extract_from_road_links(const std::vector<std::vector<wrp::RoadLink>>& links);

// ClassifyP3D classifies a road segment by its P3D model path.
RoadType classify_p3d(const std::string& p3d_path);

} // namespace armatools::roadnet
