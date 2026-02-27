#include "render_domain/rd_runtime_state.h"

#include <utility>

namespace render_domain {

namespace {

RuntimeState g_state;

}  // namespace

void set_runtime_state(RuntimeState state) {
    g_state = std::move(state);
}

const RuntimeState& runtime_state() {
    return g_state;
}

}  // namespace render_domain
