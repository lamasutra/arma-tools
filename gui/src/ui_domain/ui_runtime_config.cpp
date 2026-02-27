#include "ui_domain/ui_runtime_config.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ui_domain {

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

const json& ui_node_or_root(const json& root) {
    if (root.contains("ui") && root.at("ui").is_object()) {
        return root.at("ui");
    }
    return root;
}

}  // namespace

std::filesystem::path runtime_config_path() {
    const char* override_path = std::getenv("ARMA_TOOLS_UI_CONFIG");
    if (override_path && override_path[0] != '\0') {
        return std::filesystem::path(override_path);
    }

    const auto beside_exe = executable_dir() / "ui.json";
    if (std::filesystem::exists(beside_exe)) {
        return beside_exe;
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "arma-tools" / "ui.json";
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
        const auto& ui = ui_node_or_root(parsed);

        if (ui.contains("preferred")) {
            cfg.preferred = normalize_backend_name(ui.at("preferred").get<std::string>());
        }
        if (ui.contains("imgui_overlay")) {
            cfg.imgui_overlay_enabled = ui.at("imgui_overlay").get<bool>();
        }
        if (ui.contains("imgui_overlay_enabled")) {
            cfg.imgui_overlay_enabled = ui.at("imgui_overlay_enabled").get<bool>();
        }
        if (ui.contains("imgui_docking")) {
            cfg.imgui_docking_enabled = ui.at("imgui_docking").get<bool>();
        }
        if (ui.contains("imgui_docking_enabled")) {
            cfg.imgui_docking_enabled = ui.at("imgui_docking_enabled").get<bool>();
        }
        if (ui.contains("scale")) {
            const float parsed_scale = ui.at("scale").get<float>();
            if (std::isfinite(parsed_scale) && parsed_scale > 0.0f) {
                cfg.scale = parsed_scale;
            }
        }
    } catch (const json::exception&) {
        cfg = RuntimeConfig{};
    }

    return cfg;
}

bool save_runtime_config(const RuntimeConfig& cfg) {
    json parsed;
    parsed["ui"] = {
        {"preferred", normalize_backend_name(cfg.preferred)},
        {"imgui_overlay", cfg.imgui_overlay_enabled},
        {"imgui_docking", cfg.imgui_docking_enabled},
        {"scale", cfg.scale},
    };

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
    const auto sibling = exe_dir / "plugins" / "ui";
    if (std::filesystem::exists(sibling)) {
        return sibling;
    }
    const auto build_root = exe_dir.parent_path() / "plugins" / "ui";
    if (std::filesystem::exists(build_root)) {
        return build_root;
    }
    return sibling;
}

}  // namespace ui_domain
