#include "ui_domain/ui_builtin_backends.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

namespace ui_domain {

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
    const ui_render_bridge_v1* bridge = nullptr;
    const ui_host_bridge_v1* host_bridge = nullptr;
    bool owns_host_window = false;
    std::vector<ui_vertex_v1> vertices;
    std::vector<uint16_t> indices;
    std::vector<ui_draw_cmd_v1> commands;
    uint32_t frame_counter = 0;
    float ui_scale = 1.0f;
    float panel_x = 18.0f;
    float panel_y = 18.0f;
    float panel_w = 220.0f;
    float panel_h = 56.0f;
    bool pointer_over_overlay = false;
};

int noop_resize(void*, uint32_t, uint32_t) {
    return UI_STATUS_OK;
}

float sanitize_scale(float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) return 1.0f;
    return scale;
}

bool is_pointer_in_overlay(const BackendState* state, float x, float y) {
    if (!state) return false;
    if (x < state->panel_x || y < state->panel_y) return false;
    if (x > (state->panel_x + state->panel_w)) return false;
    if (y > (state->panel_y + state->panel_h)) return false;
    return true;
}

int noop_handle_event(void* userdata, const ui_event_v1* event) {
    auto* state = static_cast<BackendState*>(userdata);
    if (!state || !event || event->struct_size < sizeof(ui_event_v1)) {
        return UI_STATUS_INVALID_ARGUMENT;
    }

    if (event->type == UI_EVENT_DPI_SCALE) {
        state->ui_scale = sanitize_scale(event->f0);
        return UI_STATUS_OK;
    }

    if (!state->overlay_enabled) {
        state->pointer_over_overlay = false;
        return UI_STATUS_OK;
    }

    switch (event->type) {
        case UI_EVENT_MOUSE_MOVE:
        case UI_EVENT_MOUSE_BUTTON: {
            state->pointer_over_overlay =
                is_pointer_in_overlay(state, event->f0, event->f1);
            return state->pointer_over_overlay ? UI_STATUS_EVENT_CONSUMED : UI_STATUS_OK;
        }
        case UI_EVENT_MOUSE_WHEEL:
        case UI_EVENT_KEY:
        case UI_EVENT_TEXT_INPUT:
            return state->pointer_over_overlay ? UI_STATUS_EVENT_CONSUMED : UI_STATUS_OK;
        default:
            return UI_STATUS_OK;
    }
}

int noop_begin_frame(void* userdata, double) {
    auto* state = static_cast<BackendState*>(userdata);
    if (state && state->bridge && state->bridge->begin_frame) {
        return state->bridge->begin_frame(state->bridge->userdata);
    }
    return UI_STATUS_OK;
}

int noop_draw(void* userdata) {
    auto* state = static_cast<BackendState*>(userdata);
    if (state && state->overlay_enabled && state->bridge && state->bridge->draw_overlay) {
        state->frame_counter++;
        const float scale = sanitize_scale(state->ui_scale);
        const float pulse = static_cast<float>((state->frame_counter % 120u)) / 119.0f;
        const float x = (18.0f + pulse * 28.0f) * scale;
        const float y = 18.0f * scale;
        const float w = 220.0f * scale;
        const float h = 56.0f * scale;
        state->panel_x = x;
        state->panel_y = y;
        state->panel_w = w;
        state->panel_h = h;

        state->vertices = {
            {x,     y,     0.0f, 0.0f, 0xFF2936F5u},
            {x + w, y,     1.0f, 0.0f, 0xFF2936F5u},
            {x + w, y + h, 1.0f, 1.0f, 0xFF1F1F1Fu},
            {x,     y + h, 0.0f, 1.0f, 0xFF1F1F1Fu},
        };
        state->indices = {0, 1, 2, 0, 2, 3};
        state->commands = {
            {static_cast<uint32_t>(state->indices.size()), 0, 0, x, y, x + w, y + h}
        };

        if (state->bridge->submit_draw_data) {
            ui_draw_data_v1 draw_data{};
            draw_data.struct_size = sizeof(ui_draw_data_v1);
            draw_data.vertices = state->vertices.data();
            draw_data.vertex_count = static_cast<uint32_t>(state->vertices.size());
            draw_data.indices = state->indices.data();
            draw_data.index_count = static_cast<uint32_t>(state->indices.size());
            draw_data.commands = state->commands.data();
            draw_data.command_count = static_cast<uint32_t>(state->commands.size());
            const int submit_status =
                state->bridge->submit_draw_data(state->bridge->userdata, &draw_data);
            if (submit_status < 0) {
                return submit_status;
            }
        }
        return state->bridge->draw_overlay(state->bridge->userdata);
    }
    return UI_STATUS_OK;
}

int noop_end_frame(void* userdata) {
    auto* state = static_cast<BackendState*>(userdata);
    if (state && state->bridge && state->bridge->end_frame) {
        return state->bridge->end_frame(state->bridge->userdata);
    }
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
    if (state && state->owns_host_window &&
        state->host_bridge && state->host_bridge->shutdown_main_window) {
        state->host_bridge->shutdown_main_window(state->host_bridge->userdata);
    }
    delete state;
}

const ui_host_bridge_v1* validated_host_bridge(const ui_backend_create_desc_v1* desc) {
    if (!desc || !desc->host_bridge) return nullptr;
    const auto* host = static_cast<const ui_host_bridge_v1*>(desc->host_bridge);
    if (host->struct_size < sizeof(ui_host_bridge_v1) ||
        host->abi_version != UI_HOST_BRIDGE_ABI_VERSION) {
        return nullptr;
    }
    return host;
}

int create_noop_backend(const ui_backend_create_desc_v1* desc,
                        ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    auto* state = new (std::nothrow) BackendState{};
    if (!state) return UI_STATUS_RUNTIME_ERROR;
    state->overlay_enabled = desc->overlay_enabled != 0;
    state->bridge = static_cast<const ui_render_bridge_v1*>(desc->render_bridge);
    state->host_bridge = validated_host_bridge(desc);

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

int create_gtk_noop_backend(const ui_backend_create_desc_v1* desc,
                            ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    bool owns_window = false;
    const auto* host = validated_host_bridge(desc);
    if (host && host->ensure_main_window) {
        const int ensure_status = host->ensure_main_window(host->userdata);
        if (ensure_status == UI_STATUS_OK) {
            owns_window = true;
            if (host->present_main_window) {
                host->present_main_window(host->userdata);
            }
        }
    }
    const int status = create_noop_backend(desc, out_instance);
    if (status != UI_STATUS_OK) return status;
    auto* state = static_cast<BackendState*>(out_instance->userdata);
    if (state) {
        state->owns_host_window = owns_window;
    }
    return UI_STATUS_OK;
}

void request_host_main_window(const ui_backend_create_desc_v1* desc) {
    const auto* host = validated_host_bridge(desc);
    if (!host || !host->ensure_main_window) {
        return;
    }
    const int ensure_status = host->ensure_main_window(host->userdata);
    if (ensure_status == UI_STATUS_OK && host->present_main_window) {
        host->present_main_window(host->userdata);
    }
}

int create_imgui_noop_backend(const ui_backend_create_desc_v1* desc,
                              ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    if (!desc->render_bridge) return UI_STATUS_RUNTIME_ERROR;
    const auto* bridge = static_cast<const ui_render_bridge_v1*>(desc->render_bridge);
    if (bridge->struct_size < sizeof(ui_render_bridge_v1) ||
        bridge->abi_version != UI_RENDER_BRIDGE_ABI_VERSION ||
        !bridge->begin_frame || !bridge->submit_draw_data ||
        !bridge->draw_overlay || !bridge->end_frame) {
        return UI_STATUS_RUNTIME_ERROR;
    }
    if (bridge->is_available && bridge->is_available(bridge->userdata) == 0) {
        return UI_STATUS_RUNTIME_ERROR;
    }
    request_host_main_window(desc);
    return create_noop_backend(desc, out_instance);
}

ui_backend_probe_result_v1 probe_gtk_backend() {
    ui_backend_probe_result_v1 result{};
    result.struct_size = sizeof(ui_backend_probe_result_v1);
    if (has_display_runtime()) {
        result.available = 1;
        result.score = 90;
        result.reason = "GTK UI backend available";
    } else {
        result.available = 0;
        result.score = 0;
        result.reason = "No graphical display session detected";
    }
    return result;
}

ui_backend_probe_result_v1 probe_imgui_backend() {
    ui_backend_probe_result_v1 result{};
    result.struct_size = sizeof(ui_backend_probe_result_v1);
    if (has_display_runtime()) {
        result.available = 1;
        result.score = 80;
        result.reason = "ImGui overlay backend available";
    } else {
        result.available = 0;
        result.score = 0;
        result.reason = "No graphical display session detected";
    }
    return result;
}

ui_backend_probe_result_v1 probe_null_backend() {
    ui_backend_probe_result_v1 result{};
    result.struct_size = sizeof(ui_backend_probe_result_v1);
    result.available = 1;
    result.score = 10;
    result.reason = "Headless UI fallback backend";
    return result;
}

const ui_backend_factory_v1 k_gtk_factory = {
    UI_ABI_VERSION,
    "gtk",
    "GTK UI",
    probe_gtk_backend,
    create_gtk_noop_backend,
};

const ui_backend_factory_v1 k_imgui_factory = {
    UI_ABI_VERSION,
    "imgui",
    "ImGui UI",
    probe_imgui_backend,
    create_imgui_noop_backend,
};

const ui_backend_factory_v1 k_null_factory = {
    UI_ABI_VERSION,
    "null",
    "Null UI",
    probe_null_backend,
    create_noop_backend,
};

}  // namespace

void register_builtin_backends(BackendRegistry& registry) {
    registry.register_factory(&k_gtk_factory, "builtin:gtk", false);
    registry.register_factory(&k_imgui_factory, "builtin:imgui", false);
    registry.register_factory(&k_null_factory, "builtin:null", false);
}

}  // namespace ui_domain
