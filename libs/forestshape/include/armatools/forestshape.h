#pragma once

#include <array>
#include <string>
#include <vector>

#include <armatools/wrp.h>

namespace armatools::forestshape {

// ForestType identifies the forest vegetation type.
using ForestType = std::string;

inline const ForestType forest_mixed   = "mixed";
inline const ForestType forest_conifer = "conifer";

// Polygon is a merged forest area polygon.
struct Polygon {
    int id = 0;
    ForestType type;
    std::vector<std::array<double, 2>> exterior;      // counterclockwise exterior ring
    std::vector<std::vector<std::array<double, 2>>> holes; // clockwise interior rings
    int cell_count = 0;
    double area = 0;
};

// ExtractFromObjects extracts forest polygons from OFP forest block objects.
std::vector<Polygon> extract_from_objects(const std::vector<wrp::ObjectRecord>& objects);

} // namespace armatools::forestshape
