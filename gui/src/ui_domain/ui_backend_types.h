#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ui_domain contains the types used by the UI backend selection system.
//
// The application supports different UI backends (GTK, ImGui, null).
// On startup the app probes all known backends, picks the best available one,
// and creates an instance of it.  These structs record the result of that process.
namespace ui_domain {

// Result of probing a single UI backend to see if it's available on this system.
struct ProbeResult {
    bool available = false;       // True if the backend can run on this machine.
    int score = 0;                // Higher = preferred when "auto" selection is used.
    uint64_t capability_flags = 0; // Bitmask of optional features this backend supports.
    std::string reason;           // Human-readable explanation (e.g. "display not found").
};

// Describes a single known UI backend (built-in or loaded from a plugin .so/.dll).
struct BackendRecord {
    std::string id;          // Short unique ID (e.g. "gtk", "imgui", "null").
    std::string name;        // Human-readable display name.
    ProbeResult probe;       // Result of availability check.
    std::string source;      // Where this backend came from ("builtin" or a file path).
    bool from_plugin = false; // True if this backend was loaded from a plugin file.
};

// Recorded when a backend is loaded or fails to load.  Shown in the log panel.
struct BackendLoadEvent {
    std::string source_path;  // File path being loaded (or "builtin").
    std::string backend_id;   // Backend ID, if known.
    bool ok = false;          // True if the load succeeded.
    std::string message;      // Error or informational message.
};

// Input to the backend selection algorithm.
// Multiple sources of preference are supported (config file, env var, CLI flag).
// CLI flags take the highest priority.
struct SelectionRequest {
    std::string config_backend = "auto"; // Preferred backend from the config file.
    std::string env_backend;             // Backend name from the UI_BACKEND env variable.
    bool has_env_override = false;       // True if UI_BACKEND was set.
    std::string cli_backend;             // Backend name from the --ui= CLI flag.
    bool has_cli_override = false;       // True if --ui= was passed.
};

// Output of the backend selection algorithm.
struct SelectionResult {
    bool success = false;               // True if a usable backend was found.
    bool used_explicit_request = false; // True if the selected backend matches an explicit request.
    std::string selected_backend;       // The ID of the chosen backend (empty on failure).
    std::string requested_backend;      // What was originally requested (for log messages).
    std::string selection_source;       // Where the preference came from ("cli", "env", "config", "auto").
    std::string message;                // Human-readable selection summary or error.
};

}  // namespace ui_domain

