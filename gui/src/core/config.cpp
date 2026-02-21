#include "config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static const std::vector<std::string> k_tool_names = {
    "a3db",
    "asc2tiff",
    "audio_player",
    "ogg_validate",
    "p3d_info",
    "p3d_odol2mlod",
    "paa2img",
    "paa2tga",
    "pbo_extract",
    "pbo_info",
    "tga2paa",
    "wrp2project",
    "wrp_dump",
    "wrp_heightmap",
    "wrp_info",
    "wrp_obj2forestshape",
    "wrp_obj2roadnet",
    "wrp_objreplace",
    "wrp_roadnet",
    "wrp_satmask",
    "heightpipe",
};

const std::vector<std::string>& tool_names() {
    return k_tool_names;
}

static const std::vector<std::string> k_used_tool_names = {
    "asc2tiff",
    "ogg_validate",
    "p3d_odol2mlod",
    "pbo_extract",
    "wrp2project",
    "heightpipe",
};

const std::vector<std::string>& used_tool_names() {
    return k_used_tool_names;
}

static fs::path exe_dir() {
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return fs::current_path();
}

std::string config_path() {
    // Try next to executable first
    auto beside = exe_dir() / "config.json";
    if (fs::exists(beside)) return beside.string();

    // Fallback to ~/.config/arma-tools/config.json
    const char* home = std::getenv("HOME");
    if (home) {
        auto dir = fs::path(home) / ".config" / "arma-tools";
        return (dir / "config.json").string();
    }
    return beside.string();
}

// JSON serialization helpers
static void to_json(json& j, const Wrp2ProjectDefaults& d) {
    j = json{
        {"offset_x", d.offset_x}, {"offset_z", d.offset_z},
        {"hm_scale", d.hm_scale}, {"split", d.split},
        {"style", d.style}, {"extract_p3d", d.extract_p3d},
        {"empty_layers", d.empty_layers}, {"replace_file", d.replace_file},
        {"use_heightpipe", d.use_heightpipe},
        {"heightpipe_preset", d.heightpipe_preset},
        {"heightpipe_seed", d.heightpipe_seed}
    };
}

static void from_json(const json& j, Wrp2ProjectDefaults& d) {
    if (j.contains("offset_x")) j.at("offset_x").get_to(d.offset_x);
    if (j.contains("offset_z")) j.at("offset_z").get_to(d.offset_z);
    if (j.contains("hm_scale")) j.at("hm_scale").get_to(d.hm_scale);
    if (j.contains("split")) j.at("split").get_to(d.split);
    if (j.contains("style")) j.at("style").get_to(d.style);
    if (j.contains("extract_p3d")) j.at("extract_p3d").get_to(d.extract_p3d);
    if (j.contains("empty_layers")) j.at("empty_layers").get_to(d.empty_layers);
    if (j.contains("replace_file")) j.at("replace_file").get_to(d.replace_file);
    if (j.contains("use_heightpipe")) j.at("use_heightpipe").get_to(d.use_heightpipe);
    if (j.contains("heightpipe_preset")) j.at("heightpipe_preset").get_to(d.heightpipe_preset);
    if (j.contains("heightpipe_seed")) j.at("heightpipe_seed").get_to(d.heightpipe_seed);
}

static void to_json(json& j, const AssetBrowserDefaults& d) {
    j = json{
        {"auto_derap", d.auto_derap},
        {"on_demand_metadata", d.on_demand_metadata},
        {"auto_extract_textures", d.auto_extract_textures}
    };
}

static void from_json(const json& j, AssetBrowserDefaults& d) {
    if (j.contains("auto_derap")) j.at("auto_derap").get_to(d.auto_derap);
    if (j.contains("on_demand_metadata")) j.at("on_demand_metadata").get_to(d.on_demand_metadata);
    if (j.contains("auto_extract_textures")) j.at("auto_extract_textures").get_to(d.auto_extract_textures);
}

static void to_json(json& j, const ObjReplaceDefaults& d) {
    j = json{{"last_replacement_file", d.last_replacement_file},
             {"last_wrp_file", d.last_wrp_file},
             {"auto_extract_textures", d.auto_extract_textures}};
}

static void from_json(const json& j, ObjReplaceDefaults& d) {
    if (j.contains("last_replacement_file")) j.at("last_replacement_file").get_to(d.last_replacement_file);
    if (j.contains("last_wrp_file")) j.at("last_wrp_file").get_to(d.last_wrp_file);
    if (j.contains("auto_extract_textures")) j.at("auto_extract_textures").get_to(d.auto_extract_textures);
}

Config load_config() {
    Config cfg;
    auto path = config_path();
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    try {
        json j = json::parse(f);
        if (j.contains("worlds_dir")) j.at("worlds_dir").get_to(cfg.worlds_dir);
        if (j.contains("project_debug_dir")) j.at("project_debug_dir").get_to(cfg.project_debug_dir);
        if (j.contains("tool_verbosity_level")) {
            int level = j.at("tool_verbosity_level").get<int>();
            if (level < 0) level = 0;
            if (level > 2) level = 2;
            cfg.tool_verbosity_level = level;
        }
        if (j.contains("drive_root")) j.at("drive_root").get_to(cfg.drive_root);
        if (j.contains("a3db_path")) j.at("a3db_path").get_to(cfg.a3db_path);
        if (j.contains("arma3_dir")) j.at("arma3_dir").get_to(cfg.arma3_dir);
        if (j.contains("workshop_dir")) j.at("workshop_dir").get_to(cfg.workshop_dir);
        if (j.contains("ofp_dir")) j.at("ofp_dir").get_to(cfg.ofp_dir);
        if (j.contains("arma1_dir")) j.at("arma1_dir").get_to(cfg.arma1_dir);
        if (j.contains("arma2_dir")) j.at("arma2_dir").get_to(cfg.arma2_dir);
        if (j.contains("ffmpeg_path")) j.at("ffmpeg_path").get_to(cfg.ffmpeg_path);
        if (j.contains("binaries")) j.at("binaries").get_to(cfg.binaries);
        if (j.contains("recent_wrps")) j.at("recent_wrps").get_to(cfg.recent_wrps);
        if (j.contains("last_browse_dir")) j.at("last_browse_dir").get_to(cfg.last_browse_dir);
        if (j.contains("last_active_tab")) j.at("last_active_tab").get_to(cfg.last_active_tab);
        if (j.contains("panel_layout")) j.at("panel_layout").get_to(cfg.panel_layout);
        if (j.contains("wrp2project_defaults")) j.at("wrp2project_defaults").get_to(cfg.wrp2project_defaults);
        if (j.contains("asset_browser_defaults")) j.at("asset_browser_defaults").get_to(cfg.asset_browser_defaults);
        if (j.contains("obj_replace_defaults")) j.at("obj_replace_defaults").get_to(cfg.obj_replace_defaults);
    } catch (const json::exception& e) {
        std::cerr << "Config parse error: " << e.what() << "\n";
    }
    return cfg;
}

void save_config(const Config& cfg) {
    json j;
    j["worlds_dir"] = cfg.worlds_dir;
    j["project_debug_dir"] = cfg.project_debug_dir;
    j["tool_verbosity_level"] = cfg.tool_verbosity_level;
    j["drive_root"] = cfg.drive_root;
    j["a3db_path"] = cfg.a3db_path;
    j["arma3_dir"] = cfg.arma3_dir;
    j["workshop_dir"] = cfg.workshop_dir;
    j["ofp_dir"] = cfg.ofp_dir;
    j["arma1_dir"] = cfg.arma1_dir;
    j["arma2_dir"] = cfg.arma2_dir;
    j["ffmpeg_path"] = cfg.ffmpeg_path;
    j["binaries"] = cfg.binaries;
    j["recent_wrps"] = cfg.recent_wrps;
    j["last_browse_dir"] = cfg.last_browse_dir;
    j["last_active_tab"] = cfg.last_active_tab;
    j["panel_layout"] = cfg.panel_layout;
    j["wrp2project_defaults"] = cfg.wrp2project_defaults;
    j["asset_browser_defaults"] = cfg.asset_browser_defaults;
    j["obj_replace_defaults"] = cfg.obj_replace_defaults;

    auto path = config_path();
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2) << "\n";
    }
}

std::string find_binary(const std::string& name) {
    // Check next to executable
    auto beside = exe_dir() / name;
    if (fs::exists(beside)) return beside.string();

    // Check $PATH
    const char* path_env = std::getenv("PATH");
    if (!path_env) return {};
    std::string path_str(path_env);
    std::string::size_type start = 0;
    while (start < path_str.size()) {
        auto end = path_str.find(':', start);
        if (end == std::string::npos) end = path_str.size();
        auto dir = path_str.substr(start, end - start);
        auto candidate = fs::path(dir) / name;
        if (fs::exists(candidate)) return candidate.string();
        start = end + 1;
    }
    return {};
}

std::string resolve_tool_path(const Config& cfg, const std::string& tool_name) {
    // Config override
    auto it = cfg.binaries.find(tool_name);
    if (it != cfg.binaries.end() && !it->second.empty() && fs::exists(it->second)) {
        return it->second;
    }
    return find_binary(tool_name);
}
