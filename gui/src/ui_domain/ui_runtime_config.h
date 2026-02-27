#pragma once

#include <filesystem>
#include <string>

namespace ui_domain {

struct RuntimeConfig {
    std::string preferred = "auto";
    bool imgui_overlay_enabled = true;
    bool imgui_docking_enabled = true;
    float scale = 1.0f;
};

std::filesystem::path runtime_config_path();
RuntimeConfig load_runtime_config();
bool save_runtime_config(const RuntimeConfig& cfg);
std::filesystem::path default_plugin_dir();

}  // namespace ui_domain
