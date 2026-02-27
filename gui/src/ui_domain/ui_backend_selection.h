#pragma once

#include "ui_domain/ui_backend_registry.h"
#include "ui_domain/ui_backend_types.h"

namespace ui_domain {

SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request);

}  // namespace ui_domain
