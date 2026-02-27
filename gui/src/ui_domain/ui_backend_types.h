#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ui_domain {

struct ProbeResult {
    bool available = false;
    int score = 0;
    uint64_t capability_flags = 0;
    std::string reason;
};

struct BackendRecord {
    std::string id;
    std::string name;
    ProbeResult probe;
    std::string source;
    bool from_plugin = false;
};

struct BackendLoadEvent {
    std::string source_path;
    std::string backend_id;
    bool ok = false;
    std::string message;
};

struct SelectionRequest {
    std::string config_backend = "auto";
    std::string env_backend;
    bool has_env_override = false;
    std::string cli_backend;
    bool has_cli_override = false;
};

struct SelectionResult {
    bool success = false;
    bool used_explicit_request = false;
    std::string selected_backend;
    std::string requested_backend;
    std::string selection_source;
    std::string message;
};

}  // namespace ui_domain
