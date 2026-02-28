#pragma once

#include <map>
#include <string>
#include <vector>

// Default settings for the WRP-to-Project export feature.
// WRP is the Arma 3 world/terrain file format. These values are used as
// starting values when the user opens the WRP Project tab.
struct Wrp2ProjectDefaults {
    std::string offset_x = "200000";
    std::string offset_z = "0";
    std::string hm_scale = "1";
    std::string split = "10000";
    std::string style;
    bool extract_p3d = false;
    bool empty_layers = false;
    std::string replace_file;
    bool use_heightpipe = false;
    std::string heightpipe_preset = "terrain_16x";
    std::string heightpipe_seed = "1";
};

// Default settings for the Asset Browser tab which lets the user
// browse PBO archives and their contained assets (models, textures, configs).
struct AssetBrowserDefaults {
    bool auto_derap = true;
    bool on_demand_metadata = false;
    bool auto_extract_textures = false;
};

// Default settings for the OBJ Replace tab.
// This tab replaces object references inside a WRP world file using a CSV mapping.
struct ObjReplaceDefaults {
    std::string last_replacement_file;
    std::string last_wrp_file;
    bool auto_extract_textures = false;
};

// The main application configuration, loaded from and saved to config.json.
// Every field has a sensible default so the app works out-of-the-box
// even if the user has not set up a config file yet.
struct Config {
    std::string worlds_dir;
    std::string project_debug_dir;
    std::string drive_root;
    std::string a3db_path;
    std::string arma3_dir;
    std::string workshop_dir;
    std::string ofp_dir;
    std::string arma1_dir;
    std::string arma2_dir;
    std::string ffmpeg_path;
    // Controls how verbose the external CLI tool output is (0 = normal, higher = more verbose).
    int tool_verbosity_level = 0;

    // Map from tool name (e.g. "cfgconvert") to its resolved binary path.
    // The user can override specific tool paths through the Config tab.
    std::map<std::string, std::string> binaries;
    std::vector<std::string> recent_wrps;
    std::string last_browse_dir;
    std::string last_active_tab;

    Wrp2ProjectDefaults wrp2project_defaults;
    AssetBrowserDefaults asset_browser_defaults;
    ObjReplaceDefaults obj_replace_defaults;
};

// Stores the saved panel layout so it can be restored on next launch.
// The panels field is a serialized GVariant string produced by libpanel's PanelSession API.
struct LayoutConfig {
    std::string panels;  // Serialized PanelSession GVariant string
};

// Returns the path to the config JSON file.
std::string config_path();

// Load configs from disk. Returns defaults if file doesn't exist.
Config load_config();
LayoutConfig load_layout_config();

// Save configs to disk.
void save_config(const Config& cfg);
void save_layout_config(const LayoutConfig& cfg);

// Resolve a tool binary path: config override -> next to exe -> $PATH.
std::string resolve_tool_path(const Config& cfg, const std::string& tool_name);

// Find a binary by scanning next to the executable, then $PATH.
std::string find_binary(const std::string& name);

// List of all CLI tool binary names.
const std::vector<std::string>& tool_names();

// List of tool binaries actually used by the GUI.
const std::vector<std::string>& used_tool_names();
