#include "ui_domain/ui_backend_selection.h"

#include <algorithm>

namespace ui_domain {

namespace {

const BackendRecord* find_backend(const std::vector<BackendRecord>& backends,
                                  const std::string& id) {
    auto it = std::find_if(backends.begin(), backends.end(),
                           [&id](const BackendRecord& item) {
                               return item.id == id;
                           });
    return it == backends.end() ? nullptr : &(*it);
}

const BackendRecord* find_best_available_backend(const std::vector<BackendRecord>& backends) {
    const BackendRecord* best = nullptr;
    for (const auto& backend : backends) {
        if (!backend.probe.available) continue;
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

std::string join_backend_ids(const std::vector<BackendRecord>& backends) {
    std::string joined = "auto";
    for (const auto& backend : backends) {
        joined += ",";
        joined += backend.id;
    }
    return joined;
}

std::string source_label(const SelectionRequest& request) {
    if (request.has_cli_override) return "cli";
    if (request.has_env_override) return "env";
    return "config";
}

std::string requested_backend_name(const SelectionRequest& request) {
    if (request.has_cli_override) return request.cli_backend;
    if (request.has_env_override) return request.env_backend;
    return request.config_backend;
}

}  // namespace

SelectionResult select_backend(const BackendRegistry& registry,
                               const SelectionRequest& request) {
    SelectionResult result;
    const auto& backends = registry.backends();

    std::string requested = requested_backend_name(request);
    if (requested.empty()) requested = "auto";
    const std::string source = source_label(request);

    result.requested_backend = requested;
    result.selection_source = source;

    const bool explicit_selection = requested != "auto";
    result.used_explicit_request = explicit_selection;

    if (explicit_selection) {
        const BackendRecord* backend = find_backend(backends, requested);
        if (!backend) {
            result.success = false;
            result.message = "Requested UI backend '" + requested + "' (" + source +
                             ") is not available. Valid backends: " +
                             join_backend_ids(backends);
            return result;
        }
        if (!backend->probe.available) {
            result.success = false;
            result.message = "Requested UI backend '" + requested +
                             "' is unavailable: " + backend->probe.reason;
            return result;
        }
        result.success = true;
        result.selected_backend = backend->id;
        result.message = "UI backend '" + backend->id +
                         "' selected by explicit " + source + " request";
        return result;
    }

    const BackendRecord* best = find_best_available_backend(backends);
    if (!best) {
        result.success = false;
        result.message = "No available UI backend was detected";
        return result;
    }

    result.success = true;
    result.selected_backend = best->id;
    result.message = "UI auto-selected: '" + best->id +
                     "' (score " + std::to_string(best->probe.score) +
                     ", reason: " + (best->probe.reason.empty() ? std::string("-") : best->probe.reason) +
                     ")";
    return result;
}

}  // namespace ui_domain
