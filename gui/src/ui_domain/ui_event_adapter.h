#pragma once

#include "ui_domain/ui_backend_abi.h"

#include <cstdint>

namespace ui_domain {
namespace event_adapter {

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
