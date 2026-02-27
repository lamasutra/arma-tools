#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace render_domain {

struct ProbeResult {
    bool available = false;
    int score = 0;
    uint64_t capability_flags = 0;
    std::string device_name;
    std::string driver_info;
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
    std::string cli_backend;
    bool has_cli_override = false;
};

struct SelectionResult {
    bool success = false;
    bool used_explicit_request = false;
    std::string selected_backend;
    std::string message;
};

}  // namespace render_domain
