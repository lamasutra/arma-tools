#pragma once

#include "ui_domain/ui_backend_abi.h"

#include <cstdint>

namespace ui_domain {
namespace event_adapter {

// ui_event_adapter provides helper functions to create `ui_event_v1` structs.
//
// Problem it solves:
//   The UI backends (like ImGui) communicate across a C-ABI boundary using raw
//   C structs (`ui_event_v1`). Creating these structs manually is error-prone
//   because you must always set the `struct_size` field correctly for ABI safety.
//
// These helpers let the GTK frontend (AppWindow) easily translate raw GTK
// input events (mouse, keyboard, scroll) into safe `ui_event_v1` structs
// ready to be dispatched to the active backend.

ui_event_v1 make_mouse_move_event(uint64_t timestamp_ns,
                                  uint32_t modifiers,
                                  float x,
                                  float y);
ui_event_v1 make_mouse_button_event(uint64_t timestamp_ns,
                                    uint32_t modifiers,
                                    int32_t button,
                                    bool pressed,
                                    float x,
                                    float y);
ui_event_v1 make_mouse_wheel_event(uint64_t timestamp_ns,
                                   uint32_t modifiers,
                                   float dx,
                                   float dy);
ui_event_v1 make_key_event(uint64_t timestamp_ns,
                           uint32_t modifiers,
                           int32_t keyval,
                           bool pressed);
ui_event_v1 make_text_input_event(uint64_t timestamp_ns,
                                  uint32_t modifiers,
                                  const char* text);
ui_event_v1 make_dpi_scale_event(uint64_t timestamp_ns, float scale);

}  // namespace event_adapter
}  // namespace ui_domain
