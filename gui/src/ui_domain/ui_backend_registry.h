#pragma once

#include "ui_domain/ui_backend_abi.h"
#include "ui_domain/ui_backend_instance.h"
#include "ui_domain/ui_backend_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ui_domain {

class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry();

    void register_factory(const ui_backend_factory_v1* factory,
                          std::string source,
                          bool from_plugin);
    void discover_plugin_backends(const std::filesystem::path& plugin_dir);
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
