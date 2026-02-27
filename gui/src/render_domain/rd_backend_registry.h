#pragma once

#include "render_domain/rd_backend_abi.h"
#include "render_domain/rd_backend_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace render_domain {

class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry();

    void register_factory(const rd_backend_factory_v1* factory,
                          std::string source,
                          bool from_plugin);
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
