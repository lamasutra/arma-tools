#pragma once

#include <filesystem>
#include <string>

namespace render_domain {

struct RuntimeConfig {
    std::string backend = "auto";
};

std::filesystem::path runtime_config_path();
RuntimeConfig load_runtime_config();
bool save_runtime_config(const RuntimeConfig& cfg);
std::filesystem::path default_plugin_dir();

}  // namespace render_domain
