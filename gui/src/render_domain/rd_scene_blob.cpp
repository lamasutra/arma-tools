#include "render_domain/rd_scene_blob.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <sstream>

namespace render_domain {

namespace {

bool check_range(uint32_t offset, uint32_t size, uint32_t data_size) {
    if (offset == RD_OFFSET_NONE) {
        return size == 0;
    }
    const uint64_t end = static_cast<uint64_t>(offset) + static_cast<uint64_t>(size);
    return end <= data_size;
}

uint32_t bytes_for_positions(uint32_t vertex_count) {
    return vertex_count * 3u * static_cast<uint32_t>(sizeof(float));
}

uint32_t bytes_for_normals(uint32_t vertex_count) {
    return vertex_count * 3u * static_cast<uint32_t>(sizeof(float));
}

uint32_t bytes_for_uv0(uint32_t vertex_count) {
    return vertex_count * 2u * static_cast<uint32_t>(sizeof(float));
}

uint32_t bytes_for_color0_rgba8(uint32_t vertex_count) {
    return vertex_count * static_cast<uint32_t>(sizeof(uint32_t));
}

uint32_t bytes_for_color0_float4(uint32_t vertex_count) {
    return vertex_count * 4u * static_cast<uint32_t>(sizeof(float));
}

uint32_t bytes_for_indices(uint32_t index_count, bool index32) {
    return index_count * static_cast<uint32_t>(index32 ? sizeof(uint32_t) : sizeof(uint16_t));
}

uint32_t bytes_for_meshes(uint32_t mesh_count) {
    return mesh_count * static_cast<uint32_t>(sizeof(rd_scene_mesh_v1));
}

uint32_t bytes_for_materials(uint32_t material_count) {
    return material_count * static_cast<uint32_t>(sizeof(rd_scene_material_v1));
}

uint32_t bytes_for_textures(uint32_t texture_count) {
    return texture_count * static_cast<uint32_t>(sizeof(rd_scene_texture_v1));
}

}  // namespace

bool validate_scene_blob_v1(const rd_scene_blob_v1& blob, std::string* error_message) {
    auto fail = [error_message](const std::string& msg) {
        if (error_message) *error_message = msg;
        return false;
    };

    if (blob.struct_size < sizeof(rd_scene_blob_v1)) {
        return fail("blob.struct_size is smaller than rd_scene_blob_v1");
    }
    if (blob.version != RD_SCENE_BLOB_VERSION) {
        return fail("unsupported scene blob version");
    }
    if (!blob.data && blob.data_size > 0) {
        return fail("blob.data is null but data_size is non-zero");
    }
    if (blob.positions_offset == RD_OFFSET_NONE) {
        return fail("positions_offset is required");
    }
    if (blob.indices_offset == RD_OFFSET_NONE) {
        return fail("indices_offset is required");
    }

    const bool index32 = (blob.flags & RD_SCENE_BLOB_FLAG_INDEX32) != 0;

    if (!check_range(blob.positions_offset,
                     bytes_for_positions(blob.vertex_count),
                     blob.data_size)) {
        return fail("position stream out of bounds");
    }

    if ((blob.flags & RD_SCENE_BLOB_FLAG_HAS_NORMALS) != 0) {
        if (!check_range(blob.normals_offset,
                         bytes_for_normals(blob.vertex_count),
                         blob.data_size)) {
            return fail("normal stream out of bounds");
        }
    }

    if ((blob.flags & RD_SCENE_BLOB_FLAG_HAS_UV0) != 0) {
        if (!check_range(blob.uv0_offset,
                         bytes_for_uv0(blob.vertex_count),
                         blob.data_size)) {
            return fail("uv0 stream out of bounds");
        }
    }

    if ((blob.flags & RD_SCENE_BLOB_FLAG_HAS_COLOR0_RGBA8) != 0) {
        if (!check_range(blob.color0_rgba8_offset,
                         bytes_for_color0_rgba8(blob.vertex_count),
                         blob.data_size)) {
            return fail("color0_rgba8 stream out of bounds");
        }
    }

    if ((blob.flags & RD_SCENE_BLOB_FLAG_HAS_COLOR0_FLOAT4) != 0) {
        if (!check_range(blob.color0_float4_offset,
                         bytes_for_color0_float4(blob.vertex_count),
                         blob.data_size)) {
            return fail("color0_float4 stream out of bounds");
        }
    }

    if (!check_range(blob.indices_offset,
                     bytes_for_indices(blob.index_count, index32),
                     blob.data_size)) {
        return fail("index stream out of bounds");
    }

    if (!check_range(blob.meshes_offset,
                     bytes_for_meshes(blob.mesh_count),
                     blob.data_size)) {
        return fail("mesh table out of bounds");
    }

    if (!check_range(blob.materials_offset,
                     bytes_for_materials(blob.material_count),
                     blob.data_size)) {
        return fail("material table out of bounds");
    }

    if (!check_range(blob.textures_offset,
                     bytes_for_textures(blob.texture_count),
                     blob.data_size)) {
        return fail("texture table out of bounds");
    }

    if (error_message) error_message->clear();
    return true;
}

bool validate_camera_blob_v1(const rd_camera_blob_v1& camera, std::string* error_message) {
    auto fail = [error_message](const std::string& msg) {
        if (error_message) *error_message = msg;
        return false;
    };

    if (camera.struct_size < sizeof(rd_camera_blob_v1)) {
        return fail("camera.struct_size is smaller than rd_camera_blob_v1");
    }
    if (camera.version != RD_CAMERA_BLOB_VERSION) {
        return fail("unsupported camera blob version");
    }

    for (float value : camera.view) {
        if (!std::isfinite(value)) {
            return fail("camera view matrix contains non-finite values");
        }
    }
    for (float value : camera.projection) {
        if (!std::isfinite(value)) {
            return fail("camera projection matrix contains non-finite values");
        }
    }
    for (float value : camera.position) {
        if (!std::isfinite(value)) {
            return fail("camera position contains non-finite values");
        }
    }

    if (error_message) error_message->clear();
    return true;
}

rd_camera_blob_v1 make_camera_blob_v1(const float* view16,
                                      const float* projection16,
                                      const float* position3) {
    rd_camera_blob_v1 camera{};
    camera.struct_size = sizeof(rd_camera_blob_v1);
    camera.version = RD_CAMERA_BLOB_VERSION;
    if (view16) {
        std::memcpy(camera.view, view16, sizeof(camera.view));
    }
    if (projection16) {
        std::memcpy(camera.projection, projection16, sizeof(camera.projection));
    }
    if (position3) {
        std::memcpy(camera.position, position3, sizeof(camera.position));
    }
    return camera;
}

std::string summarize_scene_blob_v1(const rd_scene_blob_v1& blob) {
    std::ostringstream out;
    out << "scene_blob_v" << blob.version
        << " vertices=" << blob.vertex_count
        << " indices=" << blob.index_count
        << " meshes=" << blob.mesh_count
        << " materials=" << blob.material_count
        << " textures=" << blob.texture_count
        << " data=" << blob.data_size << "B";
    return out.str();
}

std::string summarize_camera_blob_v1(const rd_camera_blob_v1& camera) {
    std::ostringstream out;
    out << "camera_blob_v" << camera.version
        << " pos=(" << camera.position[0] << ","
        << camera.position[1] << ","
        << camera.position[2] << ")";
    return out.str();
}

}  // namespace render_domain
