#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace armatools::pboindex {

// PBORef describes a PBO file and its prefix.
struct PBORef {
    std::string path;   // filesystem path to .pbo file
    std::string prefix; // prefix from PBO header extensions
};

// ResolveResult describes where a model file can be found.
struct ResolveResult {
    std::string pbo_path;   // path to .pbo file on disk
    std::string prefix;     // PBO prefix
    std::string entry_name; // path inside PBO (relative to prefix)
    std::string full_path;  // original model path
};

// Index maps normalized prefixes to PBO references for fast model path resolution.
class Index {
public:
    explicit Index(std::vector<PBORef> refs);

    int size() const;

    // Resolve maps a model path to a PBO and an entry name within that PBO.
    // Returns true if resolved, false if no matching PBO found.
    bool resolve(const std::string& model_path, ResolveResult& result) const;

private:
    std::vector<PBORef> refs_;
};

// ScanDir finds all .pbo files in dir and reads their prefixes.
std::vector<PBORef> scan_dir(const std::string& dir);

// DiscoverPBOPaths returns all .pbo file paths from standard Arma 3 locations.
// GameDirs holds optional paths for legacy Arma game directories.
struct GameDirs {
    std::string ofp_dir;    // Operation Flashpoint / Arma: Cold War Assault
    std::string arma1_dir;  // Arma: Armed Assault
    std::string arma2_dir;  // Arma 2
};

// PBOPath holds a PBO file path and its source identifier.
struct PBOPath {
    std::string path;
    std::string source; // "arma3", "workshop", "ofp", "arma1", "arma2", "custom"
};

std::vector<std::string> discover_pbo_paths(const std::string& arma3_dir,
                                             const std::string& workshop_dir,
                                             const std::vector<std::string>& mod_dirs,
                                             const GameDirs& game_dirs = {});

// DiscoverPBOPathsWithSource returns PBO paths tagged with their source.
std::vector<PBOPath> discover_pbo_paths_with_source(
    const std::string& arma3_dir,
    const std::string& workshop_dir,
    const std::vector<std::string>& mod_dirs,
    const GameDirs& game_dirs = {});

// DiscoverPBOs finds all PBO files from standard locations and reads their prefixes.
std::vector<PBORef> discover_pbos(const std::string& arma3_dir,
                                   const std::string& workshop_dir,
                                   const std::vector<std::string>& mod_dirs,
                                   const GameDirs& game_dirs = {});

// FindResult describes a file found in the database.
struct FindResult {
    std::string pbo_path;
    std::string prefix;
    std::string file_path;
    uint32_t data_size = 0;
};

// DirEntry represents an entry in a directory listing.
struct DirEntry {
    std::string name;
    bool is_dir = false;
    std::vector<FindResult> files; // non-empty only for files
};

// DBStats contains aggregate database statistics.
struct DBStats {
    std::string schema_version;
    std::string created_at;
    std::string arma3_dir;
    std::string workshop_dir;
    std::vector<std::string> mod_dirs;
    std::string ofp_dir;
    std::string arma1_dir;
    std::string arma2_dir;
    int pbo_count = 0;
    int pbos_with_prefix = 0;
    int file_count = 0;
    int64_t total_data_size = 0;
    int p3d_model_count = 0;
    int texture_count = 0;
    int audio_file_count = 0;
};

// ModelBBox holds bounding box data for a P3D model.
struct ModelBBox {
    float bbox_min[3]{};
    float bbox_max[3]{};
    float bbox_center[3]{};
    float bbox_radius = 0;
    float mi_max[3]{};
    float vis_min[3]{};
    float vis_max[3]{};
    float vis_center[3]{};
};

// BuildProgress reports the current state of a build/update operation.
struct BuildProgress {
    std::string phase;     // "discovery", "pbo", "p3d", "paa", "ogg", "audio", "commit"
    int pbo_index = 0;
    int pbo_total = 0;
    std::string pbo_path;
    std::string file_name;
    int file_index = 0;
    int file_total = 0;
};

using BuildProgressFunc = std::function<void(const BuildProgress&)>;

// BuildOptions controls what metadata is eagerly indexed during build/update.
struct BuildOptions {
    bool on_demand_metadata = false;
};

// BuildResult holds counts from a build/update operation.
struct BuildResult {
    int pbo_count = 0;
    int file_count = 0;
    int p3d_count = 0;
    int paa_count = 0;
    int audio_count = 0;
};

// UpdateResult holds counts from an update operation.
struct UpdateResult {
    int added = 0;
    int updated = 0;
    int removed = 0;
    int file_count = 0;
    int p3d_count = 0;
    int paa_count = 0;
    int audio_count = 0;
};

// DB wraps a SQLite database of PBO file metadata.
class DB {
public:
    ~DB();
    DB(DB&& other) noexcept;
    DB& operator=(DB&& other) noexcept;

    // BuildDB creates a new SQLite database with PBO metadata.
    static BuildResult build_db(const std::string& db_path,
                                const std::string& arma3_dir,
                                const std::string& workshop_dir,
                                const std::vector<std::string>& mod_dirs,
                                const BuildOptions& opts = {},
                                BuildProgressFunc progress = nullptr,
                                const GameDirs& game_dirs = {});

    // UpdateDB incrementally updates an existing database.
    static UpdateResult update_db(const std::string& db_path,
                                   const std::string& arma3_dir,
                                   const std::string& workshop_dir,
                                   const std::vector<std::string>& mod_dirs,
                                   const BuildOptions& opts = {},
                                   BuildProgressFunc progress = nullptr,
                                   const GameDirs& game_dirs = {});

    // OpenDB opens an existing PBO database for reading.
    static DB open(const std::string& path);

    // Index builds a prefix Index from the database.
    Index index() const;

    // Stats returns aggregate database statistics.
    DBStats stats() const;

    // ListDir returns immediate children of a virtual directory path.
    std::vector<DirEntry> list_dir(const std::string& dir) const;

    // AllFiles returns every file in the database.
    std::vector<FindResult> all_files() const;

    // FindFiles searches for files matching a glob pattern.
    // If source is non-empty, only files from PBOs with that source are returned.
    std::vector<FindResult> find_files(const std::string& pattern,
                                       const std::string& source = "") const;

    // ListPBOPaths returns all indexed PBO file paths sorted alphabetically.
    std::vector<std::string> list_pbo_paths() const;

    // QueryModelBBoxes returns bounding box data for all P3D models.
    std::unordered_map<std::string, ModelBBox> query_model_bboxes() const;

    // QueryModelTextures returns texture paths for the given model paths.
    std::unordered_map<std::string, std::vector<std::string>>
        query_model_textures(const std::vector<std::string>& models) const;

    // QueryModelPaths returns a map from lowercase full virtual path to the
    // original-case basename (without extension) for all P3D models.
    // Example: "a3/structures_f/data/ammostore2.p3d" -> "AmmoStore2"
    std::unordered_map<std::string, std::string> query_model_paths() const;

    // QuerySources returns the distinct source values from the pbos table.
    std::vector<std::string> query_sources() const;

    // ListDirForSource returns directory entries filtered by PBO source.
    std::vector<DirEntry> list_dir_for_source(const std::string& dir,
                                               const std::string& source) const;

private:
    DB();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace armatools::pboindex
