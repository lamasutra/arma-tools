#include "ui_domain/ui_backend_abi.h"

#include <new>

namespace {

struct BackendState {
    bool overlay_enabled = false;
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
    delete static_cast<BackendState*>(userdata);
}

int create_backend(const ui_backend_create_desc_v1* desc,
                   ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    auto* state = new (std::nothrow) BackendState{};
    if (!state) return UI_STATUS_RUNTIME_ERROR;
    state->overlay_enabled = desc->overlay_enabled != 0;

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
    result.available = 1;
    result.score = 10;
    result.reason = "Headless UI fallback backend";
    return result;
}

const ui_backend_factory_v1 k_factory = {
    UI_ABI_VERSION,
    "null",
    "Null UI",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" UI_PLUGIN_EXPORT const ui_backend_factory_v1* uiGetBackendFactory() {
    return &k_factory;
}
