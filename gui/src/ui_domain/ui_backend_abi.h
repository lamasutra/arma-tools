#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_ABI_VERSION 1u
#define UI_RENDER_BRIDGE_ABI_VERSION 1u

#if defined(_WIN32)
#define UI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define UI_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum ui_status_v1 {
    UI_STATUS_OK = 0,
    UI_STATUS_NOT_IMPLEMENTED = 1,
    UI_STATUS_EVENT_CONSUMED = 2,
    UI_STATUS_INVALID_ARGUMENT = -1,
    UI_STATUS_RUNTIME_ERROR = -2,
} ui_status_v1;

typedef enum ui_event_type_v1 {
    UI_EVENT_NONE = 0,
    UI_EVENT_MOUSE_MOVE = 1,
    UI_EVENT_MOUSE_BUTTON = 2,
    UI_EVENT_MOUSE_WHEEL = 3,
    UI_EVENT_KEY = 4,
    UI_EVENT_TEXT_INPUT = 5,
    UI_EVENT_DPI_SCALE = 6,
} ui_event_type_v1;

typedef struct ui_event_v1 {
    uint32_t struct_size;
    uint32_t type;
    uint64_t timestamp_ns;
    uint32_t modifiers;
    int32_t i0;
    int32_t i1;
    float f0;
    float f1;
    const char* text;
} ui_event_v1;

typedef struct ui_backend_create_desc_v1 {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    void* native_window;
    void* native_display;
    void* render_bridge;
    void* host_bridge;
    uint64_t flags;
    uint8_t overlay_enabled;
    uint8_t reserved0;
    uint16_t reserved1;
} ui_backend_create_desc_v1;

typedef struct ui_host_bridge_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* userdata;
    int (*ensure_main_window)(void* userdata);
    int (*present_main_window)(void* userdata);
    int (*shutdown_main_window)(void* userdata);
} ui_host_bridge_v1;

#define UI_HOST_BRIDGE_ABI_VERSION 1u

typedef struct ui_vertex_v1 {
    float x;
    float y;
    float u;
    float v;
    uint32_t color_rgba8;
} ui_vertex_v1;

typedef struct ui_draw_cmd_v1 {
    uint32_t elem_count;
    uint32_t idx_offset;
    uint32_t vtx_offset;
    float clip_rect_x1;
    float clip_rect_y1;
    float clip_rect_x2;
    float clip_rect_y2;
} ui_draw_cmd_v1;

typedef struct ui_draw_data_v1 {
    uint32_t struct_size;
    const ui_vertex_v1* vertices;
    uint32_t vertex_count;
    const uint16_t* indices;
    uint32_t index_count;
    const ui_draw_cmd_v1* commands;
    uint32_t command_count;
} ui_draw_data_v1;

typedef struct ui_render_bridge_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* userdata;
    int (*begin_frame)(void* userdata);
    int (*submit_draw_data)(void* userdata, const ui_draw_data_v1* draw_data);
    int (*draw_overlay)(void* userdata);
    int (*end_frame)(void* userdata);
    uint8_t (*is_available)(void* userdata);
    const char* (*bridge_name)(void* userdata);
    const char* (*renderer_backend)(void* userdata);
    const char* (*reason)(void* userdata);
} ui_render_bridge_v1;

typedef struct ui_backend_instance_v1 {
    void* userdata;
    void (*destroy)(void* userdata);
    int (*resize)(void* userdata, uint32_t width, uint32_t height);
    int (*handle_event)(void* userdata, const ui_event_v1* event);
    int (*begin_frame)(void* userdata, double delta_seconds);
    int (*draw)(void* userdata);
    int (*end_frame)(void* userdata);
    int (*set_overlay_enabled)(void* userdata, uint8_t enabled);
    uint8_t (*get_overlay_enabled)(void* userdata);
} ui_backend_instance_v1;

typedef struct ui_backend_probe_result_v1 {
    uint32_t struct_size;
    uint8_t available;
    uint8_t reserved0;
    uint16_t reserved1;
    int32_t score;
    uint64_t capability_flags;
    const char* reason;
} ui_backend_probe_result_v1;

typedef ui_backend_probe_result_v1 (*ui_backend_probe_fn_v1)(void);
typedef int (*ui_backend_create_fn_v1)(
    const ui_backend_create_desc_v1* desc,
    ui_backend_instance_v1* out_instance);

typedef struct ui_backend_factory_v1 {
    uint32_t abi_version;
    const char* backend_id;
    const char* backend_name;
    ui_backend_probe_fn_v1 probe;
    ui_backend_create_fn_v1 create;
} ui_backend_factory_v1;

typedef const ui_backend_factory_v1* (*ui_get_backend_factory_fn)(void);

#ifdef __cplusplus
}
#endif
