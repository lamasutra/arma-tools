#pragma once

#include "render_domain/rd_backend_registry.h"
#include "render_domain/rd_backend_types.h"

namespace render_domain {

SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request);

}  // namespace render_domain
