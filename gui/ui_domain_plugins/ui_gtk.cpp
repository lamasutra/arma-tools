#include "ui_domain/ui_backend_abi.h"

#include <cstdlib>
#include <new>

namespace {

bool has_display_runtime() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* x11_display = std::getenv("DISPLAY");
    return (wayland_display && wayland_display[0] != '\0') ||
           (x11_display && x11_display[0] != '\0');
#endif
}

struct BackendState {
    bool overlay_enabled = false;
    const ui_host_bridge_v1* host_bridge = nullptr;
    bool owns_window = false;
};

int noop_resize(void*, uint32_t, uint32_t) {
    return UI_STATUS_OK;
}

int noop_handle_event(void*, const ui_event_v1*) {
    return UI_STATUS_OK;
}

int noop_begin_frame(void*, double) {
    return UI_STATUS_OK;
}

int noop_draw(void*) {
    return UI_STATUS_OK;
}

int noop_end_frame(void*) {
    return UI_STATUS_OK;
}

int noop_set_overlay(void* userdata, uint8_t enabled) {
    auto* state = static_cast<BackendState*>(userdata);
    if (!state) return UI_STATUS_INVALID_ARGUMENT;
    state->overlay_enabled = enabled != 0;
    return UI_STATUS_OK;
}

uint8_t noop_get_overlay(void* userdata) {
    const auto* state = static_cast<const BackendState*>(userdata);
    return (state && state->overlay_enabled) ? 1 : 0;
}

void noop_destroy(void* userdata) {
    auto* state = static_cast<BackendState*>(userdata);
    if (state && state->owns_window && state->host_bridge &&
        state->host_bridge->shutdown_main_window) {
        state->host_bridge->shutdown_main_window(state->host_bridge->userdata);
    }
    delete state;
}

int create_backend(const ui_backend_create_desc_v1* desc,
                   ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    auto* state = new (std::nothrow) BackendState{};
    if (!state) return UI_STATUS_RUNTIME_ERROR;
    state->overlay_enabled = desc->overlay_enabled != 0;
    if (desc->host_bridge) {
        const auto* host = static_cast<const ui_host_bridge_v1*>(desc->host_bridge);
        if (host->struct_size >= sizeof(ui_host_bridge_v1) &&
            host->abi_version == UI_HOST_BRIDGE_ABI_VERSION &&
            host->ensure_main_window) {
            state->host_bridge = host;
            const int status = host->ensure_main_window(host->userdata);
            if (status == UI_STATUS_OK) {
                state->owns_window = true;
                if (host->present_main_window) {
                    host->present_main_window(host->userdata);
                }
            }
        }
    }

    out_instance->userdata = state;
    out_instance->destroy = noop_destroy;
    out_instance->resize = noop_resize;
    out_instance->handle_event = noop_handle_event;
    out_instance->begin_frame = noop_begin_frame;
    out_instance->draw = noop_draw;
    out_instance->end_frame = noop_end_frame;
    out_instance->set_overlay_enabled = noop_set_overlay;
    out_instance->get_overlay_enabled = noop_get_overlay;
    return UI_STATUS_OK;
}

ui_backend_probe_result_v1 probe_backend() {
    ui_backend_probe_result_v1 result{};
    result.struct_size = sizeof(ui_backend_probe_result_v1);
    if (has_display_runtime()) {
        result.available = 1;
        result.score = 90;
        result.reason = "GTK runtime available";
    } else {
        result.available = 0;
        result.score = 0;
        result.reason = "No graphical display session detected";
    }
    return result;
}

const ui_backend_factory_v1 k_factory = {
    UI_ABI_VERSION,
    "gtk",
    "GTK UI",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" UI_PLUGIN_EXPORT const ui_backend_factory_v1* uiGetBackendFactory() {
    return &k_factory;
}
