#include "render_domain/rd_backend_abi.h"

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

int create_backend(const rd_backend_create_desc_v1* desc,
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

rd_backend_probe_result_v1 probe_backend() {
    rd_backend_probe_result_v1 result{};
    result.struct_size = sizeof(rd_backend_probe_result_v1);
    result.available = 1;
    result.score = 10;
    result.device_name = "none";
    result.driver_info = "null";
    result.reason = "Headless fallback backend";
    return result;
}

const rd_backend_factory_v1 k_factory = {
    RD_ABI_VERSION,
    "null",
    "Null Renderer",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" RD_PLUGIN_EXPORT const rd_backend_factory_v1* rdGetBackendFactory() {
    return &k_factory;
}
