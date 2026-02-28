#pragma once

#include "render_domain/rd_backend_registry.h"
#include "render_domain/rd_backend_types.h"

namespace render_domain {

// Selects the most appropriate renderer backend from the registry.
//
// If the user requested a specific backend (via CLI or config), it tries to use it.
// If "auto" is requested (the default), it picks the available renderer with
// the highest score. In case of a tie, it falls back to alphabetical order of the ID.
SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request);

}  // namespace render_domain

