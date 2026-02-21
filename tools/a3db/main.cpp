#include "armatools/pboindex.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

struct Config {
    std::string arma3;
    std::string workshop;
    std::vector<std::string> mods;
    std::string db;
    std::string ofp;
    std::string arma1;
    std::string arma2;
};

static Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("reading config " + path);
    json j = json::parse(f);
    Config cfg;
    if (j.contains("arma3")) cfg.arma3 = j["arma3"].get<std::string>();
    if (j.contains("workshop")) cfg.workshop = j["workshop"].get<std::string>();
    if (j.contains("db")) cfg.db = j["db"].get<std::string>();
    if (j.contains("mods")) {
        for (const auto& m : j["mods"]) cfg.mods.push_back(m.get<std::string>());
    }
    if (j.contains("ofp")) cfg.ofp = j["ofp"].get<std::string>();
    if (j.contains("arma1")) cfg.arma1 = j["arma1"].get<std::string>();
    if (j.contains("arma2")) cfg.arma2 = j["arma2"].get<std::string>();
    return cfg;
}

static void stderr_progress(const armatools::pboindex::BuildProgress& p) {
    std::string pbo_name = fs::path(p.pbo_path).filename().string();
    int width = static_cast<int>(std::to_string(p.pbo_total).size());

    if (p.phase == "discovery") {
        std::cerr << "Discovered " << p.pbo_total << " PBOs\n";
    } else if (p.phase == "warning") {
        std::cerr << "\nWarning: " << pbo_name << ": " << p.file_name << "\n";
    } else if (p.phase == "pbo") {
        std::cerr << std::format("\r[{:>{}}/{:d}] {}\033[K", p.pbo_index + 1, width, p.pbo_total, pbo_name);
    } else if (p.phase == "p3d" || p.phase == "paa" || p.phase == "ogg" || p.phase == "audio") {
        std::cerr << std::format("\r[{:>{}}/{:d}] {} -- {} {}/{}: {}\033[K",
                                  p.pbo_index + 1, width, p.pbo_total, pbo_name,
                                  p.phase, p.file_index + 1, p.file_total, p.file_name);
    } else if (p.phase == "commit") {
        std::cerr << "\nCommitting...\n";
    }
}

static armatools::pboindex::GameDirs game_dirs_from_config(const Config& cfg) {
    return {.ofp_dir = cfg.ofp, .arma1_dir = cfg.arma1, .arma2_dir = cfg.arma2};
}

static bool has_any_search_path(const Config& cfg) {
    return !cfg.arma3.empty() || !cfg.workshop.empty() || !cfg.mods.empty()
        || !cfg.ofp.empty() || !cfg.arma1.empty() || !cfg.arma2.empty();
}

static void do_build(const Config& cfg, bool on_demand) {
    if (!has_any_search_path(cfg)) {
        std::cerr << "Error: no PBO search paths. Use -arma3, -workshop, -ofp, -arma1, -arma2, -config, or config mods[].\n";
        return;
    }

    if (cfg.db.empty()) {
        std::cerr << "Error: no output path. Specify output.db as argument, use -db, or set db in config.\n";
        return;
    }

    armatools::pboindex::BuildOptions opts{.on_demand_metadata = on_demand};
    try {
        auto result = armatools::pboindex::DB::build_db(cfg.db, cfg.arma3, cfg.workshop, cfg.mods, opts, stderr_progress, game_dirs_from_config(cfg));
        std::cerr << std::format("\nIndexed {} PBOs, {} files, {} P3D models, {} textures, {} audio files\n",
                                  result.pbo_count, result.file_count, result.p3d_count, result.paa_count, result.audio_count);
    } catch (const std::exception& e) {
        std::cerr << "Error: building database: " << e.what() << '\n';
        return;
    }

    std::error_code ec;
    auto size = fs::file_size(cfg.db, ec);
    if (!ec) {
        std::cerr << std::format("Wrote {} ({:.1f} MB)\n", cfg.db, static_cast<double>(size) / 1024 / 1024);
    }
}

static void do_update(Config cfg, bool on_demand) {
    if (!has_any_search_path(cfg)) {
        std::cerr << "Error: no PBO search paths.\n";
        return;
    }
    if (cfg.db.empty()) {
        std::cerr << "Error: -db is required for -update.\n";
        return;
    }

    if (!fs::exists(cfg.db)) {
        std::cerr << "No existing database found, doing full build.\n";
        do_build(cfg, on_demand);
        return;
    }

    armatools::pboindex::BuildOptions opts{.on_demand_metadata = on_demand};
    try {
        auto result = armatools::pboindex::DB::update_db(cfg.db, cfg.arma3, cfg.workshop, cfg.mods, opts, stderr_progress, game_dirs_from_config(cfg));
        std::cerr << std::format("\nAdded {}, updated {}, removed {} PBOs ({} files, {} P3D, {} textures, {} audio)\n",
                                  result.added, result.updated, result.removed,
                                  result.file_count, result.p3d_count, result.paa_count, result.audio_count);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.find("schema version mismatch") != std::string::npos ||
            msg.find("incompatible") != std::string::npos) {
            std::cerr << "Schema outdated, removing old DB and rebuilding...\n";
            std::error_code ec;
            fs::remove(cfg.db, ec);
            fs::remove(cfg.db + "-wal", ec);
            fs::remove(cfg.db + "-shm", ec);
            do_build(cfg, on_demand);
            return;
        }
        std::cerr << "Error: updating database: " << msg << '\n';
        return;
    }

    std::error_code ec;
    auto size = fs::file_size(cfg.db, ec);
    if (!ec) {
        std::cerr << std::format("Database {} ({:.1f} MB)\n", cfg.db, static_cast<double>(size) / 1024 / 1024);
    }
}

static void do_find(const std::string& db_path, const std::string& pattern, bool pretty) {
    if (db_path.empty()) {
        std::cerr << "Error: -db is required for -find.\n";
        return;
    }

    auto db = armatools::pboindex::DB::open(db_path);
    auto results = db.find_files(pattern);

    json arr = json::array();
    for (const auto& r : results) {
        arr.push_back({
            {"pbo_path", r.pbo_path},
            {"prefix", r.prefix},
            {"file_path", r.file_path},
            {"data_size", r.data_size},
        });
    }

    if (pretty) std::cout << std::setw(2) << arr << '\n';
    else std::cout << arr << '\n';
    std::cerr << "Found " << results.size() << " matches\n";
}

static void do_info(const std::string& db_path) {
    if (db_path.empty()) {
        std::cerr << "Error: -db is required for -info.\n";
        return;
    }

    auto db = armatools::pboindex::DB::open(db_path);
    auto stats = db.stats();

    std::error_code ec;
    auto size = fs::file_size(db_path, ec);

    std::cout << "Database:       " << db_path << '\n';
    if (!ec) {
        std::cout << std::format("Size:           {:.1f} MB\n", static_cast<double>(size) / 1024 / 1024);
    }
    std::cout << "Schema version: " << stats.schema_version << '\n';
    std::cout << "Created:        " << stats.created_at << '\n';
    if (!stats.arma3_dir.empty()) std::cout << "Arma 3:         " << stats.arma3_dir << '\n';
    if (!stats.workshop_dir.empty()) std::cout << "Workshop:       " << stats.workshop_dir << '\n';
    if (!stats.ofp_dir.empty()) std::cout << "OFP/CWA:        " << stats.ofp_dir << '\n';
    if (!stats.arma1_dir.empty()) std::cout << "Arma 1:         " << stats.arma1_dir << '\n';
    if (!stats.arma2_dir.empty()) std::cout << "Arma 2:         " << stats.arma2_dir << '\n';
    for (const auto& m : stats.mod_dirs) std::cout << "Mod:            " << m << '\n';
    std::cout << std::format("PBOs:           {} ({} with prefix)\n", stats.pbo_count, stats.pbos_with_prefix);
    std::cout << "Files:          " << stats.file_count << '\n';
    std::cout << "P3D models:     " << stats.p3d_model_count << '\n';
    std::cout << "Textures:       " << stats.texture_count << '\n';
    std::cout << "Audio files:    " << stats.audio_file_count << '\n';
    std::cout << std::format("Total data:     {:.1f} MB\n", static_cast<double>(stats.total_data_size) / 1024 / 1024);
}

static void print_usage() {
    std::cerr << "Usage: a3db [flags] [output.db]\n\n"
              << "PBO database tool for fast file lookup.\n\n"
              << "Modes:\n"
              << "  Build  (default)  Scan PBOs, write SQLite database\n"
              << "  Update (-update)  Incremental update (only changed PBOs)\n"
              << "  Find   (-find)    Search database for files\n"
              << "  Info   (-info)    Show database statistics\n\n"
              << "Flags:\n"
              << "  -config <path>    Config file with game paths (JSON)\n"
              << "  -arma3 <dir>      Arma 3 directory\n"
              << "  -workshop <dir>   Workshop directory\n"
              << "  -ofp <dir>        OFP / Arma: Cold War Assault directory\n"
              << "  -arma1 <dir>      Arma: Armed Assault directory\n"
              << "  -arma2 <dir>      Arma 2 directory\n"
              << "  -db <path>        Database file path\n"
              << "  -ondemand         Skip eager P3D/PAA/audio parsing\n"
              << "  -find <pattern>   Find files matching glob pattern\n"
              << "  -info             Show database statistics\n"
              << "  -update           Incremental update\n"
              << "  --pretty          Pretty-print JSON output (for -find)\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string arma3_flag;
    std::string workshop_flag;
    std::string ofp_flag;
    std::string arma1_flag;
    std::string arma2_flag;
    std::string db_flag;
    bool on_demand = false;
    std::string find_pattern;
    bool info_flag = false;
    bool update_flag = false;
    bool pretty = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-config") == 0 && i + 1 < argc) config_path = argv[++i];
        else if (std::strcmp(argv[i], "-arma3") == 0 && i + 1 < argc) arma3_flag = argv[++i];
        else if (std::strcmp(argv[i], "-workshop") == 0 && i + 1 < argc) workshop_flag = argv[++i];
        else if (std::strcmp(argv[i], "-ofp") == 0 && i + 1 < argc) ofp_flag = argv[++i];
        else if (std::strcmp(argv[i], "-arma1") == 0 && i + 1 < argc) arma1_flag = argv[++i];
        else if (std::strcmp(argv[i], "-arma2") == 0 && i + 1 < argc) arma2_flag = argv[++i];
        else if (std::strcmp(argv[i], "-db") == 0 && i + 1 < argc) db_flag = argv[++i];
        else if (std::strcmp(argv[i], "-ondemand") == 0) on_demand = true;
        else if (std::strcmp(argv[i], "-find") == 0 && i + 1 < argc) find_pattern = argv[++i];
        else if (std::strcmp(argv[i], "-info") == 0) info_flag = true;
        else if (std::strcmp(argv[i], "-update") == 0) update_flag = true;
        else if (std::strcmp(argv[i], "--pretty") == 0) pretty = true;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    // Load config
    Config cfg;
    if (!config_path.empty()) {
        try {
            cfg = load_config(config_path);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n';
            return 1;
        }
    }

    // Override with flags
    if (!arma3_flag.empty()) cfg.arma3 = arma3_flag;
    if (!workshop_flag.empty()) cfg.workshop = workshop_flag;
    if (!ofp_flag.empty()) cfg.ofp = ofp_flag;
    if (!arma1_flag.empty()) cfg.arma1 = arma1_flag;
    if (!arma2_flag.empty()) cfg.arma2 = arma2_flag;
    if (!db_flag.empty()) cfg.db = db_flag;

    // Positional arg as db path for build
    if (cfg.db.empty() && !positional.empty()) {
        cfg.db = positional[0];
    }

    if (!find_pattern.empty()) {
        do_find(cfg.db, find_pattern, pretty);
    } else if (info_flag) {
        do_info(cfg.db);
    } else if (update_flag) {
        do_update(cfg, on_demand);
    } else {
        do_build(cfg, on_demand);
    }

    return 0;
}
