#pragma once

#include "ui_domain/ui_backend_abi.h"
#include "ui_domain/ui_backend_instance.h"
#include "ui_domain/ui_backend_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui_domain {

// BackendRegistry manages the discovery, storage, and instantiation of UI backends.
//
// Like the render_domain registry, it handles both "builtin" backends (like GTK)
// and "plugin" backends loaded from .so/.dll files at runtime.
// It uses ABI versioning to ensure plugins are safe to load, probes their
// capabilities, and provides a method to create a live instance (BackendInstance)
// of the chosen backend.
class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry();

    // Registers a UI backend factory (builtin or from a plugin).
    // Checks ABI version, calls the factory's probe() method, and records the result.
    void register_factory(const ui_backend_factory_v1* factory,
                          std::string source,
                          bool from_plugin);

    // Scans a directory for .so/.dll files, loads them, looks for the
    // `uiGetBackendFactory` C symbol, and registers any valid backends found.
    void discover_plugin_backends(const std::filesystem::path& plugin_dir);

    // Creates a live instance of the backend specified by `backend_id`.
    // The `create_desc` struct contains host bridges (callbacks) that give the
    // backend access to the main window and renderer without tight coupling.
    // Returns true on success, or sets `error` and returns false on failure.
    bool create_instance(const std::string& backend_id,
                         const ui_backend_create_desc_v1& create_desc,
                         BackendInstance& out_instance,
                         std::string& error) const;

    [[nodiscard]] const std::vector<BackendRecord>& backends() const;
    [[nodiscard]] const std::vector<BackendLoadEvent>& load_events() const;

private:
    struct DynamicLibrary;
    struct FactoryEntry {
        std::string id;
        const ui_backend_factory_v1* factory = nullptr;
    };

    std::vector<BackendRecord> backends_;
    std::vector<FactoryEntry> factories_;
    std::vector<BackendLoadEvent> load_events_;
    std::vector<std::unique_ptr<DynamicLibrary>> plugin_handles_;

    void add_load_event(BackendLoadEvent event);
    static std::string normalize_backend_id(const char* backend_id);
};

}  // namespace ui_domain
