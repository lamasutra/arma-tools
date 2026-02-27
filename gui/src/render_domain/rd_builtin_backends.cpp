#include "render_domain/rd_builtin_backends.h"

namespace render_domain {

namespace {

int noop_resize(void*, uint32_t, uint32_t) {
    return RD_STATUS_OK;
}

int noop_scene_update(void*, const rd_scene_blob_v1*) {
    return RD_STATUS_OK;
}

int noop_render(void*, const rd_camera_blob_v1*) {
    return RD_STATUS_OK;
}

int noop_stats(void*, rd_frame_stats_v1* stats) {
    if (stats) {
        stats->draw_calls = 0;
        stats->triangles = 0;
        stats->cpu_frame_ms = 0.0f;
        stats->gpu_frame_ms = -1.0f;
    }
    return RD_STATUS_OK;
}

void noop_destroy(void*) {}

int create_noop_backend(const rd_backend_create_desc_v1* desc,
                        rd_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return RD_STATUS_INVALID_ARGUMENT;

    out_instance->userdata = nullptr;
    out_instance->destroy = noop_destroy;
    out_instance->resize = noop_resize;
    out_instance->scene_create_or_update = noop_scene_update;
    out_instance->render_frame = noop_render;
    out_instance->get_frame_stats = noop_stats;
    return RD_STATUS_OK;
}

rd_backend_probe_result_v1 probe_null_backend() {
    rd_backend_probe_result_v1 result{};
    result.struct_size = sizeof(rd_backend_probe_result_v1);
    result.available = 1;
    result.score = 10;
    result.reason = "Headless fallback backend";
    result.device_name = "none";
    result.driver_info = "null";
    return result;
}

rd_backend_probe_result_v1 probe_gles_backend() {
    rd_backend_probe_result_v1 result{};
    result.struct_size = sizeof(rd_backend_probe_result_v1);
    result.available = 1;
#ifdef _WIN32
    result.score = 60;
#else
    result.score = 80;
#endif
    result.reason = "OpenGL ES backend available";
    result.device_name = "OpenGL ES";
    result.driver_info = "GtkGLArea";
    return result;
}

const rd_backend_factory_v1 k_null_factory = {
    RD_ABI_VERSION,
    "null",
    "Null Renderer",
    probe_null_backend,
    create_noop_backend,
};

const rd_backend_factory_v1 k_gles_factory = {
    RD_ABI_VERSION,
    "gles",
    "OpenGL ES",
    probe_gles_backend,
    create_noop_backend,
};

}  // namespace

void register_builtin_backends(BackendRegistry& registry) {
    registry.register_factory(&k_gles_factory, "builtin:gles", false);
    registry.register_factory(&k_null_factory, "builtin:null", false);
}

}  // namespace render_domain
