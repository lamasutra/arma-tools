#pragma once

#include <cstdint>
#include <string>
#include <vector>

// render_domain contains types for the 3D renderer backend selection system.
//
// The renderer draws 3D content (P3D models, WRP terrain) inside OpenGL widget areas.
// Multiple backends may be available (e.g. desktop OpenGL, OpenGL ES, null/headless).
// The selection system picks the best one available at runtime.
namespace render_domain {

// Result of probing a single renderer backend to see if it's available.
struct ProbeResult {
    bool available = false;           // True if this renderer can run on the current machine.
    int score = 0;                    // Higher = preferred when "auto" selection is used.
    uint64_t capability_flags = 0;    // Bitmask of optional rendering features.
    std::string device_name;          // GPU name (e.g. "NVIDIA GeForce RTX 3080").
    std::string driver_info;          // Driver version string.
    std::string reason;               // Explanation for unavailability (empty = available).
};

// Describes a single known renderer backend.
struct BackendRecord {
    std::string id;           // Short unique ID (e.g. "gles", "null").
    std::string name;         // Human-readable display name.
    ProbeResult probe;        // Result of availability check.
    std::string source;       // "builtin" or a plugin .so/.dll file path.
    bool from_plugin = false; // True if loaded from a plugin file.
};

// Recorded when a renderer backend is loaded or fails to load.
struct BackendLoadEvent {
    std::string source_path;  // File loaded, or "builtin".
    std::string backend_id;   // Backend ID (may be empty if loading failed early).
    bool ok = false;          // True if the backend loaded successfully.
    std::string message;      // Error description or informational note.
};

// Input to the renderer backend selection algorithm.
struct SelectionRequest {
    std::string config_backend = "auto"; // Preferred backend from the renderer config file.
    std::string cli_backend;             // Backend name from --renderer= CLI flag.
    bool has_cli_override = false;       // True if --renderer= was passed.
};

// Output of the renderer backend selection algorithm.
struct SelectionResult {
    bool success = false;               // True if a usable renderer backend was found.
    bool used_explicit_request = false; // True if the selection matched an explicit request.
    std::string selected_backend;       // The ID of the chosen backend (empty on failure).
    std::string message;                // Human-readable summary or error.
};

}  // namespace render_domain

