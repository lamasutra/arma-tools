#include "armatools/p3d.h"

#include "armatools/binutil.h"
#include "armatools/lzss.h"
#include "armatools/lzo.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace armatools::p3d {

namespace {

using namespace armatools::binutil;

// ---------------------------------------------------------------------------
// Resolution name mapping
// ---------------------------------------------------------------------------

// isVisualLOD returns true if the resolution name represents a visual
// (distance-based) LOD -- i.e. starts with a digit.
bool is_visual_lod(const std::string& name) {
    return !name.empty() && name[0] >= '0' && name[0] <= '9';
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> read_string_array(std::istream& r) {
    auto count = read_u32(r);
    std::vector<std::string> result(count);
    for (uint32_t i = 0; i < count; ++i) {
        result[i] = read_asciiz(r);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ODOL v7 helpers
// ---------------------------------------------------------------------------

// readCompressedArrayV7Raw reads a count-prefixed LZSS-compressed/raw array.
// Returns (count, data).
static std::pair<uint32_t, std::vector<uint8_t>>
read_compressed_array_v7_raw(std::istream& r, int elem_size) {
    auto count = read_u32(r);
    auto expected_size = static_cast<size_t>(count) * static_cast<size_t>(elem_size);
    auto data = lzss::decompress_or_raw(r, expected_size);
    return {count, std::move(data)};
}

// skipCompressedArray reads a LZSS-compressed array header and
// skips/consumes its data. Returns the element count.
static uint32_t skip_compressed_array_v7(std::istream& r, int elem_size) {
    auto count = read_u32(r);
    auto total_bytes = static_cast<size_t>(count) * static_cast<size_t>(elem_size);
    if (total_bytes < 1024) {
        r.seekg(static_cast<std::streamoff>(total_bytes), std::ios::cur);
        if (!r) throw std::runtime_error("p3d: failed to skip raw array");
    } else {
        lzss::decompress(r, total_bytes);
    }
    return count;
}

// ---------------------------------------------------------------------------
// ODOL v7 model info
// ---------------------------------------------------------------------------

static std::unique_ptr<ModelInfo> read_odol_model_info(std::istream& r) {
    auto info = std::make_unique<ModelInfo>();

    // properties (uint32)
    r.seekg(4, std::ios::cur);

    // lodSphere (float32)
    info->bounding_sphere = read_f32(r);

    // physicsSphere (float32)
    r.seekg(4, std::ios::cur);

    // properties2 (uint32)
    r.seekg(4, std::ios::cur);

    // hintsAnd, hintsOr (uint32 each)
    r.seekg(8, std::ios::cur);

    // aimPoint (Vector3F = 12 bytes)
    r.seekg(12, std::ios::cur);

    // color (BGRA, 4 bytes), color2 (BGRA, 4 bytes)
    r.seekg(8, std::ios::cur);

    // density (float32)
    r.seekg(4, std::ios::cur);

    // min (Vector3F)
    for (size_t j = 0; j < 3; ++j)
        info->bounding_box_min[j] = read_f32(r);

    // max (Vector3F)
    for (size_t j = 0; j < 3; ++j)
        info->bounding_box_max[j] = read_f32(r);

    // lodCenter (Vector3F)
    r.seekg(12, std::ios::cur);

    // physicsCenter (Vector3F)
    r.seekg(12, std::ios::cur);

    // massCenter (Vector3F)
    for (size_t j = 0; j < 3; ++j)
        info->center_of_mass[j] = read_f32(r);

    // invInertia (Matrix3F = 9 floats = 36 bytes)
    r.seekg(36, std::ios::cur);

    // autoCenter, autoCenter2, canOcclude, canBeOccluded, allowAnimation (5 bools)
    r.seekg(5, std::ios::cur);

    // mapType (uint8)
    r.seekg(1, std::ios::cur);

    // masses (compressed float array)
    skip_compressed_array_v7(r, 4);

    // mass (float32)
    info->mass = read_f32(r);

    // invMass (float32)
    r.seekg(4, std::ios::cur);

    // armor (float32)
    info->armor = read_f32(r);

    // invArmor (float32)
    r.seekg(4, std::ios::cur);

    // LOD indices (12 x int8)
    uint8_t indices[12];
    if (!r.read(reinterpret_cast<char*>(indices), 12))
        throw std::runtime_error("p3d: failed to read LOD indices");
    info->memory_lod         = static_cast<int>(static_cast<int8_t>(indices[0]));
    info->geometry_lod       = static_cast<int>(static_cast<int8_t>(indices[1]));
    info->fire_geometry_lod  = static_cast<int>(static_cast<int8_t>(indices[2]));
    info->view_geometry_lod  = static_cast<int>(static_cast<int8_t>(indices[3]));
    // indices[4..7] = viewPilot, viewGunner, viewCommander, viewCargo
    info->land_contact_lod   = static_cast<int>(static_cast<int8_t>(indices[8]));
    info->roadway_lod        = static_cast<int>(static_cast<int8_t>(indices[9]));
    info->paths_lod          = static_cast<int>(static_cast<int8_t>(indices[10]));
    info->hitpoints_lod      = static_cast<int>(static_cast<int8_t>(indices[11]));

    return info;
}

// ---------------------------------------------------------------------------
// ODOL v7 LOD reader
// ---------------------------------------------------------------------------

static LOD read_odol_lod(std::istream& r) {
    LOD lod;

    // flags (compressed uint32 array)
    skip_compressed_array_v7(r, 4);

    // UV coords (compressed Vector2 array, 8 bytes per element)
    auto [uv_count, uv_data] = read_compressed_array_v7_raw(r, 8);
    if (uv_count > 0) {
        std::vector<UV> uv_set(uv_count);
        for (uint32_t i = 0; i < uv_count; ++i) {
            auto off = static_cast<size_t>(i) * 8;
            float u, v;
            std::memcpy(&u, uv_data.data() + off, 4);
            std::memcpy(&v, uv_data.data() + off + 4, 4);
            uv_set[i] = {u, v};
        }
        lod.uv_sets.push_back(std::move(uv_set));
    }

    // positions (Vector3F array, count-prefixed, not compressed)
    auto pos_count = read_u32(r);
    lod.vertex_count = static_cast<int>(pos_count);
    lod.vertices.resize(pos_count);
    for (uint32_t i = 0; i < pos_count; ++i) {
        for (size_t j = 0; j < 3; ++j)
            lod.vertices[i][j] = read_f32(r);
    }

    // normals (Vector3F array, count-prefixed, not compressed)
    auto normal_count = read_u32(r);
    lod.normals.resize(normal_count);
    for (uint32_t i = 0; i < normal_count; ++i) {
        for (size_t j = 0; j < 3; ++j)
            lod.normals[i][j] = read_f32(r);
    }

    // hintsOr, hintsAnd (uint32 each)
    r.seekg(8, std::ios::cur);

    // min (Vector3F)
    for (size_t j = 0; j < 3; ++j)
        lod.bounding_box_min[j] = read_f32(r);
    // max (Vector3F)
    for (size_t j = 0; j < 3; ++j)
        lod.bounding_box_max[j] = read_f32(r);

    // center (Vector3F) + radius (float32)
    for (size_t j = 0; j < 3; ++j)
        lod.bounding_center[j] = read_f32(r);
    lod.bounding_radius = read_f32(r);

    // textureNames (string array)
    auto raw_textures = read_string_array(r);
    for (auto& t : raw_textures) {
        if (!t.empty())
            lod.textures.push_back(t);
    }

    // pointToVertices (compressed uint16 array) -- skip
    skip_compressed_array_v7(r, 2);

    // vertexToPoints (compressed uint16 array) -- skip
    skip_compressed_array_v7(r, 2);

    // Faces: count (uint32), size (uint32), then face data
    auto face_count = read_u32(r);
    lod.face_count = static_cast<int>(face_count);

    // size field (total byte size of face data)
    read_u32(r);

    // Each face: flags(u32) + textureIndex(u16) + vertexCount(u8) + vertices(N * u16)
    lod.faces.reserve(face_count);
    lod.face_data.reserve(face_count);
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        auto flags = read_u32(r);
        auto texture_index = read_u16(r);
        auto n = read_u8(r);
        std::vector<uint32_t> indices(n);
        std::vector<FaceVertex> face_verts(n);
        for (uint8_t j = 0; j < n; ++j) {
            auto idx = static_cast<uint32_t>(read_u16(r));
            indices[j] = idx;
            int32_t normal_idx = -1;
            if (idx < lod.normals.size())
                normal_idx = static_cast<int32_t>(idx);
            UV uv = {0.0f, 0.0f};
            if (!lod.uv_sets.empty() && idx < lod.uv_sets[0].size())
                uv = lod.uv_sets[0][idx];
            face_verts[j] = FaceVertex{idx, normal_idx, uv};
        }
        lod.faces.push_back(std::move(indices));
        std::string texture;
        if (texture_index < raw_textures.size())
            texture = raw_textures[texture_index];
        lod.face_data.push_back(Face{
            std::move(face_verts),
            flags,
            std::move(texture),
            std::string{},
            static_cast<int32_t>(texture_index)});
    }

    // Sections (ShapeSection array)
    auto section_count = read_u32(r);
    // Each section: 18 bytes
    r.seekg(static_cast<std::streamoff>(section_count) * 18, std::ios::cur);

    // Named sections (NamedSection array)
    auto named_section_count = read_u32(r);
    lod.named_selections.resize(named_section_count);
    for (uint32_t i = 0; i < named_section_count; ++i) {
        auto name = read_asciiz(r);
        lod.named_selections[i] = name;

        // faceIndices (compressed uint16 array)
        auto [face_index_count, face_indices_data] =
            read_compressed_array_v7_raw(r, 2);
        std::vector<uint32_t> selected_faces;
        selected_faces.reserve(face_index_count);
        for (uint32_t fi = 0; fi < face_index_count; ++fi) {
            uint16_t idx = 0;
            std::memcpy(&idx,
                        face_indices_data.data() + static_cast<size_t>(fi) * 2,
                        2);
            selected_faces.push_back(static_cast<uint32_t>(idx));
        }
        // faceWeights (compressed uint8 array)
        skip_compressed_array_v7(r, 1);
        // faceSelectionIndices (compressed uint32 array)
        skip_compressed_array_v7(r, 4);
        // needSelection (bool = 1 byte)
        r.seekg(1, std::ios::cur);
        // faceSelectionIndices2 (compressed uint32 array)
        skip_compressed_array_v7(r, 4);
        // vertexIndices (compressed uint16 array)
        auto [vertex_index_count, vertex_indices_data] =
            read_compressed_array_v7_raw(r, 2);
        // vertexWeights (compressed uint8 array)
        auto [vertex_weight_count, vertex_weights_data] =
            read_compressed_array_v7_raw(r, 1);

        std::vector<uint32_t> selected_vertices;
        selected_vertices.reserve(vertex_index_count);
        for (uint32_t vi = 0; vi < vertex_index_count; ++vi) {
            uint16_t idx = 0;
            std::memcpy(&idx, vertex_indices_data.data() + static_cast<size_t>(vi) * 2, 2);
            if (vertex_weight_count == vertex_index_count &&
                vi < vertex_weights_data.size() &&
                vertex_weights_data[vi] == 0)
                continue;
            selected_vertices.push_back(static_cast<uint32_t>(idx));
        }

        if (!selected_faces.empty()) {
            auto& target_faces = lod.named_selection_faces[name];
            target_faces.insert(target_faces.end(), selected_faces.begin(), selected_faces.end());
            std::sort(target_faces.begin(), target_faces.end());
            target_faces.erase(std::unique(target_faces.begin(), target_faces.end()), target_faces.end());
        }
        if (!selected_vertices.empty()) {
            auto& target = lod.named_selection_vertices[name];
            target.insert(target.end(), selected_vertices.begin(), selected_vertices.end());
            std::sort(target.begin(), target.end());
            target.erase(std::unique(target.begin(), target.end()), target.end());
        }
    }
    if (!lod.vertices.empty()) {
        const auto max_vertex_index = static_cast<uint32_t>(lod.vertices.size());
        for (auto& [_, indices] : lod.named_selection_vertices) {
            indices.erase(std::remove_if(indices.begin(), indices.end(),
                                         [max_vertex_index](uint32_t idx) {
                                             return idx >= max_vertex_index;
                                         }),
                          indices.end());
        }
    }

    // Named properties
    auto prop_count = read_u32(r);
    lod.named_properties.resize(prop_count);
    for (uint32_t i = 0; i < prop_count; ++i) {
        lod.named_properties[i].name = read_asciiz(r);
        lod.named_properties[i].value = read_asciiz(r);
    }

    // Animation phases
    auto anim_count = read_u32(r);
    for (uint32_t i = 0; i < anim_count; ++i) {
        // time (float32)
        r.seekg(4, std::ios::cur);
        // points (Vector3F array: count + data)
        auto point_count = read_u32(r);
        r.seekg(static_cast<std::streamoff>(point_count) * 12, std::ios::cur);
    }

    // color (BGRA, 4 bytes), color2 (BGRA, 4 bytes), flags2 (uint32)
    r.seekg(12, std::ios::cur);

    // Proxies
    auto proxy_count = read_u32(r);
    for (uint32_t i = 0; i < proxy_count; ++i) {
        // name (asciiz)
        read_asciiz(r);
        // transform (Matrix4F = 48 bytes)
        r.seekg(48, std::ios::cur);
        // id (int32) + sectionIndex (int32)
        r.seekg(8, std::ios::cur);
    }

    return lod;
}

// ---------------------------------------------------------------------------
// ODOL v7 top-level reader
// ---------------------------------------------------------------------------

static P3DFile read_odol(std::istream& r, uint32_t version) {
    auto lod_count = read_u32(r);
    if (lod_count > 1000)
        throw std::runtime_error(
            std::format("odol: invalid lodCount: {}", lod_count));

    std::vector<LOD> lods(lod_count);
    for (uint32_t i = 0; i < lod_count; ++i) {
        lods[i] = read_odol_lod(r);
        lods[i].index = static_cast<int>(i);
    }

    // After all LODs: read lodDistances (resolution values)
    for (uint32_t i = 0; i < lod_count; ++i) {
        auto res = read_f32(r);
        lods[i].resolution = res;
        lods[i].resolution_name = resolution_name(res);
    }

    // Read model-level info
    auto info = read_odol_model_info(r);

    P3DFile result;
    result.format = "ODOL";
    result.version = static_cast<int>(version);
    result.lods = std::move(lods);
    result.model_info = std::move(info);
    return result;
}

// ---------------------------------------------------------------------------
// MLOD helpers
// ---------------------------------------------------------------------------

static void read_mlod_taggs(std::istream& r, LOD& lod) {
    auto sig = read_signature(r);
    if (sig != "TAGG")
        throw std::runtime_error(
            std::format("mlod: expected TAGG signature, got \"{}\"", sig));

    for (;;) {
        // active (uint8)
        r.seekg(1, std::ios::cur);

        auto tag_name = read_asciiz(r);
        auto tag_size = read_u32(r);

        if (tag_name == "#EndOfFile#")
            break;

        if (tag_name == "#Property#") {
            auto key = read_fixed_string(r, 64);
            auto val = read_fixed_string(r, 64);
            lod.named_properties.push_back(NamedProperty{std::move(key), std::move(val)});
        } else {
            // Named selections have tag names that don't start with '#'.
            if (!tag_name.empty() && tag_name[0] != '#') {
                lod.named_selections.push_back(tag_name);
                if (tag_size > 0) {
                    std::vector<uint8_t> tag_data(static_cast<size_t>(tag_size));
                    if (!r.read(reinterpret_cast<char*>(tag_data.data()),
                                static_cast<std::streamsize>(tag_data.size())))
                        throw std::runtime_error("mlod: failed to read named selection TAGG data");

                    // MLOD named selection TAGG payload starts with per-vertex weights.
                    auto vertex_count = static_cast<size_t>(std::max(lod.vertex_count, 0));
                    auto vertex_span = std::min(vertex_count, tag_data.size());
                    std::vector<uint32_t> selected_vertices;
                    selected_vertices.reserve(vertex_span);
                    for (size_t i = 0; i < vertex_span; ++i) {
                        if (tag_data[i] != 0)
                            selected_vertices.push_back(static_cast<uint32_t>(i));
                    }
                    if (!selected_vertices.empty()) {
                        auto& target = lod.named_selection_vertices[tag_name];
                        target.insert(target.end(),
                                      selected_vertices.begin(),
                                      selected_vertices.end());
                        std::sort(target.begin(), target.end());
                        target.erase(std::unique(target.begin(), target.end()), target.end());
                    }
                }
            } else if (tag_size > 0) {
                r.seekg(static_cast<std::streamoff>(tag_size), std::ios::cur);
            }
        }
    }
}

static LOD read_mlod_lod(std::istream& r) {
    LOD lod;

    // P3DM or SP3X sub-signature
    auto sig = read_signature(r);
    if (sig != "P3DM" && sig != "SP3X")
        throw std::runtime_error(
            std::format("mlod: expected P3DM or SP3X signature, got \"{}\"", sig));

    // major_version (uint32), minor_version (uint32)
    r.seekg(8, std::ios::cur);

    auto points_count = read_u32(r);
    auto normals_count = read_u32(r);
    auto faces_count = read_u32(r);
    // flags (uint32)
    r.seekg(4, std::ios::cur);

    lod.vertex_count = static_cast<int>(points_count);
    lod.face_count = static_cast<int>(faces_count);

    // Read points data: pointsCount x 16 bytes (Vector3F + uint32 flags)
    lod.vertices.resize(points_count);
    if (points_count > 0) {
        Vector3P b_min = {std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max()};
        Vector3P b_max = {-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max()};
        for (uint32_t i = 0; i < points_count; ++i) {
            for (size_t j = 0; j < 3; ++j)
                lod.vertices[i][j] = read_f32(r);
            // Skip flags (uint32)
            r.seekg(4, std::ios::cur);
            for (size_t j = 0; j < 3; ++j) {
                if (lod.vertices[i][j] < b_min[j])
                    b_min[j] = lod.vertices[i][j];
                if (lod.vertices[i][j] > b_max[j])
                    b_max[j] = lod.vertices[i][j];
            }
        }
        lod.bounding_box_min = b_min;
        lod.bounding_box_max = b_max;
    }

    // Normals data: normalsCount x 12 bytes (Vector3F)
    lod.normals.resize(normals_count);
    for (uint32_t i = 0; i < normals_count; ++i) {
        for (size_t j = 0; j < 3; ++j)
            lod.normals[i][j] = read_f32(r);
    }

    // Read faces
    std::set<std::string> tex_set;
    std::set<std::string> mat_set;
    lod.faces.resize(faces_count);
    lod.face_data.reserve(faces_count);
    for (uint32_t fi = 0; fi < faces_count; ++fi) {
        // nVertices (int32)
        auto nv = read_i32(r);
        if (nv < 0 || nv > 4)
            throw std::runtime_error(
                std::format("mlod: face {} nVertices: invalid {}", fi, nv));

        // 4 x vertex struct (points_index i32, normals_index i32, u f32, v f32)
        std::vector<uint32_t> indices;
        std::vector<FaceVertex> face_verts;
        indices.reserve(static_cast<size_t>(nv));
        face_verts.reserve(static_cast<size_t>(nv));
        for (int j = 0; j < 4; ++j) {
            auto point_idx = read_i32(r);
            auto normal_idx = read_i32(r);
            auto u = read_f32(r);
            auto v = read_f32(r);
            if (j < nv) {
                indices.push_back(static_cast<uint32_t>(point_idx));
                face_verts.push_back(FaceVertex{
                    static_cast<uint32_t>(point_idx),
                    normal_idx,
                    {u, v}});
            }
        }
        // Reverse vertex order to match ODOL winding convention
        std::ranges::reverse(indices);
        std::ranges::reverse(face_verts);
        lod.faces[fi] = std::move(indices);

        // flags (int32)
        auto flags = read_i32(r);
        // texture (asciiz)
        auto texture = read_asciiz(r);
        if (!texture.empty())
            tex_set.insert(texture);
        // material (asciiz)
        auto material = read_asciiz(r);
        if (!material.empty())
            mat_set.insert(material);

        lod.face_data.push_back(Face{
            std::move(face_verts),
            static_cast<uint32_t>(flags),
            std::move(texture),
            std::move(material),
            -1});
    }

    lod.textures.assign(tex_set.begin(), tex_set.end());
    lod.materials.assign(mat_set.begin(), mat_set.end());

    // Read TAGGs
    read_mlod_taggs(r, lod);

    // Resolution (float32) at the very end of the LOD
    auto res = read_f32(r);
    lod.resolution = res;
    lod.resolution_name = resolution_name(res);

    return lod;
}

static P3DFile read_mlod(std::istream& r) {
    auto version = read_u32(r);
    auto lod_count = read_u32(r);
    if (lod_count > 1000)
        throw std::runtime_error(
            std::format("mlod: invalid lodCount: {}", lod_count));

    std::vector<LOD> lods(lod_count);
    for (uint32_t i = 0; i < lod_count; ++i) {
        lods[i] = read_mlod_lod(r);
        lods[i].index = static_cast<int>(i);
    }

    P3DFile result;
    result.format = "MLOD";
    result.version = static_cast<int>(version);
    result.lods = std::move(lods);
    // model_info remains nullptr for MLOD
    return result;
}

// ---------------------------------------------------------------------------
// ODOL v28-75 context and helpers
// ---------------------------------------------------------------------------

struct Odol28Ctx {
    std::istream& r;
    uint32_t version;
    bool use_lzo;  // v44+
    bool use_flag; // v64+

    // Read compressed data (LZO or LZSS based on version).
    std::vector<uint8_t> read_compressed(size_t expected_size) {
        if (expected_size == 0)
            return {};
        if (use_lzo) {
            bool compressed = expected_size >= 1024;
            if (use_flag) {
                auto flag = read_u8(r);
                compressed = flag != 0;
            }
            if (!compressed)
                return read_bytes(r, expected_size);
            return lzo::decompress(r, expected_size);
        }
        // LZSS (v28-43)
        return lzss::decompress_or_raw(r, expected_size);
    }

    // Skip a count-prefixed compressed array. Returns element count.
    int32_t skip_compressed_array(int elem_size) {
        auto count = read_i32(r);
        if (count <= 0)
            return count;
        read_compressed(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
        return count;
    }

    // Skip a condensed array (defaultFill or compressed). Returns element count.
    int32_t skip_condensed_array(int elem_size) {
        auto count = read_i32(r);
        auto fill = read_u8(r);
        if (fill != 0) {
            r.seekg(static_cast<std::streamoff>(elem_size), std::ios::cur);
            return count;
        }
        if (count <= 0)
            return count;
        read_compressed(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
        return count;
    }

    // Skip a compressed vertex index array.
    void skip_compressed_vertex_index_array() {
        int elem_size = (version >= 69) ? 4 : 2;
        skip_compressed_array(elem_size);
    }

    // Read a compressed vertex index array.
    std::vector<uint32_t> read_compressed_vertex_index_array() {
        int elem_size = (version >= 69) ? 4 : 2;
        auto count = read_i32(r);
        if (count <= 0)
            return {};
        auto data = read_compressed(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
        std::vector<uint32_t> result(static_cast<size_t>(count));
        if (elem_size == 4) {
            for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
                uint32_t val;
                std::memcpy(&val, data.data() + i * 4, 4);
                result[i] = val;
            }
        } else {
            for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
                uint16_t val;
                std::memcpy(&val, data.data() + i * 2, 2);
                result[i] = static_cast<uint32_t>(val);
            }
        }
        return result;
    }

    // Read condensed raw data. Returns (count, data).
    std::pair<int32_t, std::vector<uint8_t>> read_condensed_raw(int elem_size) {
        auto count = read_i32(r);
        auto fill = read_u8(r);
        if (count <= 0)
            return {count, {}};
        if (fill != 0) {
            auto def = read_bytes(r, static_cast<size_t>(elem_size));
            std::vector<uint8_t> out(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
            for (size_t i = 0; i < static_cast<size_t>(count); ++i)
                std::memcpy(out.data() + i * static_cast<size_t>(elem_size),
                            def.data(), static_cast<size_t>(elem_size));
            return {count, std::move(out)};
        }
        auto data = read_compressed(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
        return {count, std::move(data)};
    }

    // Read a UV set.
    std::vector<UV> read_uv_set(int elem_size) {
        bool discretized = version >= 45;
        float min_u = 0, min_v = 0, max_u = 0, max_v = 0;
        if (discretized) {
            min_u = read_f32(r);
            min_v = read_f32(r);
            max_u = read_f32(r);
            max_v = read_f32(r);
        }

        auto count = read_i32(r);
        auto fill = read_u8(r);
        if (count <= 0)
            return {};

        std::vector<uint8_t> data;
        if (fill != 0) {
            data = read_bytes(r, static_cast<size_t>(elem_size));
        } else {
            data = read_compressed(static_cast<size_t>(count) * static_cast<size_t>(elem_size));
        }

        std::vector<UV> uvs(static_cast<size_t>(count));
        if (discretized) {
            double scale_u = static_cast<double>(max_u - min_u);
            double scale_v = static_cast<double>(max_v - min_v);
            constexpr double factor = 1.52587890625e-05;
            if (fill != 0) {
                int16_t su, sv;
                std::memcpy(&su, data.data(), 2);
                std::memcpy(&sv, data.data() + 2, 2);
                float u = static_cast<float>(factor * static_cast<double>(static_cast<int>(su) + 32767) * scale_u) + min_u;
                float v = static_cast<float>(factor * static_cast<double>(static_cast<int>(sv) + 32767) * scale_v) + min_v;
                for (size_t i = 0; i < static_cast<size_t>(count); ++i)
                    uvs[i] = {u, v};
            } else {
                for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
                    auto off = static_cast<size_t>(i) * 4;
                    int16_t su, sv;
                    std::memcpy(&su, data.data() + off, 2);
                    std::memcpy(&sv, data.data() + off + 2, 2);
                    uvs[i] = {
                        static_cast<float>(factor * static_cast<double>(static_cast<int>(su) + 32767) * scale_u) + min_u,
                        static_cast<float>(factor * static_cast<double>(static_cast<int>(sv) + 32767) * scale_v) + min_v};
                }
            }
            return uvs;
        }

        // Non-discretized (float UV)
        if (fill != 0) {
            float u, v;
            std::memcpy(&u, data.data(), 4);
            std::memcpy(&v, data.data() + 4, 4);
            for (size_t i = 0; i < static_cast<size_t>(count); ++i)
                uvs[i] = {u, v};
            return uvs;
        }

        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            auto off = i * 8;
            float u, v;
            std::memcpy(&u, data.data() + off, 4);
            std::memcpy(&v, data.data() + off + 4, 4);
            uvs[i] = {u, v};
        }
        return uvs;
    }

    // Skip a skeleton structure.
    void skip_skeleton() {
        auto name = read_asciiz(r);
        if (name.empty())
            return;
        // v23+: isDiscrete (bool)
        if (version >= 23)
            r.seekg(1, std::ios::cur);
        // nBones
        auto n_bones = read_i32(r);
        for (int32_t i = 0; i < n_bones; ++i) {
            read_asciiz(r); // bone name
            read_asciiz(r); // parent bone name
        }
        // v41+: pivotsNameObsolete (asciiz)
        if (version >= 41)
            read_asciiz(r);
    }

    // Skip animations block.
    void skip_animations() {
        auto v = version;
        auto n_classes = read_i32(r);

        std::vector<uint32_t> anim_types(static_cast<size_t>(n_classes));
        for (size_t i = 0; i < static_cast<size_t>(n_classes); ++i) {
            auto anim_type = read_u32(r);
            anim_types[i] = anim_type;
            read_asciiz(r); // animName
            read_asciiz(r); // animSource
            // minPhase, maxPhase, minValue, maxValue (4 x float32)
            r.seekg(16, std::ios::cur);
            // v56+: animPeriod, initPhase (2 x float32)
            if (v >= 56)
                r.seekg(8, std::ios::cur);
            // sourceAddress (uint32)
            r.seekg(4, std::ios::cur);
            // Type-specific data
            switch (anim_type) {
            case 0: case 1: case 2: case 3: // Rotation: 2 floats
                r.seekg(8, std::ios::cur);
                break;
            case 4: case 5: case 6: case 7: // Translation: 2 floats
                r.seekg(8, std::ios::cur);
                break;
            case 8: // Direct: 2xVec3 + 2 floats = 32 bytes
                r.seekg(32, std::ios::cur);
                break;
            case 9: { // Hide: 1 float (+1 for v55+)
                auto skip = (v >= 55) ? 8 : 4;
                r.seekg(skip, std::ios::cur);
                break;
            }
            default:
                throw std::runtime_error(
                    std::format("odol28: unknown AnimType {} at anim class {}", anim_type, i));
            }
        }

        // nAnimLODs
        auto n_anim_lods = read_i32(r);

        // Bones2Anims
        for (int32_t i = 0; i < n_anim_lods; ++i) {
            auto n_bones = read_u32(r);
            for (uint32_t j = 0; j < n_bones; ++j) {
                auto n_anims = read_u32(r);
                r.seekg(static_cast<std::streamoff>(n_anims) * 4, std::ios::cur);
            }
        }

        // Anims2Bones
        for (int32_t i = 0; i < n_anim_lods; ++i) {
            for (int32_t m = 0; m < n_classes; ++m) {
                auto bone_index = read_i32(r);
                if (bone_index != -1 && anim_types[static_cast<size_t>(m)] != 8 && anim_types[static_cast<size_t>(m)] != 9) {
                    // axisPos + axisDir = 24 bytes
                    r.seekg(24, std::ios::cur);
                }
            }
        }
    }

    // Skip LoadableLodInfo for non-permanent LODs.
    void skip_loadable_lod_info() {
        // nFaces(i32), color(u32), special(i32), orHints(u32)
        r.seekg(16, std::ios::cur);
        // v39+: hasSkeleton (bool)
        if (version >= 39)
            r.seekg(1, std::ios::cur);
        // v51+: nVertices(i32), faceArea(float)
        if (version >= 51)
            r.seekg(8, std::ios::cur);
    }

    // Read ModelInfo structure.
    std::unique_ptr<ModelInfo> read_model_info(int n_lods) {
        auto v = version;
        auto info = std::make_unique<ModelInfo>();

        // special (int32)
        r.seekg(4, std::ios::cur);

        // BoundingSphere, GeometrySphere
        info->bounding_sphere = read_f32(r);
        r.seekg(4, std::ios::cur); // GeometrySphere

        // remarks, andHints, orHints (3 x int32 = 12 bytes)
        r.seekg(12, std::ios::cur);

        // AimingCenter (Vector3P = 12 bytes)
        r.seekg(12, std::ios::cur);

        // color (uint32), colorType (uint32)
        r.seekg(8, std::ios::cur);

        // viewDensity (float)
        r.seekg(4, std::ios::cur);

        // bboxMin (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            info->bounding_box_min[j] = read_f32(r);
        // bboxMax (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            info->bounding_box_max[j] = read_f32(r);

        // v70+: lodDensityCoef (float)
        if (v >= 70) r.seekg(4, std::ios::cur);
        // v71+: drawImportance (float)
        if (v >= 71) r.seekg(4, std::ios::cur);
        // v52+: visual bounds (2 x Vector3P = 24 bytes)
        if (v >= 52) r.seekg(24, std::ios::cur);

        // boundingCenter (Vector3P)
        r.seekg(12, std::ios::cur);
        // geometryCenter (Vector3P)
        r.seekg(12, std::ios::cur);
        // centerOfMass (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            info->center_of_mass[j] = read_f32(r);

        // invInertia (Matrix3P = 36 bytes)
        r.seekg(36, std::ios::cur);

        // autoCenter, lockAutoCenter, canOcclude, canBeOccluded (4 bools)
        r.seekg(4, std::ios::cur);
        // v73+: AICovers (bool)
        if (v >= 73) r.seekg(1, std::ios::cur);

        // v42+: thermal profile (4 floats)
        if (v >= 42) r.seekg(16, std::ios::cur);
        // v43+: mFact, tBody (2 floats)
        if (v >= 43) r.seekg(8, std::ios::cur);

        // v33+: forceNotAlphaModel (bool)
        if (v >= 33) r.seekg(1, std::ios::cur);
        // v37+: sbSource(i32) + prefershadowvolume(bool)
        if (v >= 37) r.seekg(5, std::ios::cur);
        // v48+: shadowOffset(float)
        if (v >= 48) r.seekg(4, std::ios::cur);

        // animated (bool)
        r.seekg(1, std::ios::cur);

        // Skeleton
        skip_skeleton();

        // mapType (byte)
        r.seekg(1, std::ios::cur);

        // massArray (compressed float array)
        skip_compressed_array(4);

        // mass (float)
        info->mass = read_f32(r);
        // invMass (float)
        r.seekg(4, std::ios::cur);
        // armor (float)
        info->armor = read_f32(r);
        // invArmor (float)
        r.seekg(4, std::ios::cur);

        // v72+: explosionshielding (float)
        if (v >= 72) r.seekg(4, std::ios::cur);

        // v53+: geometrySimple (byte)
        if (v >= 53) r.seekg(1, std::ios::cur);
        // v54+: geometryPhys (byte)
        if (v >= 54) r.seekg(1, std::ios::cur);

        // LOD indices: 12 bytes
        uint8_t indices[12];
        if (!r.read(reinterpret_cast<char*>(indices), 12))
            throw std::runtime_error("odol28: failed to read LOD indices");
        info->memory_lod         = static_cast<int>(static_cast<int8_t>(indices[0]));
        info->geometry_lod       = static_cast<int>(static_cast<int8_t>(indices[1]));
        info->fire_geometry_lod  = static_cast<int>(static_cast<int8_t>(indices[2]));
        info->view_geometry_lod  = static_cast<int>(static_cast<int8_t>(indices[3]));
        info->land_contact_lod   = static_cast<int>(static_cast<int8_t>(indices[8]));
        info->roadway_lod        = static_cast<int>(static_cast<int8_t>(indices[9]));
        info->paths_lod          = static_cast<int>(static_cast<int8_t>(indices[10]));
        info->hitpoints_lod      = static_cast<int>(static_cast<int8_t>(indices[11]));

        // minShadow (uint32)
        r.seekg(4, std::ios::cur);

        // v38+: canBlend (bool)
        if (v >= 38) r.seekg(1, std::ios::cur);

        // propertyClass (asciiz), propertyDamage (asciiz)
        read_asciiz(r);
        read_asciiz(r);

        // propertyFrequent (bool)
        r.seekg(1, std::ios::cur);

        // v31+: unknown uint32
        if (v >= 31) r.seekg(4, std::ios::cur);

        // v57+: preferred shadow arrays (3 x nLods x int32)
        if (v >= 57)
            r.seekg(static_cast<std::streamoff>(n_lods) * 12, std::ios::cur);

        return info;
    }

    // Read a StageTexture and return the texture path.
    std::string read_stage_texture(uint32_t mat_version) {
        // v5+: textureFilter (uint32)
        if (mat_version >= 5)
            r.seekg(4, std::ios::cur);
        // texture (asciiz)
        auto tex_path = read_asciiz(r);
        // v8+: stageID (uint32)
        if (mat_version >= 8)
            r.seekg(4, std::ios::cur);
        // v11+: useWorldEnvMap (bool)
        if (mat_version >= 11)
            r.seekg(1, std::ios::cur);
        return tex_path;
    }

    // Read an EmbeddedMaterial. Returns (rvmat name, stage texture paths).
    std::pair<std::string, std::vector<std::string>> read_embedded_material() {
        auto material_name = read_asciiz(r);
        auto mat_version = read_u32(r);

        // 6 x D3DCOLORVALUE = 96 bytes
        r.seekg(96, std::ios::cur);

        // specularPower (float)
        r.seekg(4, std::ios::cur);

        // pixelShader(u32), vertexShader(u32), mainLight(u32), fogMode(u32)
        r.seekg(16, std::ios::cur);

        // matVersion == 3: extra bool
        if (mat_version == 3)
            r.seekg(1, std::ios::cur);

        // v6+: surfaceFile (asciiz)
        if (mat_version >= 6)
            read_asciiz(r);

        // v4+: nRenderFlags(u32), renderFlags(u32)
        if (mat_version >= 4)
            r.seekg(8, std::ios::cur);

        uint32_t n_stages = 0, n_tex_gens = 0;
        // v7+: nStages
        if (mat_version > 6)
            n_stages = read_u32(r);
        // v9+: nTexGens
        if (mat_version > 8)
            n_tex_gens = read_u32(r);

        std::vector<std::string> stage_textures;

        if (mat_version < 8) {
            // Interleaved: transform then texture
            for (uint32_t i = 0; i < n_stages; ++i) {
                // StageTransform: uvSource(u32) + 12 floats = 52 bytes
                r.seekg(52, std::ios::cur);
                auto tex = read_stage_texture(mat_version);
                if (!tex.empty())
                    stage_textures.push_back(std::move(tex));
            }
        } else {
            // Textures first, then transforms
            for (uint32_t i = 0; i < n_stages; ++i) {
                auto tex = read_stage_texture(mat_version);
                if (!tex.empty())
                    stage_textures.push_back(std::move(tex));
            }
            for (uint32_t i = 0; i < n_tex_gens; ++i) {
                // StageTransform: 52 bytes
                r.seekg(52, std::ios::cur);
            }
        }

        // v10+: stageTI (StageTexture)
        if (mat_version >= 10) {
            auto tex = read_stage_texture(mat_version);
            if (!tex.empty())
                stage_textures.push_back(std::move(tex));
        }

        return {std::move(material_name), std::move(stage_textures)};
    }

    // Section data holder.
    struct Section28 {
        int32_t face_lower_index = 0;
        int32_t face_upper_index = 0;
        int16_t texture_index = 0;
        int32_t material_index = -1;
        std::string material_inline;
    };

    // Read a Section structure.
    Section28 read_section() {
        auto v = version;
        Section28 s;

        s.face_lower_index = read_i32(r);
        s.face_upper_index = read_i32(r);
        // minBoneIndex(i32), bonesCount(i32)
        r.seekg(8, std::ios::cur);
        // skip(u32)
        r.seekg(4, std::ios::cur);
        // textureIndex(i16)
        int16_t tex_idx;
        if (!r.read(reinterpret_cast<char*>(&tex_idx), 2))
            throw std::runtime_error("odol28: failed to read textureIndex");
        s.texture_index = tex_idx;
        // special(u32)
        r.seekg(4, std::ios::cur);
        // materialIndex(i32)
        auto mat_idx = read_i32(r);
        s.material_index = mat_idx;
        if (mat_idx == -1)
            s.material_inline = read_asciiz(r); // mat (asciiz)

        // v36+: nStages(u32) + areaOverTex(nStages x float)
        if (v >= 36) {
            auto n_stages = read_u32(r);
            r.seekg(static_cast<std::streamoff>(n_stages) * 4, std::ios::cur);
            // v67+: extra data
            if (v >= 67) {
                auto count = read_i32(r);
                if (count >= 1)
                    r.seekg(44, std::ios::cur); // 11 floats
            }
        } else {
            // areaOverTex (1 float)
            r.seekg(4, std::ios::cur);
        }
        return s;
    }

    // Read a NamedSelection and return its name with selected faces / vertices.
    struct NamedSelectionRecord {
        std::string name;
        std::vector<uint32_t> faces;
        std::vector<uint32_t> vertices;
    };
    NamedSelectionRecord read_named_selection() {
        auto name = read_asciiz(r);

        // SelectedFaces (compressed index array)
        auto selected_faces = read_compressed_vertex_index_array();

        // skip(int32)
        r.seekg(4, std::ios::cur);

        // IsSectional (bool)
        r.seekg(1, std::ios::cur);

        // Sections (compressed int32 array)
        skip_compressed_array(4);

        // SelectedVertices (compressed vertex index array)
        auto selected_vertices = read_compressed_vertex_index_array();

        // SelectedVerticesWeights: count(int32) + compressed(count bytes)
        auto expected_size = read_i32(r);
        if (expected_size > 0) {
            auto weights = read_compressed(static_cast<size_t>(expected_size));
            if (!selected_vertices.empty() &&
                weights.size() >= selected_vertices.size()) {
                std::vector<uint32_t> weighted_vertices;
                weighted_vertices.reserve(selected_vertices.size());
                for (size_t i = 0; i < selected_vertices.size(); ++i) {
                    if (weights[i] != 0)
                        weighted_vertices.push_back(selected_vertices[i]);
                }
                selected_vertices = std::move(weighted_vertices);
            }
        }

        return {std::move(name), std::move(selected_faces),
                std::move(selected_vertices)};
    }

    // Read a single ODOL v28+ LOD.
    LOD read_lod() {
        LOD lod;
        auto v = version;

        // Proxies
        auto n_proxies = read_i32(r);
        for (int32_t i = 0; i < n_proxies; ++i) {
            read_asciiz(r); // proxyModel
            // Matrix4P = 48 bytes
            r.seekg(48, std::ios::cur);
            // sequenceID(i32), namedSelectionIndex(i32), boneIndex(i32)
            r.seekg(12, std::ios::cur);
            // v40+: sectionIndex(i32)
            if (v >= 40)
                r.seekg(4, std::ios::cur);
        }

        // subSkeletonsToSkeleton
        auto n_sub_skel_map = read_i32(r);
        if (n_sub_skel_map > 0)
            r.seekg(static_cast<std::streamoff>(n_sub_skel_map) * 4, std::ios::cur);

        // skeletonToSubSkeleton
        auto n_skel_to_sub = read_i32(r);
        for (int32_t i = 0; i < n_skel_to_sub; ++i) {
            auto inner_count = read_i32(r);
            if (inner_count > 0)
                r.seekg(static_cast<std::streamoff>(inner_count) * 4, std::ios::cur);
        }

        // Clip flags / vertex count
        if (v >= 50) {
            auto vertex_count = read_u32(r);
            lod.vertex_count = static_cast<int>(vertex_count);
        } else {
            skip_condensed_array(4);
        }

        // v51+: faceArea (float)
        if (v >= 51) r.seekg(4, std::ios::cur);

        // orHints(i32), andHints(i32)
        r.seekg(8, std::ios::cur);

        // bMin (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            lod.bounding_box_min[j] = read_f32(r);
        // bMax (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            lod.bounding_box_max[j] = read_f32(r);

        // bCenter (Vector3P)
        for (size_t j = 0; j < 3; ++j)
            lod.bounding_center[j] = read_f32(r);

        // bRadius (float)
        lod.bounding_radius = read_f32(r);

        // textures (string array) -- keep raw for section textureIndex lookup
        auto raw_textures = read_string_array(r);
        for (auto& t : raw_textures) {
            if (!t.empty())
                lod.textures.push_back(t);
        }

        // Materials (EmbeddedMaterial array)
        auto n_materials = read_i32(r);
        std::vector<std::string> raw_materials(static_cast<size_t>(std::max(0, n_materials)));
        std::set<std::string> mat_tex_seen;
        for (int32_t i = 0; i < n_materials; ++i) {
            auto [mat_name, stage_tex] = read_embedded_material();
            raw_materials[static_cast<size_t>(i)] = mat_name;
            if (!mat_name.empty())
                lod.materials.push_back(mat_name);
            for (auto& t : stage_tex) {
                std::string key = t;
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (!mat_tex_seen.contains(key)) {
                    mat_tex_seen.insert(key);
                    lod.textures.push_back(std::move(t));
                }
            }
        }

        // pointToVertex -- skip
        skip_compressed_vertex_index_array();
        // vertexToPoint -- read for position expansion
        auto vertex_to_point = read_compressed_vertex_index_array();

        // Polygons: nFaces(u32), skip(u32), skip(u16)
        auto n_faces = read_u32(r);
        lod.face_count = static_cast<int>(n_faces);
        r.seekg(6, std::ios::cur); // skip + skip16

        // Read faces
        int32_t index_size = (v >= 69) ? 4 : 2;
        lod.faces.reserve(n_faces);
        std::vector<int32_t> face_byte_offsets;
        face_byte_offsets.reserve(n_faces);
        int32_t face_data_offset = 0;
        for (uint32_t fi = 0; fi < n_faces; ++fi) {
            face_byte_offsets.push_back(face_data_offset);
            auto n = read_u8(r);
            face_data_offset += index_size * (1 + static_cast<int32_t>(n));
            std::vector<uint32_t> indices(n);
            if (v >= 69) {
                for (uint8_t j = 0; j < n; ++j)
                    indices[j] = read_u32(r);
            } else {
                for (uint8_t j = 0; j < n; ++j)
                    indices[j] = static_cast<uint32_t>(read_u16(r));
            }
            lod.faces.push_back(std::move(indices));
        }

        // Sections
        auto n_sections = read_i32(r);
        std::vector<Section28> sections(static_cast<size_t>(n_sections));
        for (int32_t i = 0; i < n_sections; ++i)
            sections[static_cast<size_t>(i)] = read_section();

        // NamedSelections
        auto n_selections = read_i32(r);
        lod.named_selections.resize(static_cast<size_t>(n_selections));
        for (int32_t i = 0; i < n_selections; ++i) {
            auto record = read_named_selection();
            auto& name = record.name;
            lod.named_selections[static_cast<size_t>(i)] = name;
            if (!record.vertices.empty()) {
                auto& target = lod.named_selection_vertices[name];
                target.insert(target.end(), record.vertices.begin(), record.vertices.end());
                std::sort(target.begin(), target.end());
                target.erase(std::unique(target.begin(), target.end()), target.end());
            }
            if (!record.faces.empty()) {
                auto& target_faces = lod.named_selection_faces[name];
                target_faces.insert(target_faces.end(), record.faces.begin(), record.faces.end());
                std::sort(target_faces.begin(), target_faces.end());
                target_faces.erase(std::unique(target_faces.begin(), target_faces.end()),
                                   target_faces.end());
            }
        }

        // NamedProperties
        auto n_props = read_u32(r);
        lod.named_properties.resize(n_props);
        for (uint32_t i = 0; i < n_props; ++i) {
            lod.named_properties[i].name = read_asciiz(r);
            lod.named_properties[i].value = read_asciiz(r);
        }

        // Frames (Keyframes)
        auto n_frames = read_i32(r);
        for (int32_t i = 0; i < n_frames; ++i) {
            r.seekg(4, std::ios::cur); // time
            auto n_pts = read_u32(r);
            r.seekg(static_cast<std::streamoff>(n_pts) * 12, std::ios::cur);
        }

        // colorTop(i32), color_(i32), special(i32)
        r.seekg(12, std::ios::cur);

        // vertexBoneRefIsSimple(bool), sizeOfRestData(u32)
        r.seekg(5, std::ios::cur);

        // v50+: clip flags (condensed int32 array)
        if (v >= 50)
            skip_condensed_array(4);

        // UVSets
        int uv_elem_size = (v >= 45) ? 4 : 8;
        // First UV set
        auto first_uv = read_uv_set(uv_elem_size);
        auto n_uv_sets = read_u32(r);
        if (n_uv_sets > 0) {
            lod.uv_sets.reserve(n_uv_sets);
            lod.uv_sets.push_back(std::move(first_uv));
        } else if (!first_uv.empty()) {
            lod.uv_sets.push_back(std::move(first_uv));
        }
        for (uint32_t i = 1; i < n_uv_sets; ++i)
            lod.uv_sets.push_back(read_uv_set(uv_elem_size));

        // Vertices (compressed)
        auto n_verts = read_i32(r);
        if (lod.vertex_count == 0)
            lod.vertex_count = static_cast<int>(n_verts);
        auto vert_data = read_compressed(static_cast<size_t>(n_verts) * 12);
        if (n_verts > 0 && !vert_data.empty()) {
            std::vector<Vector3P> points(static_cast<size_t>(n_verts));
            for (int32_t i = 0; i < n_verts; ++i) {
                auto off = static_cast<size_t>(i) * 12;
                std::memcpy(&points[static_cast<size_t>(i)][0], vert_data.data() + off, 4);
                std::memcpy(&points[static_cast<size_t>(i)][1], vert_data.data() + off + 4, 4);
                std::memcpy(&points[static_cast<size_t>(i)][2], vert_data.data() + off + 8, 4);
            }
            // Expand points to per-vertex positions using vertexToPoint mapping
            if (!vertex_to_point.empty()) {
                lod.vertices.resize(vertex_to_point.size());
                for (size_t vi = 0; vi < vertex_to_point.size(); ++vi) {
                    auto pi = vertex_to_point[vi];
                    if (pi < static_cast<uint32_t>(points.size()))
                        lod.vertices[vi] = points[pi];
                }
                lod.vertex_count = static_cast<int>(vertex_to_point.size());
            } else {
                lod.vertices = std::move(points);
            }
        }

        // Normals (condensed)
        int normal_elem_size = (v >= 45) ? 4 : 12;
        auto [normal_count, normal_data] = read_condensed_raw(normal_elem_size);
        if (!normal_data.empty()) {
            if (v >= 45) {
                auto n = normal_data.size() / 4;
                lod.normals.resize(n);
                constexpr double scale_factor = -0.0019569471;
                for (size_t i = 0; i < n; ++i) {
                    int32_t packed;
                    std::memcpy(&packed, normal_data.data() + i * 4, 4);
                    int x = packed & 0x3FF;
                    int y = (packed >> 10) & 0x3FF;
                    int z = (packed >> 20) & 0x3FF;
                    if (x > 511) x -= 1024;
                    if (y > 511) y -= 1024;
                    if (z > 511) z -= 1024;
                    lod.normals[i] = {
                        static_cast<float>(static_cast<double>(x) * scale_factor),
                        static_cast<float>(static_cast<double>(y) * scale_factor),
                        static_cast<float>(static_cast<double>(z) * scale_factor)};
                }
            } else {
                auto n = normal_data.size() / 12;
                lod.normals.resize(n);
                for (size_t i = 0; i < n; ++i) {
                    auto off = i * 12;
                    std::memcpy(&lod.normals[i][0], normal_data.data() + off, 4);
                    std::memcpy(&lod.normals[i][1], normal_data.data() + off + 4, 4);
                    std::memcpy(&lod.normals[i][2], normal_data.data() + off + 8, 4);
                }
            }
        }

        // STCoords (compressed)
        int st_elem_size = (v >= 45) ? 8 : 24;
        auto n_st = read_i32(r);
        if (n_st > 0)
            read_compressed(static_cast<size_t>(n_st) * static_cast<size_t>(st_elem_size));

        // VertexBoneRef (compressed, 12 bytes each)
        auto n_bone_ref = read_i32(r);
        if (n_bone_ref > 0)
            read_compressed(static_cast<size_t>(n_bone_ref) * 12);

        // NeighborBoneRef (compressed, 32 bytes each)
        auto n_neighbor = read_i32(r);
        if (n_neighbor > 0)
            read_compressed(static_cast<size_t>(n_neighbor) * 32);

        // v67+: unknown uint32
        if (v >= 67) r.seekg(4, std::ios::cur);
        // v68+: unknown byte
        if (v >= 68) r.seekg(1, std::ios::cur);

        // Build per-face vertex data from index faces and vertex-level UV/normal arrays
        lod.face_data.reserve(lod.faces.size());
        for (size_t face_idx = 0; face_idx < lod.faces.size(); ++face_idx) {
            auto& face = lod.faces[face_idx];
            std::vector<FaceVertex> verts(face.size());
            for (size_t i = 0; i < face.size(); ++i) {
                auto vert_idx = face[i];
                int32_t normal_idx_val = -1;
                if (vert_idx < lod.normals.size())
                    normal_idx_val = static_cast<int32_t>(vert_idx);
                UV uv = {0.0f, 0.0f};
                if (!lod.uv_sets.empty()) {
                    if (vert_idx < lod.uv_sets[0].size()) {
                        uv = lod.uv_sets[0][vert_idx];
                    } else if (vert_idx < vertex_to_point.size()) {
                        auto pi = vertex_to_point[vert_idx];
                        if (pi < lod.uv_sets[0].size())
                            uv = lod.uv_sets[0][pi];
                    }
                }
                verts[i] = FaceVertex{vert_idx, normal_idx_val, uv};
            }
            // Find texture for this face from sections using byte offsets
            std::string texture;
            std::string material;
            int32_t tex_idx = -1;
            auto byte_off = face_byte_offsets[face_idx];
            for (auto& s : sections) {
                if (byte_off >= s.face_lower_index && byte_off < s.face_upper_index) {
                    tex_idx = static_cast<int32_t>(s.texture_index);
                    if (s.texture_index >= 0 &&
                        static_cast<size_t>(s.texture_index) < raw_textures.size())
                        texture = raw_textures[static_cast<size_t>(s.texture_index)];
                    if (s.material_index >= 0 &&
                        static_cast<size_t>(s.material_index) < raw_materials.size()) {
                        material = raw_materials[static_cast<size_t>(s.material_index)];
                    } else if (!s.material_inline.empty()) {
                        material = s.material_inline;
                    }
                    break;
                }
            }
            lod.face_data.push_back(Face{
                std::move(verts), 0, std::move(texture), std::move(material), tex_idx});
        }

        const uint32_t max_vertex_index = !lod.vertices.empty()
            ? static_cast<uint32_t>(lod.vertices.size())
            : static_cast<uint32_t>(std::max(lod.vertex_count, 0));
        if (max_vertex_index > 0) {
            for (auto& [_, indices] : lod.named_selection_vertices) {
                indices.erase(std::remove_if(indices.begin(), indices.end(),
                                             [max_vertex_index](uint32_t idx) {
                                                 return idx >= max_vertex_index;
                                             }),
                              indices.end());
            }
        }
        const uint32_t max_face_index = static_cast<uint32_t>(lod.faces.size());
        if (max_face_index > 0) {
            for (auto& [_, indices] : lod.named_selection_faces) {
                indices.erase(std::remove_if(indices.begin(), indices.end(),
                                             [max_face_index](uint32_t idx) {
                                                 return idx >= max_face_index;
                                             }),
                              indices.end());
            }
        }

        return lod;
    }
};

// ---------------------------------------------------------------------------
// ODOL v28-75 top-level reader
// ---------------------------------------------------------------------------

static P3DFile read_odol28(std::istream& r, uint32_t version) {
    Odol28Ctx ctx{r, version, version >= 44, version >= 64};

    // Header fields after version
    if (version >= 59)
        read_u32(r); // appID
    if (version >= 74)
        r.seekg(8, std::ios::cur); // two unknown uint32s
    if (version >= 58)
        read_asciiz(r); // muzzleFlash

    // nLods + resolutions
    auto n_lods = read_i32(r);
    if (n_lods < 0 || n_lods > 1000)
        throw std::runtime_error(
            std::format("odol28: invalid nLods: {}", n_lods));
    auto resolutions = read_f32_slice(r, static_cast<size_t>(n_lods));

    // ModelInfo
    auto info = ctx.read_model_info(n_lods);

    // Animations block (v30+)
    if (version >= 30) {
        auto has_anims = read_u8(r);
        if (has_anims != 0)
            ctx.skip_animations();
    }

    // LOD start/end addresses and permanent flags
    auto lod_starts = read_u32_slice(r, static_cast<size_t>(n_lods));
    auto lod_ends = read_u32_slice(r, static_cast<size_t>(n_lods));
    std::vector<uint8_t> permanent(static_cast<size_t>(n_lods));
    if (n_lods > 0 &&
        !r.read(reinterpret_cast<char*>(permanent.data()), static_cast<std::streamsize>(n_lods)))
        throw std::runtime_error("odol28: failed to read permanent flags");

    // Read LODs via address tables
    std::vector<LOD> lods(static_cast<size_t>(n_lods));
    auto cur_pos = r.tellg();

    for (int32_t i = 0; i < n_lods; ++i) {
        if (permanent[static_cast<size_t>(i)] == 0) {
            ctx.skip_loadable_lod_info();
            cur_pos = r.tellg();
        }

        // Seek to LOD start
        r.seekg(static_cast<std::streamoff>(lod_starts[static_cast<size_t>(i)]), std::ios::beg);

        lods[static_cast<size_t>(i)] = ctx.read_lod();
        lods[static_cast<size_t>(i)].index = static_cast<int>(i);
        lods[static_cast<size_t>(i)].resolution = resolutions[static_cast<size_t>(i)];
        lods[static_cast<size_t>(i)].resolution_name = resolution_name(resolutions[static_cast<size_t>(i)]);

        // Restore position after LOD
        r.seekg(cur_pos, std::ios::beg);
    }

    P3DFile result;
    result.format = "ODOL";
    result.version = static_cast<int>(version);
    result.lods = std::move(lods);
    result.model_info = std::move(info);
    return result;
}

// ---------------------------------------------------------------------------
// SizeInfo helpers
// ---------------------------------------------------------------------------

static SizeInfo size_from_lod(const LOD& lod, const std::string& source) {
    auto center = lod.bounding_center;
    auto radius = lod.bounding_radius;

    // If center/radius are zero (e.g. MLOD), compute from bounding box
    if (center == Vector3P{0.0f, 0.0f, 0.0f} && radius == 0.0f) {
        for (size_t i = 0; i < 3; ++i)
            center[i] = (lod.bounding_box_min[i] + lod.bounding_box_max[i]) / 2.0f;
        float dx = lod.bounding_box_max[0] - center[0];
        float dy = lod.bounding_box_max[1] - center[1];
        float dz = lod.bounding_box_max[2] - center[2];
        radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    return SizeInfo{
        source,
        lod.bounding_box_min,
        lod.bounding_box_max,
        center,
        radius,
        {lod.bounding_box_max[0] - lod.bounding_box_min[0],
         lod.bounding_box_max[1] - lod.bounding_box_min[1],
         lod.bounding_box_max[2] - lod.bounding_box_min[2]}};
}

static std::optional<SizeInfo> size_from_vertices(const LOD& lod) {
    if (lod.vertices.empty())
        return std::nullopt;

    auto bmin = lod.vertices[0];
    auto bmax = lod.vertices[0];
    for (size_t i = 1; i < lod.vertices.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) {
            if (lod.vertices[i][j] < bmin[j])
                bmin[j] = lod.vertices[i][j];
            if (lod.vertices[i][j] > bmax[j])
                bmax[j] = lod.vertices[i][j];
        }
    }

    Vector3P center;
    for (size_t i = 0; i < 3; ++i)
        center[i] = (bmin[i] + bmax[i]) / 2.0f;
    float dx = bmax[0] - center[0];
    float dy = bmax[1] - center[1];
    float dz = bmax[2] - center[2];
    float radius = std::sqrt(dx * dx + dy * dy + dz * dz);

    return SizeInfo{
        lod.resolution_name,
        bmin,
        bmax,
        center,
        radius,
        {bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2]}};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string resolution_name(float r) {
    uint32_t bits;
    std::memcpy(&bits, &r, 4);

    switch (bits) {
    case 0x551184e7: return "Geometry";
    case 0x58635fa9: return "Memory";
    case 0x58e35fa9: return "LandContact";
    case 0x592a87bf: return "Roadway";
    case 0x59635fa9: return "Paths";
    case 0x598e1bca: return "HitPoints";
    case 0x59aa87bf: return "ViewGeometry";
    case 0x59c6f3b4: return "FireGeometry";
    case 0x59e35fa9: return "ViewCargoGeometry";
    case 0x59ffcb9e: return "ViewCargoFireGeometry";
    case 0x5a0e1bca: return "ViewCommander";
    case 0x5a1c51c4: return "ViewCommanderGeometry";
    case 0x5a2a87bf: return "ViewCommanderFireGeometry";
    case 0x5a38bdb9: return "ViewPilotGeometry";
    case 0x5a46f3b4: return "ViewPilotFireGeometry";
    case 0x5a5529af: return "ViewGunnerGeometry";
    case 0x5a635fa9: return "ViewGunnerFireGeometry";
    default:
        break;
    }

    // ShadowVolume range: 1e4 <= r < 2e4
    if (r >= 1e4f && r < 2e4f)
        return std::format("ShadowVolume {:.0f}", r - 1e4f);

    // Buoyancy, PhysX, Wreck -- Arma 3 additions
    switch (bits) {
    case 0x559184e7: return "Buoyancy";  // 2e13
    case 0x561184e7: return "PhysX";     // 4e13
    case 0x5a9536c7: return "Wreck";     // 2.1e16
    default:
        break;
    }

    return std::format("{:.3f}", r);
}

P3DFile read(std::istream& r) {
    auto sig = binutil::read_signature(r);

    if (sig == "ODOL") {
        auto version = binutil::read_u32(r);
        if (version >= 28)
            return read_odol28(r, version);
        return read_odol(r, version);
    }
    if (sig == "MLOD") {
        return read_mlod(r);
    }

    // Check for LZSS-compressed P3D (OFP-era PBO entries extracted raw).
    // The first LZSS flag byte is often 0xFF with "ODOL" or "MLOD" as literals.
    if (static_cast<uint8_t>(sig[0]) != 0 &&
        (sig.substr(1, 3) == "ODO" || sig.substr(1, 3) == "MLO")) {
        // Likely LZSS-compressed: read entire file, decompress with auto-size
        r.seekg(0, std::ios::end);
        auto file_size = static_cast<size_t>(r.tellg());
        r.seekg(0, std::ios::beg);
        auto compressed = binutil::read_bytes(r, file_size);

        auto decompressed = lzss::decompress_buf_auto(
            compressed.data(), compressed.size());
        if (decompressed.empty())
            throw std::runtime_error(
                "p3d: file appears LZSS-compressed but decompression failed");

        std::string decompressed_str(
            reinterpret_cast<const char*>(decompressed.data()),
            decompressed.size());
        std::istringstream dec_stream(decompressed_str);
        return read(dec_stream);
    }

    throw std::runtime_error(
        std::format("p3d: not a P3D file (signature \"{}\")", sig));
}

CalculateSizeResult calculate_size(const P3DFile& model) {
    // Try Geometry LOD first
    for (auto& lod : model.lods) {
        if (lod.resolution_name == "Geometry")
            return {size_from_lod(lod, "Geometry"), ""};
    }

    // No Geometry LOD -- try visual LODs
    int best_idx = -1;
    float best_res = std::numeric_limits<float>::max();
    for (size_t i = 0; i < model.lods.size(); ++i) {
        auto& l = model.lods[i];
        if (!is_visual_lod(l.resolution_name) || l.vertex_count == 0)
            continue;
        if (l.resolution < best_res) {
            best_res = l.resolution;
            best_idx = static_cast<int>(i);
        }
    }
    if (best_idx >= 0) {
        auto& src = model.lods[static_cast<size_t>(best_idx)].resolution_name;
        return {size_from_lod(model.lods[static_cast<size_t>(best_idx)], src),
                "no Geometry LOD found, using visual LOD " + src};
    }

    return {std::nullopt,
            "no Geometry or visual LODs found, cannot calculate size"};
}

std::optional<SizeInfo> visual_bbox(const P3DFile& model) {
    const LOD* lod = nullptr;

    // Try LOD 1.000 first (highest detail visual)
    for (auto& l : model.lods) {
        if (l.resolution_name == "1.000" && !l.vertices.empty()) {
            lod = &l;
            break;
        }
    }

    // Fallback to lowest-resolution visual LOD
    if (!lod) {
        int best_idx = -1;
        float best_res = std::numeric_limits<float>::max();
        for (size_t i = 0; i < model.lods.size(); ++i) {
            auto& l = model.lods[i];
            if (!is_visual_lod(l.resolution_name) || l.vertices.empty())
                continue;
            if (l.resolution < best_res) {
                best_res = l.resolution;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_idx >= 0)
            lod = &model.lods[static_cast<size_t>(best_idx)];
    }

    if (!lod)
        return std::nullopt;

    return size_from_vertices(*lod);
}

} // namespace armatools::p3d
