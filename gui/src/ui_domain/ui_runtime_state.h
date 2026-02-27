#pragma once

#include "ui_domain/ui_backend_types.h"
#include "ui_domain/ui_backend_instance.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui_domain {

class BackendRegistry;

struct RuntimeState {
    std::filesystem::path plugin_dir;
    std::filesystem::path config_path;
    std::vector<BackendRecord> backends;
    std::vector<BackendLoadEvent> load_events;
    SelectionResult selection;
    std::string requested_backend;
    bool requested_from_cli = false;
    bool requested_from_env = false;
    std::shared_ptr<BackendRegistry> registry_owner;
    std::shared_ptr<BackendInstance> backend_instance;
    std::shared_ptr<BackendInstance> overlay_backend_instance;
    std::string overlay_backend_id;
};

void set_runtime_state(RuntimeState state);
const RuntimeState& runtime_state();
RuntimeState& runtime_state_mut();

}  // namespace ui_domain
