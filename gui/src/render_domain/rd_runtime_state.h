#pragma once

#include "render_domain/rd_backend_types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace render_domain {

struct RuntimeState {
    std::filesystem::path plugin_dir;
    std::filesystem::path config_path;
    std::vector<BackendRecord> backends;
    std::vector<BackendLoadEvent> load_events;
    SelectionResult selection;
    std::string requested_backend;
    bool requested_from_cli = false;
};

void set_runtime_state(RuntimeState state);
const RuntimeState& runtime_state();

}  // namespace render_domain
