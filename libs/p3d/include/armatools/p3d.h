#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace armatools::p3d {

using Vector3P = std::array<float, 3>;
using UV = std::array<float, 2>;

// NamedProperty is a key-value metadata pair attached to a LOD.
struct NamedProperty {
    std::string name;
    std::string value;
};

// FaceVertex stores per-vertex face attributes.
struct FaceVertex {
    uint32_t point_index = 0;
    int32_t normal_index = -1;
    UV uv = {0.0f, 0.0f};
};

// Face stores per-face attributes and vertices.
struct Face {
    std::vector<FaceVertex> vertices;
    uint32_t flags = 0;
    std::string texture;
    std::string material;
    int32_t texture_index = -1;
};

// LOD holds metadata for a single Level of Detail.
struct LOD {
    int index = 0;
    float resolution = 0.0f;
    std::string resolution_name;
    std::vector<std::string> textures;
    std::vector<std::string> materials; // MLOD face materials, ODOL v28+ rvmat paths
    std::vector<NamedProperty> named_properties;
    std::vector<std::string> named_selections; // just names, not the full vertex/face data
    std::unordered_map<std::string, std::vector<uint32_t>> named_selection_vertices;
    std::vector<Vector3P> vertices;            // vertex positions (X, Y, Z)
    std::vector<Vector3P> normals;             // normal vectors
    std::vector<std::vector<UV>> uv_sets;      // UV sets per vertex: [set][vertex]{u,v}
    std::vector<Face> face_data;
    std::vector<std::vector<uint32_t>> faces;  // face vertex indices (triangles, quads, etc.)
    int vertex_count = 0;
    int face_count = 0;
    Vector3P bounding_box_min = {0.0f, 0.0f, 0.0f};
    Vector3P bounding_box_max = {0.0f, 0.0f, 0.0f};
    Vector3P bounding_center = {0.0f, 0.0f, 0.0f};
    float bounding_radius = 0.0f;
};

// ModelInfo holds model-level metadata from ODOL files.
struct ModelInfo {
    float bounding_sphere = 0.0f;
    Vector3P bounding_box_min = {0.0f, 0.0f, 0.0f};
    Vector3P bounding_box_max = {0.0f, 0.0f, 0.0f};
    Vector3P center_of_mass = {0.0f, 0.0f, 0.0f};
    float mass = 0.0f;
    float armor = 0.0f;
    // Special LOD indices (-1 = not present)
    int memory_lod = -1;
    int geometry_lod = -1;
    int fire_geometry_lod = -1;
    int view_geometry_lod = -1;
    int land_contact_lod = -1;
    int roadway_lod = -1;
    int paths_lod = -1;
    int hitpoints_lod = -1;
};

// P3DFile is the parsed metadata from a P3D model file.
struct P3DFile {
    std::string format; // "ODOL" or "MLOD"
    int version = 0;    // ODOL version (7, 28-75) or MLOD version (257)
    std::vector<LOD> lods;
    std::unique_ptr<ModelInfo> model_info; // nullptr for MLOD
};

// SizeInfo holds model dimensions calculated from a LOD's bounding box.
struct SizeInfo {
    std::string source; // LOD used: "Geometry", "1.000", etc.
    Vector3P bbox_min = {0.0f, 0.0f, 0.0f};
    Vector3P bbox_max = {0.0f, 0.0f, 0.0f};
    Vector3P bbox_center = {0.0f, 0.0f, 0.0f};
    float bbox_radius = 0.0f;
    Vector3P dimensions = {0.0f, 0.0f, 0.0f}; // width, height, depth
};

// Read parses a P3D file from r and returns its metadata.
// Supports ODOL (v7 OFP/CWA, v28-75 Arma) and MLOD (editable) formats.
P3DFile read(std::istream& r);

// ResolutionName returns a human-readable name for a LOD resolution value.
std::string resolution_name(float r);

// CalculateSize computes model dimensions from the Geometry LOD's bounding box.
// If no Geometry LOD is present, falls back to the lowest-resolution visual LOD.
// Returns std::nullopt if no suitable LOD is found.
// The warning string, if non-empty, describes the fallback taken.
struct CalculateSizeResult {
    std::optional<SizeInfo> info;
    std::string warning;
};
CalculateSizeResult calculate_size(const P3DFile& model);

// VisualBBox computes a bounding box from the actual vertex positions of the
// best visual LOD (1.000 preferred). Returns std::nullopt if no visual LOD
// with vertices is found.
std::optional<SizeInfo> visual_bbox(const P3DFile& model);

} // namespace armatools::p3d
