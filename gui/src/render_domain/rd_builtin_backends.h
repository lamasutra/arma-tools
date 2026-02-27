#pragma once

#include "render_domain/rd_backend_registry.h"

namespace render_domain {

void register_builtin_backends(BackendRegistry& registry);

}  // namespace render_domain
