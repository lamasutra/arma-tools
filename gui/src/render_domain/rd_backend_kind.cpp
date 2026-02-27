#include "render_domain/rd_backend_kind.h"

#include "render_domain/rd_runtime_state.h"

namespace render_domain {

std::string active_backend_id() {
    const auto& state = runtime_state();
    if (!state.selection.selected_backend.empty()) {
        return state.selection.selected_backend;
    }
    if (!state.requested_backend.empty()) {
        return state.requested_backend;
    }
    return "gles";
}

BackendKind active_backend_kind() {
    const auto id = active_backend_id();
    if (id == "gles") return BackendKind::Gles;
    if (id == "null") return BackendKind::Null;
    return BackendKind::Unsupported;
}

}  // namespace render_domain
