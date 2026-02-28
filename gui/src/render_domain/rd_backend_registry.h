#pragma once

#include "render_domain/rd_backend_abi.h"
#include "render_domain/rd_backend_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace render_domain {

// BackendRegistry manages the discovery and storage of available renderer backends.
//
// A backend can either be "builtin" (compiled directly into the app, like OpenGL)
// or a "plugin" (loaded dynamically from a .so or .dll file at runtime).
// The registry finds these plugins, verifies their ABI (Application Binary Interface)
// compatibility, calls their setup functions to probe capabilities, and stores
// the results so select_backend() can choose the best one.
class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry();

    // Register a generic backend factory (builtin or plugin).
    // Checks ABI version, calls the factory's probe() method, and records the result.
    void register_factory(const rd_backend_factory_v1* factory,
                          std::string source,
                          bool from_plugin);

    // Scans a directory for .so/.dll files, attempts to load them as dynamic
    // libraries, looks for the `rdGetBackendFactory` C symbol, and registers them.
    void discover_plugin_backends(const std::filesystem::path& plugin_dir);

    [[nodiscard]] const std::vector<BackendRecord>& backends() const;
    [[nodiscard]] const std::vector<BackendLoadEvent>& load_events() const;

private:
    struct DynamicLibrary;

    std::vector<BackendRecord> backends_;
    std::vector<BackendLoadEvent> load_events_;
    std::vector<std::unique_ptr<DynamicLibrary>> plugin_handles_;

    void add_load_event(BackendLoadEvent event);
    static std::string normalize_backend_id(const char* backend_id);
};

}  // namespace render_domain
