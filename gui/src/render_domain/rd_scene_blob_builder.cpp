#include "render_domain/rd_scene_blob_builder.h"

#include "render_domain/rd_scene_blob.h"

#include <armatools/armapath.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace render_domain {

namespace {

struct GroupData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv0;
    std::vector<uint32_t> indices;
};

struct PackedData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv0;
    std::vector<uint32_t> indices;
    std::vector<rd_scene_mesh_v1> meshes;
    std::vector<rd_scene_material_v1> materials;
    std::vector<std::string> material_texture_keys;
};

float sane(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

std::string normalized_texture_key(std::string_view texture, std::string_view material) {
    auto key = armatools::armapath::to_slash_lower(std::string(texture));
    if (!key.empty()) return key;
    return armatools::armapath::to_slash_lower(std::string(material));
}

template <typename T>
uint32_t append_pod_block(const std::vector<T>& src, std::vector<uint8_t>* dst) {
    if (src.empty()) {
        return 0;
    }
    const uint32_t offset = static_cast<uint32_t>(dst->size());
    const auto* begin = reinterpret_cast<const uint8_t*>(src.data());
    const auto* end = begin + src.size() * sizeof(T);
    dst->insert(dst->end(), begin, end);
    return offset;
}

bool triangulate_lods(const std::vector<armatools::p3d::LOD>& lods,
                      PackedData* out,
                      std::string* error_message) {
    std::unordered_map<std::string, GroupData> grouped;

    for (const auto& lod : lods) {
        for (const auto& face : lod.face_data) {
            if (face.vertices.size() < 3) continue;

            const auto key = normalized_texture_key(face.texture, face.material);
            auto& group = grouped[key];

            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                const size_t tri[3] = {0, i, i + 1};
                for (size_t corner : tri) {
                    const auto& fv = face.vertices[corner];
                    if (fv.point_index >= lod.vertices.size()) {
                        if (error_message) {
                            *error_message = "face references vertex out of range";
                        }
                        return false;
                    }

                    const auto& p = lod.vertices[fv.point_index];
                    group.positions.push_back(-sane(p[0]));
                    group.positions.push_back(sane(p[1]));
                    group.positions.push_back(sane(p[2]));

                    if (fv.normal_index >= 0 &&
                        static_cast<size_t>(fv.normal_index) < lod.normals.size()) {
                        const auto& n = lod.normals[static_cast<size_t>(fv.normal_index)];
                        group.normals.push_back(-sane(n[0]));
                        group.normals.push_back(sane(n[1], 1.0f));
                        group.normals.push_back(sane(n[2]));
                    } else {
                        group.normals.push_back(0.0f);
                        group.normals.push_back(1.0f);
                        group.normals.push_back(0.0f);
                    }

                    group.uv0.push_back(sane(fv.uv[0]));
                    group.uv0.push_back(sane(fv.uv[1]));

                    group.indices.push_back(static_cast<uint32_t>(group.indices.size()));
                }
            }
        }
    }

    std::vector<std::string> keys;
    keys.reserve(grouped.size());
    for (const auto& [key, _] : grouped) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    uint32_t vertex_base = 0;
    uint32_t index_base = 0;
    for (const auto& key : keys) {
        const auto it = grouped.find(key);
        if (it == grouped.end()) continue;
        const auto& group = it->second;

        const uint32_t vertex_count = static_cast<uint32_t>(group.positions.size() / 3);
        const uint32_t index_count = static_cast<uint32_t>(group.indices.size());

        rd_scene_mesh_v1 mesh{};
        mesh.vertex_offset = vertex_base;
        mesh.vertex_count = vertex_count;
        mesh.index_offset = index_base;
        mesh.index_count = index_count;
        mesh.material_index = static_cast<uint32_t>(out->materials.size());
        out->meshes.push_back(mesh);

        rd_scene_material_v1 material{};
        material.intent = key.empty() ? RD_MATERIAL_INTENT_VERTEX_COLOR
                                      : RD_MATERIAL_INTENT_UNLIT_TEXTURED;
        material.base_texture_index = RD_OFFSET_NONE;
        material.flags = 0;
        material.alpha_test_ref = 0.5f;
        out->materials.push_back(material);
        out->material_texture_keys.push_back(key);

        out->positions.insert(out->positions.end(),
                              group.positions.begin(),
                              group.positions.end());
        out->normals.insert(out->normals.end(),
                            group.normals.begin(),
                            group.normals.end());
        out->uv0.insert(out->uv0.end(),
                        group.uv0.begin(),
                        group.uv0.end());

        for (uint32_t local_idx : group.indices) {
            out->indices.push_back(vertex_base + local_idx);
        }

        vertex_base += vertex_count;
        index_base += index_count;
    }

    return true;
}

}  // namespace

bool build_scene_blob_v1_from_lods(const std::vector<armatools::p3d::LOD>& lods,
                                   SceneBlobBuildOutput* out,
                                   std::string* error_message) {
    if (!out) {
        if (error_message) *error_message = "output pointer is null";
        return false;
    }

    PackedData packed;
    if (!triangulate_lods(lods, &packed, error_message)) {
        return false;
    }

    out->data.clear();
    out->material_texture_keys = std::move(packed.material_texture_keys);

    rd_scene_blob_v1 blob{};
    blob.struct_size = sizeof(rd_scene_blob_v1);
    blob.version = RD_SCENE_BLOB_VERSION;
    blob.flags = RD_SCENE_BLOB_FLAG_INDEX32 |
                 RD_SCENE_BLOB_FLAG_HAS_NORMALS |
                 RD_SCENE_BLOB_FLAG_HAS_UV0;

    blob.vertex_count = static_cast<uint32_t>(packed.positions.size() / 3);
    blob.index_count = static_cast<uint32_t>(packed.indices.size());
    blob.mesh_count = static_cast<uint32_t>(packed.meshes.size());
    blob.material_count = static_cast<uint32_t>(packed.materials.size());
    blob.texture_count = 0;

    blob.positions_offset = append_pod_block(packed.positions, &out->data);
    blob.normals_offset = append_pod_block(packed.normals, &out->data);
    blob.uv0_offset = append_pod_block(packed.uv0, &out->data);
    blob.color0_rgba8_offset = RD_OFFSET_NONE;
    blob.color0_float4_offset = RD_OFFSET_NONE;
    blob.indices_offset = append_pod_block(packed.indices, &out->data);
    blob.meshes_offset = append_pod_block(packed.meshes, &out->data);
    blob.materials_offset = append_pod_block(packed.materials, &out->data);
    blob.textures_offset = 0;

    blob.data_size = static_cast<uint32_t>(out->data.size());
    blob.data = out->data.empty() ? nullptr : out->data.data();

    std::string validation_error;
    if (!validate_scene_blob_v1(blob, &validation_error)) {
        if (error_message) {
            *error_message = "scene blob build failed validation: " + validation_error;
        }
        return false;
    }

    out->blob = blob;
    if (error_message) error_message->clear();
    return true;
}

}  // namespace render_domain
