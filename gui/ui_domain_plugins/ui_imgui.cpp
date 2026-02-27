#include "ui_domain/ui_backend_abi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

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
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };
    Rect menu_bar;
    Rect selector_panel;
    Rect stats_panel;
    Rect selector_ui_row;
    Rect selector_renderer_row;
    int ui_backend_index = 1;       // imgui
    int renderer_backend_index = 0; // gles
    std::vector<ui_vertex_v1> vertices;
    std::vector<uint16_t> indices;
    std::vector<ui_draw_cmd_v1> commands;
    uint32_t frame_counter = 0;
    float ui_scale = 1.0f;
    float frame_time_ms = 16.6f;
    bool pointer_over_overlay = false;
};

void request_host_main_window(const ui_backend_create_desc_v1* desc) {
    if (!desc || !desc->host_bridge) return;
    const auto* host = static_cast<const ui_host_bridge_v1*>(desc->host_bridge);
    if (host->struct_size < sizeof(ui_host_bridge_v1) ||
        host->abi_version != UI_HOST_BRIDGE_ABI_VERSION ||
        !host->ensure_main_window) {
        return;
    }
    const int ensure_status = host->ensure_main_window(host->userdata);
    if (ensure_status == UI_STATUS_OK && host->present_main_window) {
        host->present_main_window(host->userdata);
    }
}

int noop_resize(void*, uint32_t, uint32_t) {
    return UI_STATUS_OK;
}

float sanitize_scale(float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) return 1.0f;
    return scale;
}

bool is_pointer_in_overlay(const BackendState* state, float x, float y) {
    if (!state) return false;
    const auto inside = [x, y](const BackendState::Rect& rect) {
        return x >= rect.x && y >= rect.y &&
               x <= (rect.x + rect.w) && y <= (rect.y + rect.h);
    };
    return inside(state->menu_bar) ||
           inside(state->selector_panel) ||
           inside(state->stats_panel);
}

void update_layout(BackendState* state, float scale) {
    if (!state) return;
    state->menu_bar = {0.0f, 0.0f, 560.0f * scale, 30.0f * scale};
    state->selector_panel = {16.0f * scale, 42.0f * scale, 290.0f * scale, 126.0f * scale};
    state->stats_panel = {320.0f * scale, 42.0f * scale, 230.0f * scale, 126.0f * scale};

    const float row_x = state->selector_panel.x + 12.0f * scale;
    const float row_w = state->selector_panel.w - 24.0f * scale;
    const float row_h = 24.0f * scale;
    state->selector_ui_row = {row_x, state->selector_panel.y + 38.0f * scale, row_w, row_h};
    state->selector_renderer_row = {row_x, state->selector_ui_row.y + row_h + 10.0f * scale, row_w, row_h};
}

void append_rect(BackendState* state, const BackendState::Rect& rect, uint32_t color) {
    if (!state) return;
    const size_t base_index = state->vertices.size();
    if (base_index + 4u > 65535u) return;
    const auto base = static_cast<uint16_t>(base_index);
    const uint32_t idx_offset = static_cast<uint32_t>(state->indices.size());

    state->vertices.push_back({rect.x, rect.y, 0.0f, 0.0f, color});
    state->vertices.push_back({rect.x + rect.w, rect.y, 1.0f, 0.0f, color});
    state->vertices.push_back({rect.x + rect.w, rect.y + rect.h, 1.0f, 1.0f, color});
    state->vertices.push_back({rect.x, rect.y + rect.h, 0.0f, 1.0f, color});

    state->indices.push_back(static_cast<uint16_t>(base + 0));
    state->indices.push_back(static_cast<uint16_t>(base + 1));
    state->indices.push_back(static_cast<uint16_t>(base + 2));
    state->indices.push_back(static_cast<uint16_t>(base + 0));
    state->indices.push_back(static_cast<uint16_t>(base + 2));
    state->indices.push_back(static_cast<uint16_t>(base + 3));

    state->commands.push_back({
        6u,
        idx_offset,
        0u,
        rect.x,
        rect.y,
        rect.x + rect.w,
        rect.y + rect.h,
    });
}

void append_panel(BackendState* state,
                  const BackendState::Rect& panel,
                  float scale,
                  uint32_t frame_color,
                  uint32_t body_color,
                  uint32_t header_color) {
    append_rect(state, panel, frame_color);
    const BackendState::Rect body = {
        panel.x + 2.0f * scale,
        panel.y + 2.0f * scale,
        panel.w - 4.0f * scale,
        panel.h - 4.0f * scale,
    };
    append_rect(state, body, body_color);
    const BackendState::Rect header = {
        body.x,
        body.y,
        body.w,
        24.0f * scale,
    };
    append_rect(state, header, header_color);
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
            if (event->type == UI_EVENT_MOUSE_BUTTON && event->i1 != 0) {
                const auto in_rect = [event](const BackendState::Rect& rect) {
                    return event->f0 >= rect.x && event->f1 >= rect.y &&
                           event->f0 <= (rect.x + rect.w) &&
                           event->f1 <= (rect.y + rect.h);
                };
                if (in_rect(state->selector_ui_row)) {
                    state->ui_backend_index = (state->ui_backend_index + 1) % 3;
                    return UI_STATUS_EVENT_CONSUMED;
                }
                if (in_rect(state->selector_renderer_row)) {
                    state->renderer_backend_index = (state->renderer_backend_index + 1) % 3;
                    return UI_STATUS_EVENT_CONSUMED;
                }
            }
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
    if (!state || !state->bridge || !state->bridge->begin_frame) {
        return UI_STATUS_NOT_IMPLEMENTED;
    }
    return state->bridge->begin_frame(state->bridge->userdata);
}

int noop_draw(void* userdata) {
    auto* state = static_cast<BackendState*>(userdata);
    if (!state || !state->overlay_enabled) return UI_STATUS_OK;
    if (!state->bridge || !state->bridge->draw_overlay) {
        return UI_STATUS_NOT_IMPLEMENTED;
    }

    state->frame_counter++;
    const float scale = sanitize_scale(state->ui_scale);
    update_layout(state, scale);

    const float phase = static_cast<float>(state->frame_counter % 240u) / 239.0f;
    const float wave = 0.5f + 0.5f * std::sin(phase * 6.2831853f);
    state->frame_time_ms = 15.0f + wave * 5.0f;

    state->vertices.clear();
    state->indices.clear();
    state->commands.clear();

    // Top menu bar (mimics ImGui main menu bar)
    append_rect(state, state->menu_bar, 0xE61A1A1Au);
    const BackendState::Rect menu_accent = {
        state->menu_bar.x,
        state->menu_bar.y + state->menu_bar.h - 2.0f * scale,
        state->menu_bar.w,
        2.0f * scale,
    };
    append_rect(state, menu_accent, 0xFF3AA8FFu);

    // Backend selector window with two selectable rows (UI + renderer).
    append_panel(state, state->selector_panel, scale,
                 0xFF202020u, 0xF52A2A2Au, 0xFF343434u);
    append_rect(state, state->selector_ui_row,
                state->ui_backend_index == 0 ? 0xFF34A853u :
                state->ui_backend_index == 1 ? 0xFF3AA8FFu :
                                               0xFF9E9E9Eu);
    append_rect(state, state->selector_renderer_row,
                state->renderer_backend_index == 0 ? 0xFF3AA8FFu :
                state->renderer_backend_index == 1 ? 0xFFF57C00u :
                                                     0xFF9E9E9Eu);

    // Selector row markers (three slots each, active slot highlighted).
    auto append_selector_dots = [&](const BackendState::Rect& row, int active_index) {
        const float dot_w = 10.0f * scale;
        const float dot_h = 10.0f * scale;
        const float gap = 8.0f * scale;
        const float start_x = row.x + row.w - (3.0f * dot_w + 2.0f * gap) - 10.0f * scale;
        const float y = row.y + (row.h - dot_h) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const BackendState::Rect dot = {
                start_x + static_cast<float>(i) * (dot_w + gap),
                y,
                dot_w,
                dot_h,
            };
            append_rect(state, dot, i == active_index ? 0xFFFFFFFFu : 0x99505050u);
        }
    };
    append_selector_dots(state->selector_ui_row, state->ui_backend_index);
    append_selector_dots(state->selector_renderer_row, state->renderer_backend_index);

    // Stats window (three animated bars: FPS/load/commands).
    append_panel(state, state->stats_panel, scale,
                 0xFF202020u, 0xF5282828u, 0xFF333333u);
    const float bar_x = state->stats_panel.x + 12.0f * scale;
    const float bar_w = state->stats_panel.w - 24.0f * scale;
    const float bar_h = 12.0f * scale;
    const float bar_gap = 10.0f * scale;
    const float bar_y0 = state->stats_panel.y + 36.0f * scale;

    const float fps_norm = std::clamp(1.0f - (state->frame_time_ms - 12.0f) / 12.0f, 0.05f, 1.0f);
    const float load_norm = std::clamp(0.35f + wave * 0.55f, 0.05f, 1.0f);
    const float cmd_norm = std::clamp(
        static_cast<float>(state->commands.size()) / 40.0f, 0.05f, 1.0f);
    const std::array<float, 3> norms = {fps_norm, load_norm, cmd_norm};
    const std::array<uint32_t, 3> colors = {
        0xFF34A853u,  // fps
        0xFFF9A825u,  // load
        0xFF42A5F5u,  // cmds
    };
    for (size_t i = 0; i < norms.size(); ++i) {
        const BackendState::Rect bg = {
            bar_x,
            bar_y0 + static_cast<float>(i) * (bar_h + bar_gap),
            bar_w,
            bar_h,
        };
        append_rect(state, bg, 0x99303030u);
        const BackendState::Rect fg = {
            bg.x,
            bg.y,
            bg.w * norms[i],
            bg.h,
        };
        append_rect(state, fg, colors[i]);
    }

    if (state->bridge->submit_draw_data) {
        ui_draw_data_v1 draw_data{};
        draw_data.struct_size = sizeof(ui_draw_data_v1);
        draw_data.vertices = state->vertices.data();
        draw_data.vertex_count = static_cast<uint32_t>(state->vertices.size());
        draw_data.indices = state->indices.data();
        draw_data.index_count = static_cast<uint32_t>(state->indices.size());
        draw_data.commands = state->commands.data();
        draw_data.command_count = static_cast<uint32_t>(state->commands.size());
        const int submit_status = state->bridge->submit_draw_data(state->bridge->userdata, &draw_data);
        if (submit_status < 0) return submit_status;
    }
    return state->bridge->draw_overlay(state->bridge->userdata);
}

int noop_end_frame(void* userdata) {
    auto* state = static_cast<BackendState*>(userdata);
    if (!state || !state->bridge || !state->bridge->end_frame) {
        return UI_STATUS_NOT_IMPLEMENTED;
    }
    return state->bridge->end_frame(state->bridge->userdata);
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
    auto* state = new (std::nothrow) BackendState{};
    if (!state) return UI_STATUS_RUNTIME_ERROR;
    state->overlay_enabled = desc->overlay_enabled != 0;
    state->bridge = bridge;
    request_host_main_window(desc);

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
        result.score = 80;
        result.reason = "ImGui overlay backend available";
    } else {
        result.available = 0;
        result.score = 0;
        result.reason = "No graphical display session detected";
    }
    return result;
}

const ui_backend_factory_v1 k_factory = {
    UI_ABI_VERSION,
    "imgui",
    "ImGui UI",
    probe_backend,
    create_backend,
};

}  // namespace

extern "C" UI_PLUGIN_EXPORT const ui_backend_factory_v1* uiGetBackendFactory() {
    return &k_factory;
}
