#include "render_domain/rd_backend_selection.h"

#include <algorithm>

namespace render_domain {

namespace {

// Helper: find a backend by its exact string ID (e.g., "gles").
// Returns nullptr if not found.
const BackendRecord* find_backend(const std::vector<BackendRecord>& backends,
                                  const std::string& id) {
    auto it = std::find_if(backends.begin(), backends.end(),
                           [&id](const BackendRecord& item) {
                               return item.id == id;
                           });
    return it == backends.end() ? nullptr : &(*it);
}

// Helper: iterate over all known backends and find the one that:
// 1. Is actually available on this machine (probe.available == true)
// 2. Has the highest preference score
// 3. Resolves ties using alphabetical order of the ID
const BackendRecord* find_best_available_backend(const std::vector<BackendRecord>& backends) {
    const BackendRecord* best = nullptr;
    for (const auto& backend : backends) {
        if (!backend.probe.available) continue; // Skip unavailable ones
        if (!best) {
            best = &backend;
            continue;
        }
        if (backend.probe.score > best->probe.score) {
            best = &backend;
            continue;
        }
        if (backend.probe.score == best->probe.score && backend.id < best->id) {
            best = &backend;
        }
    }
    return best;
}

}  // namespace

// Core selection logic used during application startup.
SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request) {
    SelectionResult result;
    const auto& backends = registry.backends();

    // CLI flags (--renderer=xyz) take precedence over the saved config.json preference.
    const std::string requested = request.has_cli_override
        ? request.cli_backend
        : request.config_backend;

    const bool explicit_selection = !requested.empty() && requested != "auto";
    result.used_explicit_request = explicit_selection;

    // If the user explicitly asked for a specific backend (e.g. they ran with --renderer=gles),
    // try to honor that request exactly.  Fail if it doesn't exist or isn't available.
    if (explicit_selection) {
        const BackendRecord* backend = find_backend(backends, requested);
        if (!backend) {
            result.success = false;
            result.message = "Requested renderer '" + requested + "' is not available";
            return result;
        }
        if (!backend->probe.available) {
            result.success = false;
            result.message = "Requested renderer '" + requested +
                             "' is unavailable: " + backend->probe.reason;
            return result;
        }
        result.success = true;
        result.selected_backend = backend->id;
        result.message = "Renderer '" + backend->id + "' selected by explicit request";
        return result;
    }

    const BackendRecord* best = find_best_available_backend(backends);
    if (!best) {
        result.success = false;
        result.message = "No available renderer backend was detected";
        return result;
    }

    result.success = true;
    result.selected_backend = best->id;
    result.message = "Renderer auto-selected: '" + best->id +
                     "' (score " + std::to_string(best->probe.score) + ")";
    return result;
}

}  // namespace render_domain
