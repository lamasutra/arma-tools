#pragma once

#include <string>

namespace render_domain {

enum class BackendKind {
    Gles,
    Null,
    Unsupported,
};

BackendKind active_backend_kind();
std::string active_backend_id();

}  // namespace render_domain
