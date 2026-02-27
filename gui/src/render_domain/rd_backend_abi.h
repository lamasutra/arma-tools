#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RD_ABI_VERSION 1u
#define RD_SCENE_BLOB_VERSION 1u
#define RD_CAMERA_BLOB_VERSION 1u
#define RD_OFFSET_NONE 0xffffffffu

#if defined(_WIN32)
#define RD_PLUGIN_EXPORT __declspec(dllexport)
#else
#define RD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum rd_status_v1 {
    RD_STATUS_OK = 0,
    RD_STATUS_NOT_IMPLEMENTED = 1,
    RD_STATUS_INVALID_ARGUMENT = -1,
    RD_STATUS_RUNTIME_ERROR = -2,
} rd_status_v1;

typedef enum rd_scene_blob_flags_v1 {
    RD_SCENE_BLOB_FLAG_INDEX32 = 1u << 0,
    RD_SCENE_BLOB_FLAG_HAS_NORMALS = 1u << 1,
    RD_SCENE_BLOB_FLAG_HAS_UV0 = 1u << 2,
    RD_SCENE_BLOB_FLAG_HAS_COLOR0_RGBA8 = 1u << 3,
    RD_SCENE_BLOB_FLAG_HAS_COLOR0_FLOAT4 = 1u << 4,
} rd_scene_blob_flags_v1;

typedef enum rd_material_intent_v1 {
    RD_MATERIAL_INTENT_UNLIT_TEXTURED = 1,
    RD_MATERIAL_INTENT_VERTEX_COLOR = 2,
    RD_MATERIAL_INTENT_ALPHA_TEST_TEXTURED = 3,
} rd_material_intent_v1;

typedef enum rd_texture_format_v1 {
    RD_TEXTURE_FORMAT_RGBA8 = 1,
    RD_TEXTURE_FORMAT_DXT1 = 2,
    RD_TEXTURE_FORMAT_DXT5 = 3,
} rd_texture_format_v1;

typedef struct rd_scene_mesh_v1 {
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
    uint32_t material_index;
} rd_scene_mesh_v1;

typedef struct rd_scene_material_v1 {
    uint32_t intent;
    uint32_t base_texture_index;
    uint32_t flags;
    float alpha_test_ref;
} rd_scene_material_v1;

typedef struct rd_scene_texture_v1 {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t data_offset;
    uint32_t data_size;
} rd_scene_texture_v1;

typedef struct rd_scene_blob_v1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;

    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t mesh_count;
    uint32_t material_count;
    uint32_t texture_count;

    uint32_t data_size;
    const uint8_t* data;

    uint32_t positions_offset;
    uint32_t normals_offset;
    uint32_t uv0_offset;
    uint32_t color0_rgba8_offset;
    uint32_t color0_float4_offset;
    uint32_t indices_offset;

    uint32_t meshes_offset;
    uint32_t materials_offset;
    uint32_t textures_offset;
} rd_scene_blob_v1;

typedef struct rd_camera_blob_v1 {
    uint32_t struct_size;
    uint32_t version;
    float view[16];
    float projection[16];
    float position[3];
    float reserved0;
} rd_camera_blob_v1;

typedef struct rd_frame_stats_v1 {
    uint64_t draw_calls;
    uint64_t triangles;
    float cpu_frame_ms;
    float gpu_frame_ms;
} rd_frame_stats_v1;

typedef struct rd_backend_create_desc_v1 {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    void* native_window;
    void* native_display;
    uint64_t flags;
} rd_backend_create_desc_v1;

typedef struct rd_backend_instance_v1 {
    void* userdata;
    void (*destroy)(void* userdata);
    int (*resize)(void* userdata, uint32_t width, uint32_t height);
    int (*scene_create_or_update)(void* userdata, const rd_scene_blob_v1* blob);
    int (*render_frame)(void* userdata, const rd_camera_blob_v1* camera);
    int (*get_frame_stats)(void* userdata, rd_frame_stats_v1* stats);
} rd_backend_instance_v1;

typedef struct rd_backend_probe_result_v1 {
    uint32_t struct_size;
    uint8_t available;
    uint8_t reserved0;
    uint16_t reserved1;
    int32_t score;
    uint64_t capability_flags;
    const char* device_name;
    const char* driver_info;
    const char* reason;
} rd_backend_probe_result_v1;

typedef rd_backend_probe_result_v1 (*rd_backend_probe_fn_v1)(void);
typedef int (*rd_backend_create_fn_v1)(
    const rd_backend_create_desc_v1* desc,
    rd_backend_instance_v1* out_instance);

typedef struct rd_backend_factory_v1 {
    uint32_t abi_version;
    const char* backend_id;
    const char* backend_name;
    rd_backend_probe_fn_v1 probe;
    rd_backend_create_fn_v1 create;
} rd_backend_factory_v1;

typedef const rd_backend_factory_v1* (*rd_get_backend_factory_fn)(void);

#ifdef __cplusplus
}
#endif
