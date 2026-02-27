#include "ui_domain/ui_runtime_state.h"

#include <utility>

namespace ui_domain {

namespace {

RuntimeState g_state;

}  // namespace

void set_runtime_state(RuntimeState state) {
    g_state = std::move(state);
}

const RuntimeState& runtime_state() {
    return g_state;
}

RuntimeState& runtime_state_mut() {
    return g_state;
}

}  // namespace ui_domain
