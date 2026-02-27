#include "ui_domain/ui_event_adapter.h"

namespace ui_domain {
namespace event_adapter {

namespace {

ui_event_v1 make_base_event(uint64_t timestamp_ns,
                            uint32_t type,
                            uint32_t modifiers) {
    ui_event_v1 event{};
    event.struct_size = sizeof(ui_event_v1);
    event.type = type;
    event.timestamp_ns = timestamp_ns;
    event.modifiers = modifiers;
    return event;
}

}  // namespace

ui_event_v1 make_mouse_move_event(uint64_t timestamp_ns,
                                  uint32_t modifiers,
                                  float x,
                                  float y) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_MOUSE_MOVE, modifiers);
    event.f0 = x;
    event.f1 = y;
    return event;
}

ui_event_v1 make_mouse_button_event(uint64_t timestamp_ns,
                                    uint32_t modifiers,
                                    int32_t button,
                                    bool pressed,
                                    float x,
                                    float y) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_MOUSE_BUTTON, modifiers);
    event.i0 = button;
    event.i1 = pressed ? 1 : 0;
    event.f0 = x;
    event.f1 = y;
    return event;
}

ui_event_v1 make_mouse_wheel_event(uint64_t timestamp_ns,
                                   uint32_t modifiers,
                                   float dx,
                                   float dy) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_MOUSE_WHEEL, modifiers);
    event.f0 = dx;
    event.f1 = dy;
    return event;
}

ui_event_v1 make_key_event(uint64_t timestamp_ns,
                           uint32_t modifiers,
                           int32_t keyval,
                           bool pressed) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_KEY, modifiers);
    event.i0 = keyval;
    event.i1 = pressed ? 1 : 0;
    return event;
}

ui_event_v1 make_text_input_event(uint64_t timestamp_ns,
                                  uint32_t modifiers,
                                  const char* text) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_TEXT_INPUT, modifiers);
    event.text = text;
    return event;
}

ui_event_v1 make_dpi_scale_event(uint64_t timestamp_ns, float scale) {
    ui_event_v1 event = make_base_event(timestamp_ns, UI_EVENT_DPI_SCALE, 0);
    event.f0 = scale;
    return event;
}

}  // namespace event_adapter
}  // namespace ui_domain
