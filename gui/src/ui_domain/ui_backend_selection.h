#pragma once

#include "ui_domain/ui_backend_registry.h"
#include "ui_domain/ui_backend_types.h"

namespace ui_domain {

// Selects the most appropriate UI backend (e.g. GTK, ImGui, null) from the registry.
//
// Prioritization order for explicit requests:
//   1. CLI flag (--ui=gtk)
//   2. Environment variable (UI_BACKEND=gtk)
//   3. Config file preference (config.json)
//
// If no explicit request is made (or the request is "auto"), it picks the
// available UI backend with the highest score.
SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request);

}  // namespace ui_domain

