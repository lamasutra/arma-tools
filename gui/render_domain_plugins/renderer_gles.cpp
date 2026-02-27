#include "render_domain/rd_backend_abi.h"

namespace {

int create_backend(const rd_backend_create_desc_v1*,
                   rd_backend_instance_v1*) {
    // Phase 1 migration path: existing GL widgets still own rendering.
    return RD_STATUS_NOT_IMPLEMENTED;
}

rd_backend_probe_result_v1 probe_backend() {
    rd_backend_probe_result_v1 result{};
    result.struct_size = sizeof(rd_backend_probe_result_v1);
    result.available = 1;
#ifdef _WIN32
    result.score = 60;
#else
    result.score = 80;
#endif
    result.device_name = "OpenGL ES";
    result.driver_info = "GtkGLArea";
    result.reason = "OpenGL ES backend available";
    return result;
}

const rd_backend_factory_v1 k_factory = {
    RD_ABI_VERSION,
    "gles",
    "OpenGL ES",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" RD_PLUGIN_EXPORT const rd_backend_factory_v1* rdGetBackendFactory() {
    return &k_factory;
}
