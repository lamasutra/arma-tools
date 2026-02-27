#include "render_domain/rd_runtime_config.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace render_domain {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

fs::path executable_dir() {
#ifdef _WIN32
    wchar_t module_path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(module_path).parent_path();
#else
    std::error_code ec;
    auto link_path = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return link_path.parent_path();
    }
    return fs::current_path();
#endif
}

std::string normalize_backend_name(std::string backend) {
    for (char& ch : backend) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (backend.empty()) return "auto";
    return backend;
}

}  // namespace

std::filesystem::path runtime_config_path() {
    const auto beside_exe = executable_dir() / "renderer.json";
    if (std::filesystem::exists(beside_exe)) {
        return beside_exe;
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "arma-tools" / "renderer.json";
    }

    return beside_exe;
}

RuntimeConfig load_runtime_config() {
    RuntimeConfig cfg;

    std::ifstream stream(runtime_config_path());
    if (!stream.is_open()) {
        return cfg;
    }

    try {
        json parsed = json::parse(stream);
        if (parsed.contains("backend")) {
            cfg.backend = normalize_backend_name(parsed.at("backend").get<std::string>());
        }
    } catch (const json::exception&) {
        cfg.backend = "auto";
    }

    return cfg;
}

bool save_runtime_config(const RuntimeConfig& cfg) {
    json parsed;
    parsed["backend"] = normalize_backend_name(cfg.backend);

    const auto path = runtime_config_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream stream(path);
    if (!stream.is_open()) {
        return false;
    }

    stream << parsed.dump(2) << "\n";
    return true;
}

std::filesystem::path default_plugin_dir() {
    const auto exe_dir = executable_dir();
    const auto sibling = exe_dir / "plugins" / "renderers";
    if (std::filesystem::exists(sibling)) {
        return sibling;
    }
    const auto build_root = exe_dir.parent_path() / "plugins" / "renderers";
    if (std::filesystem::exists(build_root)) {
        return build_root;
    }
    return sibling;
}

}  // namespace render_domain
