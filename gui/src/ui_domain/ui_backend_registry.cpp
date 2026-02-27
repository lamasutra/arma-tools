#include "ui_domain/ui_backend_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ui_domain {

namespace {

std::string safe_str(const char* text) {
    return text ? std::string(text) : std::string();
}

bool has_plugin_extension(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.extension() == ".dll";
#elif __APPLE__
    return path.extension() == ".dylib" || path.extension() == ".so";
#else
    return path.extension() == ".so";
#endif
}

ui_backend_probe_result_v1 default_probe_result() {
    ui_backend_probe_result_v1 result{};
    result.struct_size = sizeof(ui_backend_probe_result_v1);
    return result;
}

}  // namespace

struct BackendRegistry::DynamicLibrary {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
    std::filesystem::path path;

    DynamicLibrary() = default;
    ~DynamicLibrary() {
#ifdef _WIN32
        if (handle) {
            FreeLibrary(handle);
        }
#else
        if (handle) {
            dlclose(handle);
        }
#endif
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    bool open(const std::filesystem::path& lib_path, std::string& error) {
        path = lib_path;
#ifdef _WIN32
        handle = LoadLibraryA(lib_path.string().c_str());
        if (!handle) {
            error = "LoadLibrary failed";
            return false;
        }
#else
        handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* msg = dlerror();
            error = msg ? msg : "dlopen failed";
            return false;
        }
#endif
        return true;
    }

    void* symbol(const char* name) const {
#ifdef _WIN32
        return handle ? reinterpret_cast<void*>(GetProcAddress(handle, name)) : nullptr;
#else
        return handle ? dlsym(handle, name) : nullptr;
#endif
    }
};

BackendRegistry::BackendRegistry() = default;
BackendRegistry::~BackendRegistry() = default;

void BackendRegistry::add_load_event(BackendLoadEvent event) {
    load_events_.push_back(std::move(event));
}

std::string BackendRegistry::normalize_backend_id(const char* backend_id) {
    if (!backend_id) return {};
    std::string normalized(backend_id);
    for (char& ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return normalized;
}

void BackendRegistry::register_factory(const ui_backend_factory_v1* factory,
                                       std::string source,
                                       bool from_plugin) {
    if (!factory) {
        add_load_event({std::move(source), {}, false, "factory pointer is null"});
        return;
    }

    if (factory->abi_version != UI_ABI_VERSION) {
        add_load_event({
            std::move(source),
            safe_str(factory->backend_id),
            false,
            "ABI mismatch",
        });
        return;
    }

    const std::string id = normalize_backend_id(factory->backend_id);
    if (id.empty()) {
        add_load_event({std::move(source), {}, false, "backend id is empty"});
        return;
    }

    if (!factory->probe) {
        add_load_event({std::move(source), id, false, "probe callback is missing"});
        return;
    }

    if (!factory->create) {
        add_load_event({std::move(source), id, false, "create callback is missing"});
        return;
    }

    ui_backend_probe_result_v1 probe_raw = default_probe_result();
    probe_raw = factory->probe();
    if (probe_raw.struct_size < sizeof(ui_backend_probe_result_v1)) {
        add_load_event({
            std::move(source),
            id,
            false,
            "probe result struct is too small",
        });
        return;
    }

    ProbeResult probe;
    probe.available = probe_raw.available != 0;
    probe.score = probe_raw.score;
    probe.capability_flags = probe_raw.capability_flags;
    probe.reason = safe_str(probe_raw.reason);

    BackendRecord record;
    record.id = id;
    record.name = safe_str(factory->backend_name);
    record.probe = std::move(probe);
    record.source = source;
    record.from_plugin = from_plugin;

    auto existing = std::find_if(backends_.begin(), backends_.end(),
                                 [&id](const BackendRecord& entry) {
                                     return entry.id == id;
                                 });
    auto existing_factory = std::find_if(factories_.begin(), factories_.end(),
                                         [&id](const FactoryEntry& entry) {
                                             return entry.id == id;
                                         });

    if (existing != backends_.end()) {
        if (from_plugin && !existing->from_plugin) {
            *existing = std::move(record);
            if (existing_factory == factories_.end()) {
                factories_.push_back(FactoryEntry{id, factory});
            } else {
                existing_factory->factory = factory;
            }
            add_load_event({
                std::move(source),
                id,
                true,
                "loaded (plugin replaced builtin backend)",
            });
        } else {
            add_load_event({
                std::move(source),
                id,
                false,
                "duplicate backend id",
            });
        }
        return;
    }

    backends_.push_back(std::move(record));
    if (existing_factory == factories_.end()) {
        factories_.push_back(FactoryEntry{id, factory});
    } else {
        existing_factory->factory = factory;
    }
    add_load_event({std::move(source), id, true, "loaded"});
}

void BackendRegistry::discover_plugin_backends(const std::filesystem::path& plugin_dir) {
    if (!std::filesystem::exists(plugin_dir)) {
        add_load_event({plugin_dir.string(), {}, false, "plugin directory does not exist"});
        return;
    }
    if (!std::filesystem::is_directory(plugin_dir)) {
        add_load_event({plugin_dir.string(), {}, false, "plugin path is not a directory"});
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (!has_plugin_extension(path)) continue;

        auto library = std::make_unique<DynamicLibrary>();
        std::string open_error;
        if (!library->open(path, open_error)) {
            add_load_event({path.string(), {}, false, open_error});
            continue;
        }

        auto symbol = reinterpret_cast<ui_get_backend_factory_fn>(
            library->symbol("uiGetBackendFactory"));
        if (!symbol) {
            add_load_event({path.string(), {}, false, "missing uiGetBackendFactory symbol"});
            continue;
        }

        const ui_backend_factory_v1* factory = symbol();
        register_factory(factory, path.string(), true);
        plugin_handles_.push_back(std::move(library));
    }

    std::sort(backends_.begin(), backends_.end(),
              [](const BackendRecord& lhs, const BackendRecord& rhs) {
                  if (lhs.probe.score != rhs.probe.score) {
                      return lhs.probe.score > rhs.probe.score;
                  }
                  return lhs.id < rhs.id;
              });
}

const std::vector<BackendRecord>& BackendRegistry::backends() const {
    return backends_;
}

const std::vector<BackendLoadEvent>& BackendRegistry::load_events() const {
    return load_events_;
}

bool BackendRegistry::create_instance(const std::string& backend_id,
                                      const ui_backend_create_desc_v1& create_desc,
                                      BackendInstance& out_instance,
                                      std::string& error) const {
    const auto backend = std::find_if(backends_.begin(), backends_.end(),
                                      [&backend_id](const BackendRecord& item) {
                                          return item.id == backend_id;
                                      });
    if (backend == backends_.end()) {
        error = "backend '" + backend_id + "' not found";
        return false;
    }
    if (!backend->probe.available) {
        error = "backend '" + backend_id + "' is unavailable: " + backend->probe.reason;
        return false;
    }

    const auto factory_entry = std::find_if(factories_.begin(), factories_.end(),
                                            [&backend_id](const FactoryEntry& entry) {
                                                return entry.id == backend_id;
                                            });
    if (factory_entry == factories_.end() || !factory_entry->factory ||
        !factory_entry->factory->create) {
        error = "backend '" + backend_id + "' has no create callback";
        return false;
    }

    ui_backend_instance_v1 raw_instance{};
    const int status = factory_entry->factory->create(&create_desc, &raw_instance);
    if (status != UI_STATUS_OK) {
        error = "backend '" + backend_id + "' create failed with status " +
            std::to_string(status);
        return false;
    }
    if (!raw_instance.destroy) {
        error = "backend '" + backend_id + "' create returned invalid instance (destroy missing)";
        return false;
    }

    out_instance = BackendInstance::from_raw(backend_id, raw_instance);
    return true;
}

}  // namespace ui_domain
