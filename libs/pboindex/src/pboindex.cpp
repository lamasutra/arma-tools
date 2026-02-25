#include "armatools/pboindex.h"
#include "armatools/armapath.h"
#include "armatools/ogg.h"
#include "armatools/p3d.h"
#include "armatools/paa.h"
#include "armatools/pbo.h"
#include "armatools/wss.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace fs = std::filesystem;

namespace armatools::pboindex {

static bool gmtime_utc(std::time_t tt, std::tm& tm_val) {
#if defined(_WIN32)
    return gmtime_s(&tm_val, &tt) == 0;
#else
    return gmtime_r(&tt, &tm_val) != nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Schema — matches Go pboindex db.go exactly
// ---------------------------------------------------------------------------

static constexpr const char* schema_sql = R"SQL(
CREATE TABLE meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE pbos (
    id INTEGER PRIMARY KEY,
    path TEXT UNIQUE NOT NULL,
    prefix TEXT NOT NULL DEFAULT '',
    file_size INTEGER NOT NULL DEFAULT 0,
    mod_time TEXT NOT NULL DEFAULT '',
    source TEXT NOT NULL DEFAULT ''
);
CREATE TABLE pbo_extensions (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    key TEXT NOT NULL,
    value TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (pbo_id, key)
);
CREATE TABLE dirs (
    id INTEGER PRIMARY KEY,
    parent_id INTEGER REFERENCES dirs(id),
    name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE
);
CREATE INDEX idx_dirs_parent_id ON dirs(parent_id);
CREATE TABLE files (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    dir_id INTEGER REFERENCES dirs(id),
    path TEXT NOT NULL,
    original_size INTEGER NOT NULL DEFAULT 0,
    data_size INTEGER NOT NULL DEFAULT 0,
    timestamp INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_files_pbo_id ON files(pbo_id);
CREATE INDEX idx_files_dir_id ON files(dir_id);
CREATE TABLE p3d_models (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    path TEXT NOT NULL,
    name TEXT NOT NULL,
    format TEXT NOT NULL,
    size_source TEXT NOT NULL DEFAULT '',
    size_x REAL NOT NULL DEFAULT 0,
    size_y REAL NOT NULL DEFAULT 0,
    size_z REAL NOT NULL DEFAULT 0,
    bbox_min_x REAL NOT NULL DEFAULT 0,
    bbox_min_y REAL NOT NULL DEFAULT 0,
    bbox_min_z REAL NOT NULL DEFAULT 0,
    bbox_max_x REAL NOT NULL DEFAULT 0,
    bbox_max_y REAL NOT NULL DEFAULT 0,
    bbox_max_z REAL NOT NULL DEFAULT 0,
    bbox_center_x REAL NOT NULL DEFAULT 0,
    bbox_center_y REAL NOT NULL DEFAULT 0,
    bbox_center_z REAL NOT NULL DEFAULT 0,
    bbox_radius REAL NOT NULL DEFAULT 0,
    mi_max_x REAL NOT NULL DEFAULT 0,
    mi_max_y REAL NOT NULL DEFAULT 0,
    mi_max_z REAL NOT NULL DEFAULT 0,
    vis_min_x REAL NOT NULL DEFAULT 0,
    vis_min_y REAL NOT NULL DEFAULT 0,
    vis_min_z REAL NOT NULL DEFAULT 0,
    vis_max_x REAL NOT NULL DEFAULT 0,
    vis_max_y REAL NOT NULL DEFAULT 0,
    vis_max_z REAL NOT NULL DEFAULT 0,
    vis_center_x REAL NOT NULL DEFAULT 0,
    vis_center_y REAL NOT NULL DEFAULT 0,
    vis_center_z REAL NOT NULL DEFAULT 0
);
CREATE INDEX idx_p3d_models_pbo_id ON p3d_models(pbo_id);
CREATE TABLE textures (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    path TEXT NOT NULL,
    name TEXT NOT NULL,
    format TEXT NOT NULL DEFAULT '',
    data_size INTEGER NOT NULL DEFAULT 0,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_textures_pbo_id ON textures(pbo_id);
CREATE TABLE audio_files (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    path TEXT NOT NULL,
    name TEXT NOT NULL,
    format TEXT NOT NULL DEFAULT '',
    encoder TEXT NOT NULL DEFAULT '',
    sample_rate INTEGER NOT NULL DEFAULT 0,
    channels INTEGER NOT NULL DEFAULT 0,
    data_size INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_audio_files_pbo_id ON audio_files(pbo_id);
CREATE TABLE model_textures (
    pbo_id INTEGER NOT NULL REFERENCES pbos(id),
    model_path TEXT NOT NULL,
    texture_path TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT 'lod'
);
CREATE INDEX idx_model_textures_pbo_id ON model_textures(pbo_id);
CREATE INDEX idx_model_textures_model ON model_textures(model_path);
CREATE INDEX idx_pbos_source ON pbos(source);
)SQL";

static constexpr const char* schema_version = "10";

// ---------------------------------------------------------------------------
// SQLite helpers
// ---------------------------------------------------------------------------

class SqliteStmt {
public:
    SqliteStmt() = default;
    SqliteStmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(
                std::format("sqlite3_prepare_v2: {}", sqlite3_errmsg(db)));
    }
    ~SqliteStmt() { if (stmt_) sqlite3_finalize(stmt_); }
    SqliteStmt(const SqliteStmt&) = delete;
    SqliteStmt& operator=(const SqliteStmt&) = delete;
    SqliteStmt(SqliteStmt&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }
    SqliteStmt& operator=(SqliteStmt&& o) noexcept {
        if (this != &o) { if (stmt_) sqlite3_finalize(stmt_); stmt_ = o.stmt_; o.stmt_ = nullptr; }
        return *this;
    }

    sqlite3_stmt* get() const { return stmt_; }

    void reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }

    void bind_text(int idx, const std::string& v) {
        sqlite3_bind_text(stmt_, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bind_int(int idx, int v) { sqlite3_bind_int(stmt_, idx, v); }
    void bind_int64(int idx, int64_t v) { sqlite3_bind_int64(stmt_, idx, v); }
    void bind_double(int idx, double v) { sqlite3_bind_double(stmt_, idx, v); }
    void bind_null(int idx) { sqlite3_bind_null(stmt_, idx); }

    int step() { return sqlite3_step(stmt_); }

    void exec() {
        int rc = step();
        if (rc != SQLITE_DONE && rc != SQLITE_ROW)
            throw std::runtime_error(
                std::format("sqlite3_step: {}", sqlite3_errmsg(sqlite3_db_handle(stmt_))));
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

static void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error(std::format("sqlite3_exec: {}", msg));
    }
}

static sqlite3* open_db_handle(const std::string& path, int flags) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db ? sqlite3_errmsg(db) : "out of memory";
        if (db) sqlite3_close(db);
        throw std::runtime_error(std::format("sqlite3_open_v2({}): {}", path, msg));
    }
    return db;
}

// ---------------------------------------------------------------------------
// String / path helpers
// ---------------------------------------------------------------------------

static bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// Compute the virtual directory path for a file within a PBO.
// prefix: raw PBO prefix (e.g. "a3\\structures_f")
// filename: raw entry filename (e.g. "data\\cargo_house_v1.p3d")
// Returns normalized path like "a3/structures_f/data", or "" if at root.
static std::string virtual_dir_path(const std::string& prefix,
                                     const std::string& filename) {
    std::string fp = armapath::to_slash_lower(filename);
    std::string full = fp;
    if (!prefix.empty()) {
        std::string pfx = armapath::to_slash_lower(prefix);
        while (!pfx.empty() && pfx.back() == '/') pfx.pop_back();
        if (!pfx.empty()) full = pfx + "/" + fp;
    }
    auto pos = full.rfind('/');
    return (pos != std::string::npos) ? full.substr(0, pos) : "";
}

// Extract basename without extension from a path.
// "data\\cargo_house_v1.p3d" → "cargo_house_v1"
static std::string basename_no_ext(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    std::string base = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    auto dot = base.rfind('.');
    return (dot != std::string::npos) ? base.substr(0, dot) : base;
}

// Extract basename from a raw path (last component after / or \), lowercased.
static std::string file_basename_lower(const std::string& raw_path) {
    auto pos = raw_path.find_last_of("/\\");
    std::string base = (pos != std::string::npos) ? raw_path.substr(pos + 1) : raw_path;
    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lower;
}

// ---------------------------------------------------------------------------
// DirPathCache — mirrors Go's dirPathCache
// ---------------------------------------------------------------------------

class DirPathCache {
public:
    DirPathCache(sqlite3* db)
        : insert_stmt_(db, "INSERT OR IGNORE INTO dirs (parent_id, name, path) VALUES (?1, ?2, ?3)")
        , select_stmt_(db, "SELECT id FROM dirs WHERE path = ?1")
    {}

    // Ensure all components of dir_path exist in dirs table.
    // Returns the id of the deepest directory.
    int64_t ensure_dir(const std::string& dir_path) {
        if (dir_path.empty()) return -1;

        auto it = cache_.find(dir_path);
        if (it != cache_.end()) return it->second;

        // Split on /
        std::vector<std::string> parts;
        std::istringstream ss(dir_path);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!part.empty()) parts.push_back(part);
        }

        int64_t parent_id = -1; // -1 means NULL parent
        std::string sub;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i > 0) sub += "/";
            sub += parts[i];

            auto cit = cache_.find(sub);
            if (cit != cache_.end()) {
                parent_id = cit->second;
                continue;
            }

            insert_stmt_.reset();
            if (parent_id >= 0) {
                insert_stmt_.bind_int64(1, parent_id);
            } else {
                insert_stmt_.bind_null(1);
            }
            insert_stmt_.bind_text(2, parts[i]);
            insert_stmt_.bind_text(3, sub);
            insert_stmt_.exec();

            select_stmt_.reset();
            select_stmt_.bind_text(1, sub);
            select_stmt_.step();
            int64_t id = sqlite3_column_int64(select_stmt_.get(), 0);
            select_stmt_.reset(); // Release read lock on dirs table

            cache_[sub] = id;
            parent_id = id;
        }

        return parent_id;
    }

private:
    std::unordered_map<std::string, int64_t> cache_;
    SqliteStmt insert_stmt_;
    SqliteStmt select_stmt_;
};

// ---------------------------------------------------------------------------
// Index
// ---------------------------------------------------------------------------

Index::Index(std::vector<PBORef> refs) : refs_(std::move(refs)) {
    // Sort by prefix length descending for longest-prefix matching.
    std::sort(refs_.begin(), refs_.end(), [](const PBORef& a, const PBORef& b) {
        return a.prefix.size() > b.prefix.size();
    });
}

int Index::size() const {
    return static_cast<int>(refs_.size());
}

bool Index::resolve(const std::string& model_path, ResolveResult& result) const {
    std::string normalized = armapath::to_slash_lower(model_path);

    for (const auto& ref : refs_) {
        if (ref.prefix.empty()) continue;
        std::string prefix = armapath::to_slash_lower(ref.prefix);
        // Ensure prefix ends with /
        if (!prefix.empty() && prefix.back() != '/')
            prefix += '/';

        if (starts_with(normalized, prefix)) {
            result.pbo_path = ref.path;
            result.prefix = ref.prefix;
            result.entry_name = normalized.substr(prefix.size());
            result.full_path = normalized;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// scan_dir, discover_pbo_paths, discover_pbos
// ---------------------------------------------------------------------------

std::vector<PBORef> scan_dir(const std::string& dir) {
    std::vector<PBORef> refs;
    std::error_code ec;

    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) continue;
        if (!it->is_regular_file(ec) || ec) continue;

        auto path = it->path();
        if (!ends_with_ci(path.string(), ".pbo")) continue;

        std::string pbo_path = path.string();
        std::string prefix;

        try {
            std::ifstream f(pbo_path, std::ios::binary);
            if (f.is_open()) {
                auto p = pbo::read(f);
                auto pit = p.extensions.find("prefix");
                if (pit != p.extensions.end())
                    prefix = pit->second;
            }
        } catch (...) {
            // Skip unreadable PBOs
        }

        refs.push_back(PBORef{.path = std::move(pbo_path), .prefix = std::move(prefix)});
    }
    return refs;
}

std::vector<std::string> discover_pbo_paths(const std::string& arma3_dir,
                                             const std::string& workshop_dir,
                                             const std::vector<std::string>& mod_dirs,
                                             const GameDirs& game_dirs) {
    std::vector<std::string> paths;
    std::error_code ec;

    auto collect = [&](const std::string& dir) {
        if (dir.empty()) return;
        if (!fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) continue;
            if (!it->is_regular_file(ec) || ec) continue;
            auto p = it->path();
            if (ends_with_ci(p.string(), ".pbo"))
                paths.push_back(p.string());
        }
    };

    collect(arma3_dir);
    collect(workshop_dir);
    for (const auto& d : mod_dirs)
        collect(d);
    collect(game_dirs.ofp_dir);
    collect(game_dirs.arma1_dir);
    collect(game_dirs.arma2_dir);

    return paths;
}

std::vector<PBOPath> discover_pbo_paths_with_source(
    const std::string& arma3_dir,
    const std::string& workshop_dir,
    const std::vector<std::string>& mod_dirs,
    const GameDirs& game_dirs) {
    std::vector<PBOPath> paths;
    std::error_code ec;

    auto collect = [&](const std::string& dir, const std::string& source) {
        if (dir.empty()) return;
        if (!fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) continue;
            if (!it->is_regular_file(ec) || ec) continue;
            auto p = it->path();
            if (ends_with_ci(p.string(), ".pbo"))
                paths.push_back(PBOPath{.path = p.string(), .source = source});
        }
    };

    collect(arma3_dir, "arma3");
    collect(workshop_dir, "workshop");
    for (const auto& d : mod_dirs)
        collect(d, "custom");
    collect(game_dirs.ofp_dir, "ofp");
    collect(game_dirs.arma1_dir, "arma1");
    collect(game_dirs.arma2_dir, "arma2");

    return paths;
}

std::vector<PBORef> discover_pbos(const std::string& arma3_dir,
                                   const std::string& workshop_dir,
                                   const std::vector<std::string>& mod_dirs,
                                   const GameDirs& game_dirs) {
    auto pbo_paths = discover_pbo_paths(arma3_dir, workshop_dir, mod_dirs, game_dirs);
    std::vector<PBORef> refs;
    refs.reserve(pbo_paths.size());

    for (auto& pbo_path : pbo_paths) {
        std::string prefix;
        try {
            std::ifstream f(pbo_path, std::ios::binary);
            if (f.is_open()) {
                auto p = pbo::read(f);
                auto pit = p.extensions.find("prefix");
                if (pit != p.extensions.end())
                    prefix = pit->second;
            }
        } catch (...) {
            // Skip unreadable PBOs
        }
        refs.push_back(PBORef{.path = std::move(pbo_path), .prefix = std::move(prefix)});
    }
    return refs;
}

// ---------------------------------------------------------------------------
// DB::Impl
// ---------------------------------------------------------------------------

struct DB::Impl {
    sqlite3* db = nullptr;

    ~Impl() {
        if (db) sqlite3_close(db);
    }
};

DB::DB() : impl_(std::make_unique<Impl>()) {}

DB::~DB() = default;

DB::DB(DB&& other) noexcept = default;
DB& DB::operator=(DB&& other) noexcept = default;

// ---------------------------------------------------------------------------
// Indexing helpers (static, used during build)
// ---------------------------------------------------------------------------

static int64_t insert_pbo(sqlite3* db, SqliteStmt& stmt,
                           const std::string& path, const std::string& prefix,
                           int64_t file_size, const std::string& mod_time,
                           const std::string& source = "") {
    stmt.reset();
    stmt.bind_text(1, path);
    stmt.bind_text(2, prefix);
    stmt.bind_int64(3, file_size);
    stmt.bind_text(4, mod_time);
    stmt.bind_text(5, source);
    stmt.exec();
    return sqlite3_last_insert_rowid(db);
}

static void insert_file(SqliteStmt& stmt,
                         int64_t pbo_id, int64_t dir_id,
                         const std::string& path,
                         uint32_t original_size, uint32_t data_size,
                         uint32_t timestamp) {
    stmt.reset();
    stmt.bind_int64(1, pbo_id);
    if (dir_id >= 0) {
        stmt.bind_int64(2, dir_id);
    } else {
        stmt.bind_null(2);
    }
    stmt.bind_text(3, path);
    stmt.bind_int(4, static_cast<int>(original_size));
    stmt.bind_int(5, static_cast<int>(data_size));
    stmt.bind_int(6, static_cast<int>(timestamp));
    stmt.exec();
}

static void index_p3d(sqlite3* /*db*/, SqliteStmt& model_stmt, SqliteStmt& mtex_stmt,
                      int64_t pbo_id, const std::string& entry_path,
                      std::ifstream& f, const pbo::Entry& entry) {
    try {
        std::ostringstream buf;
        pbo::extract_file(f, entry, buf);
        std::string data = buf.str();
        std::istringstream is(data);

        auto model = p3d::read(is);

        std::string name = basename_no_ext(entry_path);

        float mi_max_x = 0, mi_max_y = 0, mi_max_z = 0;
        if (model.model_info) {
            mi_max_x = model.model_info->bounding_box_max[0];
            mi_max_y = model.model_info->bounding_box_max[1];
            mi_max_z = model.model_info->bounding_box_max[2];
        }

        auto size_res = p3d::calculate_size(model);
        std::string size_source;
        float size_x = 0, size_y = 0, size_z = 0;
        float bbox_min_x = 0, bbox_min_y = 0, bbox_min_z = 0;
        float bbox_max_x = 0, bbox_max_y = 0, bbox_max_z = 0;
        float bbox_center_x = 0, bbox_center_y = 0, bbox_center_z = 0;
        float bbox_radius = 0;
        if (size_res.info) {
            size_source = size_res.info->source;
            size_x = size_res.info->dimensions[0];
            size_y = size_res.info->dimensions[1];
            size_z = size_res.info->dimensions[2];
            bbox_min_x = size_res.info->bbox_min[0];
            bbox_min_y = size_res.info->bbox_min[1];
            bbox_min_z = size_res.info->bbox_min[2];
            bbox_max_x = size_res.info->bbox_max[0];
            bbox_max_y = size_res.info->bbox_max[1];
            bbox_max_z = size_res.info->bbox_max[2];
            bbox_center_x = size_res.info->bbox_center[0];
            bbox_center_y = size_res.info->bbox_center[1];
            bbox_center_z = size_res.info->bbox_center[2];
            bbox_radius = size_res.info->bbox_radius;
        }

        float vis_min_x = 0, vis_min_y = 0, vis_min_z = 0;
        float vis_max_x = 0, vis_max_y = 0, vis_max_z = 0;
        float vis_center_x = 0, vis_center_y = 0, vis_center_z = 0;
        auto vis = p3d::visual_bbox(model);
        if (vis) {
            vis_min_x = vis->bbox_min[0];
            vis_min_y = vis->bbox_min[1];
            vis_min_z = vis->bbox_min[2];
            vis_max_x = vis->bbox_max[0];
            vis_max_y = vis->bbox_max[1];
            vis_max_z = vis->bbox_max[2];
            vis_center_x = vis->bbox_center[0];
            vis_center_y = vis->bbox_center[1];
            vis_center_z = vis->bbox_center[2];
        }

        model_stmt.reset();
        model_stmt.bind_int64(1, pbo_id);       // pbo_id
        model_stmt.bind_text(2, entry_path);     // path (raw)
        model_stmt.bind_text(3, name);           // name
        model_stmt.bind_text(4, model.format);   // format
        model_stmt.bind_text(5, size_source);    // size_source
        model_stmt.bind_double(6, size_x);       // size_x
        model_stmt.bind_double(7, size_y);       // size_y
        model_stmt.bind_double(8, size_z);       // size_z
        model_stmt.bind_double(9, bbox_min_x);
        model_stmt.bind_double(10, bbox_min_y);
        model_stmt.bind_double(11, bbox_min_z);
        model_stmt.bind_double(12, bbox_max_x);
        model_stmt.bind_double(13, bbox_max_y);
        model_stmt.bind_double(14, bbox_max_z);
        model_stmt.bind_double(15, bbox_center_x);
        model_stmt.bind_double(16, bbox_center_y);
        model_stmt.bind_double(17, bbox_center_z);
        model_stmt.bind_double(18, bbox_radius);
        model_stmt.bind_double(19, mi_max_x);
        model_stmt.bind_double(20, mi_max_y);
        model_stmt.bind_double(21, mi_max_z);
        model_stmt.bind_double(22, vis_min_x);
        model_stmt.bind_double(23, vis_min_y);
        model_stmt.bind_double(24, vis_min_z);
        model_stmt.bind_double(25, vis_max_x);
        model_stmt.bind_double(26, vis_max_y);
        model_stmt.bind_double(27, vis_max_z);
        model_stmt.bind_double(28, vis_center_x);
        model_stmt.bind_double(29, vis_center_y);
        model_stmt.bind_double(30, vis_center_z);
        model_stmt.exec();

        // Collect unique textures from all LODs — source "lod"
        std::vector<std::string> seen;
        for (const auto& lod : model.lods) {
            for (const auto& tex : lod.textures) {
                std::string norm = armapath::to_slash_lower(tex);
                if (norm.empty()) continue;
                if (armapath::is_procedural_texture(norm)) continue;
                if (std::find(seen.begin(), seen.end(), norm) == seen.end()) {
                    seen.push_back(norm);
                    mtex_stmt.reset();
                    mtex_stmt.bind_int64(1, pbo_id);
                    mtex_stmt.bind_text(2, entry_path);
                    mtex_stmt.bind_text(3, norm);
                    mtex_stmt.bind_text(4, "lod");
                    mtex_stmt.exec();
                }
            }
        }

        // Collect unique material references — source "material"
        std::vector<std::string> seen_mat;
        for (const auto& lod : model.lods) {
            for (const auto& mat : lod.materials) {
                std::string norm = armapath::to_slash_lower(mat);
                if (norm.empty()) continue;
                if (std::find(seen_mat.begin(), seen_mat.end(), norm) == seen_mat.end()) {
                    seen_mat.push_back(norm);
                    mtex_stmt.reset();
                    mtex_stmt.bind_int64(1, pbo_id);
                    mtex_stmt.bind_text(2, entry_path);
                    mtex_stmt.bind_text(3, norm);
                    mtex_stmt.bind_text(4, "material");
                    mtex_stmt.exec();
                }
            }
        }
    } catch (...) {
        // Skip models that fail to parse
    }
}

static void index_paa(SqliteStmt& stmt, int64_t pbo_id,
                      const std::string& entry_path,
                      std::ifstream& f,
                      const pbo::Entry& entry) {
    try {
        std::ostringstream buf;
        pbo::extract_file(f, entry, buf);
        std::string data = buf.str();
        std::istringstream is(data);

        auto hdr = paa::read_header(is);

        stmt.reset();
        stmt.bind_int64(1, pbo_id);
        stmt.bind_text(2, entry_path);
        stmt.bind_text(3, basename_no_ext(entry_path));
        stmt.bind_text(4, hdr.format);
        stmt.bind_int(5, static_cast<int>(entry.data_size));
        stmt.bind_int(6, hdr.width);
        stmt.bind_int(7, hdr.height);
        stmt.exec();
    } catch (...) {
        // Skip textures that fail to parse
    }
}

static void index_ogg(SqliteStmt& stmt, int64_t pbo_id,
                      const std::string& entry_path,
                      std::ifstream& f,
                      const pbo::Entry& entry) {
    try {
        std::ostringstream buf;
        pbo::extract_file(f, entry, buf);
        std::string data = buf.str();
        std::istringstream is(data);

        auto hdr = ogg::read_header(is);

        stmt.reset();
        stmt.bind_int64(1, pbo_id);
        stmt.bind_text(2, entry_path);
        stmt.bind_text(3, basename_no_ext(entry_path));
        stmt.bind_text(4, "OGG");
        stmt.bind_text(5, hdr.encoder);
        stmt.bind_int(6, hdr.sample_rate);
        stmt.bind_int(7, hdr.channels);
        stmt.bind_int(8, static_cast<int>(entry.data_size));
        stmt.exec();
    } catch (...) {
        // Skip audio files that fail to parse
    }
}

static void index_audio(SqliteStmt& stmt, int64_t pbo_id,
                        const std::string& entry_path,
                        std::ifstream& f,
                        const pbo::Entry& entry) {
    try {
        std::ostringstream buf;
        pbo::extract_file(f, entry, buf);
        std::string data = buf.str();
        std::istringstream is(data);

        auto audio = wss::read(is);

        stmt.reset();
        stmt.bind_int64(1, pbo_id);
        stmt.bind_text(2, entry_path);
        stmt.bind_text(3, basename_no_ext(entry_path));
        stmt.bind_text(4, audio.format);
        stmt.bind_text(5, "");
        stmt.bind_int(6, static_cast<int>(audio.sample_rate));
        stmt.bind_int(7, static_cast<int>(audio.channels));
        stmt.bind_int(8, static_cast<int>(entry.data_size));
        stmt.exec();
    } catch (...) {
        // Skip audio files that fail to parse
    }
}

// Index a single PBO: insert into pbos, files, dirs, extensions, and metadata tables.
struct PBOIndexCounts {
    int files = 0;
    int p3d = 0;
    int paa = 0;
    int audio = 0;
};

static PBOIndexCounts index_single_pbo(
    sqlite3* db,
    SqliteStmt& pbo_stmt, SqliteStmt& file_stmt,
    SqliteStmt& ext_stmt,
    SqliteStmt& model_stmt, SqliteStmt& mtex_stmt,
    SqliteStmt& paa_stmt, SqliteStmt& audio_stmt,
    DirPathCache& dir_cache,
    const std::string& pbo_path,
    bool on_demand_metadata,
    BuildProgressFunc& progress,
    int pbo_idx, int pbo_total,
    const std::string& source = "") {

    PBOIndexCounts counts;
    std::error_code ec;

    auto fsize = fs::file_size(pbo_path, ec);
    if (ec) fsize = 0;
    auto ftime = fs::last_write_time(pbo_path, ec);
    std::string mod_time;
    if (!ec) {
        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        auto tt = std::chrono::system_clock::to_time_t(sctp);
        char tbuf[64];
        struct tm tm_val;
        if (gmtime_utc(tt, tm_val)) {
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
            mod_time = tbuf;
        }
    }

    pbo::PBO pbo_data;
    try {
        std::ifstream f(pbo_path, std::ios::binary);
        if (!f.is_open()) {
            if (progress) {
                BuildProgress bp;
                bp.phase = "warning";
                bp.pbo_path = pbo_path;
                bp.file_name = "cannot open file";
                bp.pbo_index = pbo_idx;
                bp.pbo_total = pbo_total;
                progress(bp);
            }
            return counts;
        }
        pbo_data = pbo::read(f);
    } catch (const std::exception& e) {
        if (progress) {
            BuildProgress bp;
            bp.phase = "warning";
            bp.pbo_path = pbo_path;
            bp.file_name = std::string("invalid PBO: ") + e.what();
            bp.pbo_index = pbo_idx;
            bp.pbo_total = pbo_total;
            progress(bp);
        }
        return counts;
    } catch (...) {
        if (progress) {
            BuildProgress bp;
            bp.phase = "warning";
            bp.pbo_path = pbo_path;
            bp.file_name = "invalid PBO: unknown error";
            bp.pbo_index = pbo_idx;
            bp.pbo_total = pbo_total;
            progress(bp);
        }
        return counts;
    }

    std::string prefix;
    auto pit = pbo_data.extensions.find("prefix");
    if (pit != pbo_data.extensions.end())
        prefix = pit->second;

    if (prefix.empty()) {
        if (source == "ofp" || source == "arma1" || source == "arma2") {
            std::string stem = std::filesystem::path(pbo_path).stem().string();
            if (!stem.empty())
                prefix = armapath::to_slash_lower(stem);
        }
    }

    int64_t pbo_id = insert_pbo(db, pbo_stmt, pbo_path, prefix,
                                static_cast<int64_t>(fsize), mod_time, source);

    // Insert PBO extensions.
    for (const auto& [key, value] : pbo_data.extensions) {
        ext_stmt.reset();
        ext_stmt.bind_int64(1, pbo_id);
        ext_stmt.bind_text(2, key);
        ext_stmt.bind_text(3, value);
        ext_stmt.exec();
    }

    // Open PBO file once for all metadata indexing within this PBO.
    std::ifstream pbo_file(pbo_path, std::ios::binary);

    for (const auto& entry : pbo_data.entries) {
        // Compute virtual directory path and ensure dirs exist.
        std::string vdir = virtual_dir_path(prefix, entry.filename);
        int64_t dir_id = -1;
        if (!vdir.empty()) {
            dir_id = dir_cache.ensure_dir(vdir);
        }

        // Store raw entry filename in files.path.
        insert_file(file_stmt, pbo_id, dir_id, entry.filename,
                     entry.original_size, entry.data_size, entry.timestamp);
        counts.files++;

        if (on_demand_metadata) continue;
        if (!pbo_file.is_open()) continue;

        std::string lower_path = armapath::to_slash_lower(entry.filename);
        if (ends_with_ci(lower_path, ".p3d")) {
            index_p3d(db, model_stmt, mtex_stmt, pbo_id, entry.filename,
                      pbo_file, entry);
            counts.p3d++;
        } else if (ends_with_ci(lower_path, ".paa") || ends_with_ci(lower_path, ".pac")) {
            index_paa(paa_stmt, pbo_id, entry.filename, pbo_file, entry);
            counts.paa++;
        } else if (ends_with_ci(lower_path, ".ogg")) {
            index_ogg(audio_stmt, pbo_id, entry.filename, pbo_file, entry);
            counts.audio++;
        } else if (ends_with_ci(lower_path, ".wss") || ends_with_ci(lower_path, ".wav")) {
            index_audio(audio_stmt, pbo_id, entry.filename, pbo_file, entry);
            counts.audio++;
        }
    }
    return counts;
}

// ---------------------------------------------------------------------------
// DB::build_db
// ---------------------------------------------------------------------------

BuildResult DB::build_db(const std::string& db_path,
                          const std::string& arma3_dir,
                          const std::string& workshop_dir,
                          const std::vector<std::string>& mod_dirs,
                          const BuildOptions& opts,
                          BuildProgressFunc progress,
                          const GameDirs& game_dirs) {
    BuildResult result;

    // Write to a temp file and rename on success.
    std::string tmp_path = db_path + ".tmp";

    // Remove stale temp if present.
    std::error_code ec;
    fs::remove(tmp_path, ec);

    sqlite3* db = open_db_handle(tmp_path,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

    try {
        exec_sql(db, "PRAGMA journal_mode=WAL");
        exec_sql(db, "PRAGMA synchronous=NORMAL");
        exec_sql(db, schema_sql);

        // Insert metadata.
        exec_sql(db, "BEGIN TRANSACTION");

        {
            SqliteStmt meta_stmt(db,
                "INSERT OR REPLACE INTO meta (key, value) VALUES (?1, ?2)");

            auto insert_meta = [&](const char* key, const std::string& val) {
                meta_stmt.reset();
                meta_stmt.bind_text(1, key);
                meta_stmt.bind_text(2, val);
                meta_stmt.exec();
            };

            insert_meta("schema_version", schema_version);

            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            char tbuf[64];
            struct tm tm_val;
            if (gmtime_utc(tt, tm_val)) {
                std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
                insert_meta("created_at", tbuf);
            } else {
                insert_meta("created_at", "");
            }

            insert_meta("arma3_dir", arma3_dir);
            insert_meta("workshop_dir", workshop_dir);

            std::string mod_dirs_str;
            for (size_t i = 0; i < mod_dirs.size(); ++i) {
                if (i > 0) mod_dirs_str += '\n';
                mod_dirs_str += mod_dirs[i];
            }
            insert_meta("mod_dirs", mod_dirs_str);
            insert_meta("ofp_dir", game_dirs.ofp_dir);
            insert_meta("arma1_dir", game_dirs.arma1_dir);
            insert_meta("arma2_dir", game_dirs.arma2_dir);

            insert_meta("metadata_mode",
                opts.on_demand_metadata ? "ondemand" : "prefill");
        }

        // Discover PBOs.
        if (progress) {
            BuildProgress bp;
            bp.phase = "discovery";
            progress(bp);
        }

        auto pbo_paths = discover_pbo_paths_with_source(arma3_dir, workshop_dir, mod_dirs, game_dirs);
        result.pbo_count = static_cast<int>(pbo_paths.size());

        // Scope for prepared statements — must be destroyed before WAL checkpoint.
        {
            SqliteStmt pbo_stmt(db,
                "INSERT INTO pbos (path, prefix, file_size, mod_time, source) VALUES (?1, ?2, ?3, ?4, ?5)");
            SqliteStmt file_stmt(db,
                "INSERT INTO files (pbo_id, dir_id, path, original_size, data_size, timestamp)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
            SqliteStmt ext_stmt(db,
                "INSERT OR REPLACE INTO pbo_extensions (pbo_id, key, value) VALUES (?1, ?2, ?3)");
            SqliteStmt model_stmt(db,
                "INSERT INTO p3d_models (pbo_id, path, name, format, size_source,"
                " size_x, size_y, size_z,"
                " bbox_min_x, bbox_min_y, bbox_min_z,"
                " bbox_max_x, bbox_max_y, bbox_max_z,"
                " bbox_center_x, bbox_center_y, bbox_center_z, bbox_radius,"
                " mi_max_x, mi_max_y, mi_max_z,"
                " vis_min_x, vis_min_y, vis_min_z,"
                " vis_max_x, vis_max_y, vis_max_z,"
                " vis_center_x, vis_center_y, vis_center_z)"
                " VALUES (?1, ?2, ?3, ?4, ?5,"
                " ?6, ?7, ?8,"
                " ?9, ?10, ?11, ?12, ?13, ?14,"
                " ?15, ?16, ?17, ?18,"
                " ?19, ?20, ?21,"
                " ?22, ?23, ?24, ?25, ?26, ?27,"
                " ?28, ?29, ?30)");
            SqliteStmt mtex_stmt(db,
                "INSERT INTO model_textures (pbo_id, model_path, texture_path, source)"
                " VALUES (?1, ?2, ?3, ?4)");
            SqliteStmt paa_stmt(db,
                "INSERT INTO textures (pbo_id, path, name, format, data_size, width, height)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
            SqliteStmt audio_stmt(db,
                "INSERT INTO audio_files (pbo_id, path, name, format, encoder,"
                " sample_rate, channels, data_size)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

            DirPathCache dir_cache(db);

            int pbo_total = static_cast<int>(pbo_paths.size());
            for (size_t i = 0; i < static_cast<size_t>(pbo_total); ++i) {
                if (progress) {
                    BuildProgress bp;
                    bp.phase = "pbo";
                    bp.pbo_index = static_cast<int>(i) + 1;
                    bp.pbo_total = pbo_total;
                    bp.pbo_path = pbo_paths[i].path;
                    progress(bp);
                }

                auto c = index_single_pbo(db, pbo_stmt, file_stmt, ext_stmt,
                                           model_stmt, mtex_stmt, paa_stmt, audio_stmt,
                                           dir_cache,
                                           pbo_paths[i].path, opts.on_demand_metadata,
                                           progress, static_cast<int>(i) + 1, pbo_total,
                                           pbo_paths[i].source);
                result.file_count += c.files;
                result.p3d_count += c.p3d;
                result.paa_count += c.paa;
                result.audio_count += c.audio;
            }

            if (progress) {
                BuildProgress bp;
                bp.phase = "commit";
                progress(bp);
            }

            exec_sql(db, "COMMIT");
        } // All prepared statements destroyed here, releasing table locks.

        // Checkpoint WAL so all data is in the main DB file before rename.
        // Without this, the .tmp-wal sidecar file won't be renamed and
        // the database becomes unreadable after the rename.
        exec_sql(db, "PRAGMA wal_checkpoint(TRUNCATE)");

        sqlite3_close(db);
        db = nullptr;

        // Rename temp to final.
        fs::rename(tmp_path, db_path);

        // Clean up any leftover WAL/SHM sidecar files from the temp path.
        fs::remove(tmp_path + "-wal", ec);
        fs::remove(tmp_path + "-shm", ec);

    } catch (...) {
        if (db) sqlite3_close(db);
        fs::remove(tmp_path, ec);
        fs::remove(tmp_path + "-wal", ec);
        fs::remove(tmp_path + "-shm", ec);
        throw;
    }

    return result;
}

// ---------------------------------------------------------------------------
// DB::open
// ---------------------------------------------------------------------------

// Check that a table has an expected column. Returns true if the column exists.
static bool table_has_column(sqlite3* db, const char* table, const char* column) {
    std::string sql = std::format("PRAGMA table_info({})", table);
    SqliteStmt stmt(db, sql.c_str());
    while (stmt.step() == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 1));
        if (name && std::strcmp(name, column) == 0)
            return true;
    }
    return false;
}

// Check that a table exists. Returns true if it does.
static bool table_exists(sqlite3* db, const char* table) {
    SqliteStmt stmt(db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1");
    stmt.bind_text(1, table);
    return stmt.step() == SQLITE_ROW;
}

DB DB::open(const std::string& path) {
    DB d;
    d.impl_->db = open_db_handle(path, SQLITE_OPEN_READONLY);

    // Verify meta table exists.
    if (!table_exists(d.impl_->db, "meta"))
        throw std::runtime_error("pboindex: not a valid database (no meta table)");

    // Verify schema version.
    {
        SqliteStmt stmt(d.impl_->db,
            "SELECT value FROM meta WHERE key = 'schema_version'");
        int rc = stmt.step();
        if (rc != SQLITE_ROW)
            throw std::runtime_error("pboindex: database missing schema_version");

        const char* ver = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 0));
        if (!ver || std::strcmp(ver, schema_version) != 0)
            throw std::runtime_error(
                std::format("pboindex: schema version mismatch: expected {}, got {}",
                            schema_version, ver ? ver : "(null)"));
    }

    auto* db_handle = d.impl_->db;

    // Check required tables exist.
    const char* required_tables[] = {
        "pbos", "files", "p3d_models", "textures", "audio_files"
    };
    for (const char* tbl : required_tables) {
        if (!table_exists(db_handle, tbl))
            throw std::runtime_error(
                std::format("pboindex: missing required table '{}'", tbl));
    }

    // Verify Go-compatible schema: files.path column must exist.
    if (!table_has_column(db_handle, "files", "path"))
        throw std::runtime_error(
            "pboindex: incompatible database schema — 'files' table missing "
            "'path' column. Please rebuild the database.");

    // Verify p3d_models.pbo_id column (Go schema).
    if (!table_has_column(db_handle, "p3d_models", "pbo_id"))
        throw std::runtime_error(
            "pboindex: incompatible database schema — 'p3d_models' table missing "
            "'pbo_id' column. Please rebuild the database.");

    return d;
}

// ---------------------------------------------------------------------------
// DB::index
// ---------------------------------------------------------------------------

Index DB::index() const {
    SqliteStmt stmt(impl_->db, "SELECT path, prefix FROM pbos");
    std::vector<PBORef> refs;
    while (stmt.step() == SQLITE_ROW) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        const char* pfx = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        std::string pbo_path = p ? p : "";
        std::string prefix = pfx ? pfx : "";

        // For old PBOs (OFP, Arma 1) with no prefix header, the PBO filename
        // stem acts as the virtual directory.  E.g. Data3D.pbo -> "data3d".
        if (prefix.empty() && !pbo_path.empty()) {
            auto stem = std::filesystem::path(pbo_path).stem().string();
            if (!stem.empty())
                prefix = armapath::to_slash_lower(stem);
        }

        refs.push_back(PBORef{
            .path = std::move(pbo_path),
            .prefix = std::move(prefix),
        });
    }
    return Index(std::move(refs));
}

// ---------------------------------------------------------------------------
// DB::stats
// ---------------------------------------------------------------------------

DBStats DB::stats() const {
    DBStats s;

    auto get_meta = [&](const char* key) -> std::string {
        SqliteStmt stmt(impl_->db,
            "SELECT value FROM meta WHERE key = ?1");
        stmt.bind_text(1, key);
        if (stmt.step() == SQLITE_ROW) {
            const char* v = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 0));
            return v ? v : "";
        }
        return "";
    };

    s.schema_version = get_meta("schema_version");
    s.created_at = get_meta("created_at");
    s.arma3_dir = get_meta("arma3_dir");
    s.workshop_dir = get_meta("workshop_dir");
    s.ofp_dir = get_meta("ofp_dir");
    s.arma1_dir = get_meta("arma1_dir");
    s.arma2_dir = get_meta("arma2_dir");

    std::string mod_dirs_str = get_meta("mod_dirs");
    if (!mod_dirs_str.empty()) {
        std::istringstream iss(mod_dirs_str);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty())
                s.mod_dirs.push_back(line);
        }
    }

    auto count_query = [&](const char* sql) -> int {
        SqliteStmt stmt(impl_->db, sql);
        if (stmt.step() == SQLITE_ROW)
            return sqlite3_column_int(stmt.get(), 0);
        return 0;
    };

    s.pbo_count = count_query("SELECT COUNT(*) FROM pbos");
    s.pbos_with_prefix = count_query(
        "SELECT COUNT(*) FROM pbos WHERE prefix != ''");
    s.file_count = count_query("SELECT COUNT(*) FROM files");

    {
        SqliteStmt stmt(impl_->db,
            "SELECT COALESCE(SUM(data_size), 0) FROM files");
        if (stmt.step() == SQLITE_ROW)
            s.total_data_size = sqlite3_column_int64(stmt.get(), 0);
    }

    s.p3d_model_count = count_query("SELECT COUNT(*) FROM p3d_models");
    s.texture_count = count_query("SELECT COUNT(*) FROM textures");
    s.audio_file_count = count_query("SELECT COUNT(*) FROM audio_files");

    return s;
}

// ---------------------------------------------------------------------------
// DB::list_dir — uses dirs table when available, otherwise fallback
// ---------------------------------------------------------------------------

std::vector<DirEntry> DB::list_dir(const std::string& dir,
                                   size_t limit,
                                   size_t offset) const {
    std::vector<DirEntry> entries;
    bool has_dirs = table_exists(impl_->db, "dirs");
    bool paged_in_sql = has_dirs;
    const int64_t sql_limit = (limit > 0) ? static_cast<int64_t>(limit) : -1;
    const int64_t sql_offset = static_cast<int64_t>(offset);

    if (has_dirs) {
        if (dir.empty()) {
            SqliteStmt stmt(impl_->db,
                "SELECT kind, name, pbo_path, prefix, file_path, data_size FROM ("
                "  SELECT 0 AS kind, d.name AS name,"
                "         '' AS pbo_path, '' AS prefix, '' AS file_path, 0 AS data_size"
                "  FROM dirs d WHERE d.parent_id IS NULL"
                "  UNION ALL"
                "  SELECT 1 AS kind, f.path AS name,"
                "         p.path AS pbo_path, p.prefix AS prefix,"
                "         f.path AS file_path, f.data_size AS data_size"
                "  FROM files f JOIN pbos p ON f.pbo_id = p.id"
                "  WHERE f.dir_id IS NULL"
                ") ORDER BY kind, name"
                " LIMIT ?1 OFFSET ?2");
            stmt.bind_int64(1, sql_limit);
            stmt.bind_int64(2, sql_offset);
            while (stmt.step() == SQLITE_ROW) {
                int kind = sqlite3_column_int(stmt.get(), 0);
                const char* name = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt.get(), 1));
                if (!name) continue;
                if (kind == 0) {
                    entries.push_back(DirEntry{.name = name, .is_dir = true, .files = {}});
                    continue;
                }
                const char* pp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
                const char* pfx = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
                const char* fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
                int ds = sqlite3_column_int(stmt.get(), 5);
                if (!fp) continue;
                FindResult fr;
                fr.pbo_path = pp ? pp : "";
                fr.prefix = pfx ? pfx : "";
                fr.file_path = fp;
                fr.data_size = static_cast<uint32_t>(ds);
                entries.push_back(DirEntry{
                    .name = file_basename_lower(fp),
                    .is_dir = false,
                    .files = {fr},
                });
            }
        } else {
            SqliteStmt stmt(impl_->db,
                "SELECT kind, name, pbo_path, prefix, file_path, data_size FROM ("
                "  SELECT 0 AS kind, d.name AS name,"
                "         '' AS pbo_path, '' AS prefix, '' AS file_path, 0 AS data_size"
                "  FROM dirs d JOIN dirs p ON d.parent_id = p.id"
                "  WHERE p.path = ?1"
                "  UNION ALL"
                "  SELECT 1 AS kind, f.path AS name,"
                "         p.path AS pbo_path, p.prefix AS prefix,"
                "         f.path AS file_path, f.data_size AS data_size"
                "  FROM files f JOIN pbos p ON f.pbo_id = p.id"
                "  JOIN dirs d ON f.dir_id = d.id"
                "  WHERE d.path = ?1"
                ") ORDER BY kind, name"
                " LIMIT ?2 OFFSET ?3");
            stmt.bind_text(1, dir);
            stmt.bind_int64(2, sql_limit);
            stmt.bind_int64(3, sql_offset);
            while (stmt.step() == SQLITE_ROW) {
                int kind = sqlite3_column_int(stmt.get(), 0);
                const char* name = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt.get(), 1));
                if (!name) continue;
                if (kind == 0) {
                    entries.push_back(DirEntry{.name = name, .is_dir = true, .files = {}});
                    continue;
                }
                const char* pp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
                const char* pfx = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
                const char* fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
                int ds = sqlite3_column_int(stmt.get(), 5);
                if (!fp) continue;
                FindResult fr;
                fr.pbo_path = pp ? pp : "";
                fr.prefix = pfx ? pfx : "";
                fr.file_path = fp;
                fr.data_size = static_cast<uint32_t>(ds);
                entries.push_back(DirEntry{
                    .name = file_basename_lower(fp),
                    .is_dir = false,
                    .files = {fr},
                });
            }
        }
    } else {
        // Fallback: no dirs table — build virtual paths from prefix + entry path.
        std::string norm_dir = dir;
        if (!norm_dir.empty() && norm_dir.back() != '/')
            norm_dir += '/';

        SqliteStmt stmt(impl_->db,
            "SELECT p.path, p.prefix, f.path, f.data_size"
            " FROM files f JOIN pbos p ON f.pbo_id = p.id");

        std::unordered_set<std::string> seen_dirs;
        while (stmt.step() == SQLITE_ROW) {
            const char* pp = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 0));
            const char* pfx = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 1));
            const char* fp = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 2));
            int ds = sqlite3_column_int(stmt.get(), 3);
            if (!fp) continue;

            // Construct virtual path: normalize(prefix)/normalize(entry)
            std::string prefix_str = pfx ? pfx : "";
            std::string vpath;
            if (!prefix_str.empty()) {
                vpath = armapath::to_slash_lower(prefix_str);
                while (!vpath.empty() && vpath.back() == '/') vpath.pop_back();
                vpath += "/";
            }
            vpath += armapath::to_slash_lower(std::string(fp));

            if (norm_dir.empty()) {
                // Root level
                auto slash = vpath.find('/');
                if (slash != std::string::npos) {
                    std::string dirname = vpath.substr(0, slash);
                    if (seen_dirs.insert(dirname).second)
                        entries.push_back(DirEntry{.name = dirname, .is_dir = true, .files = {}});
                } else {
                    FindResult fr;
                    fr.pbo_path = pp ? pp : "";
                    fr.prefix = pfx ? pfx : "";
                    fr.file_path = fp;
                    fr.data_size = static_cast<uint32_t>(ds);
                    entries.push_back(DirEntry{
                        .name = vpath,
                        .is_dir = false,
                        .files = {fr},
                    });
                }
            } else if (vpath.size() > norm_dir.size() &&
                       vpath.substr(0, norm_dir.size()) == norm_dir) {
                std::string rest = vpath.substr(norm_dir.size());
                auto slash = rest.find('/');
                if (slash != std::string::npos) {
                    std::string dirname = rest.substr(0, slash);
                    if (seen_dirs.insert(dirname).second)
                        entries.push_back(DirEntry{.name = dirname, .is_dir = true, .files = {}});
                } else {
                    FindResult fr;
                    fr.pbo_path = pp ? pp : "";
                    fr.prefix = pfx ? pfx : "";
                    fr.file_path = fp;
                    fr.data_size = static_cast<uint32_t>(ds);
                    entries.push_back(DirEntry{
                        .name = rest,
                        .is_dir = false,
                        .files = {fr},
                    });
                }
            }
        }
    }

    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir; // dirs first
        return a.name < b.name;
    });
    if (!paged_in_sql) {
        if (offset < entries.size()) {
            if (limit > 0 && (offset + limit) < entries.size()) {
                entries = std::vector<DirEntry>(entries.begin() + static_cast<std::ptrdiff_t>(offset),
                                                entries.begin() + static_cast<std::ptrdiff_t>(offset + limit));
            } else if (offset > 0) {
                entries = std::vector<DirEntry>(entries.begin() + static_cast<std::ptrdiff_t>(offset), entries.end());
            }
        } else if (limit > 0 || offset > 0) {
            entries.clear();
        }
    }

    return entries;
}

// ---------------------------------------------------------------------------
// DB::all_files
// ---------------------------------------------------------------------------

std::vector<FindResult> DB::all_files() const {
    SqliteStmt stmt(impl_->db,
        "SELECT p.path, p.prefix, f.path, f.data_size"
        " FROM files f JOIN pbos p ON f.pbo_id = p.id"
        " ORDER BY f.path");

    std::vector<FindResult> results;
    while (stmt.step() == SQLITE_ROW) {
        FindResult fr;
        const char* v;
        v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        fr.pbo_path = v ? v : "";
        v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        fr.prefix = v ? v : "";
        v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
        fr.file_path = v ? v : "";
        fr.data_size = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 3));
        results.push_back(std::move(fr));
    }
    return results;
}

// ---------------------------------------------------------------------------
// DB::find_files
// ---------------------------------------------------------------------------

std::vector<FindResult> DB::find_files(const std::string& pattern,
                                       const std::string& source,
                                       size_t limit,
                                       size_t offset) const {
    // Convert glob pattern: * -> %, ? -> _
    std::string like_pattern = armapath::to_slash_lower(pattern);
    for (auto& c : like_pattern) {
        if (c == '*') c = '%';
        else if (c == '?') c = '_';
    }

    std::vector<FindResult> results;

    const int64_t sql_limit = (limit > 0) ? static_cast<int64_t>(limit) : -1;
    const int64_t sql_offset = static_cast<int64_t>(offset);

    if (source.empty() || !table_has_column(impl_->db, "pbos", "source")) {
        SqliteStmt stmt(impl_->db,
            "SELECT p.path, p.prefix, f.path, f.data_size"
            " FROM files f JOIN pbos p ON f.pbo_id = p.id"
            " WHERE LOWER(REPLACE(f.path, '\\', '/')) LIKE ?1"
            " ORDER BY f.path"
            " LIMIT ?2 OFFSET ?3");
        stmt.bind_text(1, like_pattern);
        stmt.bind_int64(2, sql_limit);
        stmt.bind_int64(3, sql_offset);

        while (stmt.step() == SQLITE_ROW) {
            FindResult fr;
            const char* v;
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
            fr.pbo_path = v ? v : "";
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
            fr.prefix = v ? v : "";
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
            fr.file_path = v ? v : "";
            fr.data_size = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 3));
            results.push_back(std::move(fr));
        }
    } else {
        SqliteStmt stmt(impl_->db,
            "SELECT p.path, p.prefix, f.path, f.data_size"
            " FROM files f JOIN pbos p ON f.pbo_id = p.id"
            " WHERE LOWER(REPLACE(f.path, '\\', '/')) LIKE ?1"
            "   AND p.source = ?2"
            " ORDER BY f.path"
            " LIMIT ?3 OFFSET ?4");
        stmt.bind_text(1, like_pattern);
        stmt.bind_text(2, source);
        stmt.bind_int64(3, sql_limit);
        stmt.bind_int64(4, sql_offset);

        while (stmt.step() == SQLITE_ROW) {
            FindResult fr;
            const char* v;
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
            fr.pbo_path = v ? v : "";
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
            fr.prefix = v ? v : "";
            v = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
            fr.file_path = v ? v : "";
            fr.data_size = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 3));
            results.push_back(std::move(fr));
        }
    }
    return results;
}

// ---------------------------------------------------------------------------
// DB::list_pbo_paths
// ---------------------------------------------------------------------------

std::vector<std::string> DB::list_pbo_paths() const {
    SqliteStmt stmt(impl_->db, "SELECT path FROM pbos ORDER BY path");
    std::vector<std::string> paths;
    while (stmt.step() == SQLITE_ROW) {
        const char* v = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 0));
        if (v) paths.emplace_back(v);
    }
    return paths;
}

// ---------------------------------------------------------------------------
// DB::query_model_bboxes
// ---------------------------------------------------------------------------

std::unordered_map<std::string, ModelBBox> DB::query_model_bboxes() const {
    // Check if visual columns exist (for compatibility with older databases).
    bool has_vis = table_has_column(impl_->db, "p3d_models", "vis_min_x");

    std::string sql =
        "SELECT m.path, p.prefix,"
        " m.bbox_min_x, m.bbox_min_y, m.bbox_min_z,"
        " m.bbox_max_x, m.bbox_max_y, m.bbox_max_z,"
        " m.bbox_center_x, m.bbox_center_y, m.bbox_center_z,"
        " m.bbox_radius,"
        " m.mi_max_x, m.mi_max_y, m.mi_max_z";
    if (has_vis) {
        sql += ","
            " m.vis_min_x, m.vis_min_y, m.vis_min_z,"
            " m.vis_max_x, m.vis_max_y, m.vis_max_z,"
            " m.vis_center_x, m.vis_center_y, m.vis_center_z";
    }
    sql += " FROM p3d_models m JOIN pbos p ON m.pbo_id = p.id";

    SqliteStmt stmt(impl_->db, sql.c_str());

    std::unordered_map<std::string, ModelBBox> result;
    while (stmt.step() == SQLITE_ROW) {
        const char* fp = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 0));
        const char* pfx = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 1));
        if (!fp) continue;

        // Build the full model path: normalizePrefix/toSlashLower(path)
        std::string prefix = pfx ? pfx : "";
        std::string full_path;
        if (!prefix.empty()) {
            full_path = armapath::to_slash_lower(prefix);
            if (!full_path.empty() && full_path.back() != '/')
                full_path += '/';
        }
        full_path += armapath::to_slash_lower(std::string(fp));

        ModelBBox bbox;
        int col = 2;
        bbox.bbox_min[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_min[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_min[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_max[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_max[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_max[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_center[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_center[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_center[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.bbox_radius = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.mi_max[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.mi_max[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        bbox.mi_max[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));

        if (has_vis) {
            bbox.vis_min[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_min[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_min[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_max[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_max[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_max[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_center[0] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_center[1] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
            bbox.vis_center[2] = static_cast<float>(sqlite3_column_double(stmt.get(), col++));
        }

        result[full_path] = bbox;
    }
    return result;
}

// ---------------------------------------------------------------------------
// DB::query_model_textures
// ---------------------------------------------------------------------------

std::unordered_map<std::string, std::vector<std::string>>
DB::query_model_textures(const std::vector<std::string>& models) const {
    std::unordered_map<std::string, std::vector<std::string>> result;

    if (models.empty()) return result;

    // Check if model_textures table exists (Go schema).
    if (!table_exists(impl_->db, "model_textures")) return result;

    // Query texture paths for each model by constructing the full model path.
    SqliteStmt stmt(impl_->db,
        "SELECT mt.texture_path"
        " FROM model_textures mt"
        " JOIN pbos p ON mt.pbo_id = p.id"
        " WHERE LOWER(REPLACE("
        "   CASE WHEN p.prefix != '' THEN"
        "     REPLACE(p.prefix, '\\', '/') || '/' || REPLACE(mt.model_path, '\\', '/')"
        "   ELSE REPLACE(mt.model_path, '\\', '/')"
        "   END, '\\', '/')) = ?1");

    for (const auto& model : models) {
        std::string norm = armapath::to_slash_lower(model);
        stmt.reset();
        stmt.bind_text(1, norm);

        std::vector<std::string> textures;
        while (stmt.step() == SQLITE_ROW) {
            const char* v = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 0));
            if (v) textures.emplace_back(v);
        }
        if (!textures.empty())
            result[norm] = std::move(textures);
    }
    return result;
}

// ---------------------------------------------------------------------------
// DB::query_model_paths
// ---------------------------------------------------------------------------

std::unordered_map<std::string, std::string> DB::query_model_paths() const {
    // p3d_models.path stores the raw entry filename (original case).
    // p3d_models.name stores the basename without extension (original case).
    // We join with pbos to get the prefix and build the full virtual path.
    SqliteStmt stmt(impl_->db,
        "SELECT m.path, m.name, p.prefix"
        " FROM p3d_models m JOIN pbos p ON m.pbo_id = p.id");

    std::unordered_map<std::string, std::string> result;
    while (stmt.step() == SQLITE_ROW) {
        const char* fp = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 0));
        const char* name = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 1));
        const char* pfx = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 2));
        if (!fp || !name) continue;

        // Build the full lowercase virtual path: prefix/entry_path
        std::string prefix = pfx ? pfx : "";
        std::string full_path;
        if (!prefix.empty()) {
            full_path = armapath::to_slash_lower(prefix);
            if (!full_path.empty() && full_path.back() != '/')
                full_path += '/';
        }
        full_path += armapath::to_slash_lower(std::string(fp));

        result[full_path] = name;
    }
    return result;
}

// ---------------------------------------------------------------------------
// DB::update_db
// ---------------------------------------------------------------------------

UpdateResult DB::update_db(const std::string& db_path,
                            const std::string& arma3_dir,
                            const std::string& workshop_dir,
                            const std::vector<std::string>& mod_dirs,
                            const BuildOptions& opts,
                            BuildProgressFunc progress,
                            const GameDirs& game_dirs) {
    UpdateResult result;

    sqlite3* db = open_db_handle(db_path, SQLITE_OPEN_READWRITE);

    try {
        exec_sql(db, "PRAGMA journal_mode=WAL");
        exec_sql(db, "PRAGMA synchronous=NORMAL");

        // Verify schema version.
        {
            SqliteStmt stmt(db,
                "SELECT value FROM meta WHERE key = 'schema_version'");
            int rc = stmt.step();
            if (rc != SQLITE_ROW)
                throw std::runtime_error("pboindex: database missing schema_version");
            const char* ver = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 0));
            if (!ver || std::strcmp(ver, schema_version) != 0)
                throw std::runtime_error(
                    std::format("pboindex: schema version mismatch: expected {}, got {}",
                                schema_version, ver ? ver : "(null)"));
        }

        // Verify Go-compatible schema structure.
        if (!table_has_column(db, "files", "path"))
            throw std::runtime_error(
                "pboindex: incompatible database schema — cannot update this database. "
                "Please rebuild with Build DB.");

        // Discover current PBO paths.
        if (progress) {
            BuildProgress bp;
            bp.phase = "discovery";
            progress(bp);
        }

        auto pbo_paths = discover_pbo_paths_with_source(arma3_dir, workshop_dir, mod_dirs, game_dirs);

        // Build a set of current PBO paths.
        std::unordered_map<std::string, bool> current_paths;
        for (const auto& p : pbo_paths)
            current_paths[p.path] = true;

        exec_sql(db, "BEGIN TRANSACTION");

        // Load existing PBOs from database.
        struct ExistingPBO {
            int64_t id;
            std::string path;
            int64_t file_size;
            std::string mod_time;
        };
        std::vector<ExistingPBO> existing;
        {
            SqliteStmt stmt(db,
                "SELECT id, path, file_size, mod_time FROM pbos");
            while (stmt.step() == SQLITE_ROW) {
                ExistingPBO ep;
                ep.id = sqlite3_column_int64(stmt.get(), 0);
                const char* v = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt.get(), 1));
                ep.path = v ? v : "";
                ep.file_size = sqlite3_column_int64(stmt.get(), 2);
                v = reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt.get(), 3));
                ep.mod_time = v ? v : "";
                existing.push_back(std::move(ep));
            }
        }

        // Build existing map.
        std::unordered_map<std::string, ExistingPBO> existing_map;
        for (auto& ep : existing)
            existing_map[ep.path] = ep;

        // Helper to explicitly delete all child rows for a PBO (no CASCADE).
        auto delete_pbo_children = [&](int64_t pbo_id) {
            const char* del_sqls[] = {
                "DELETE FROM files WHERE pbo_id = ?1",
                "DELETE FROM p3d_models WHERE pbo_id = ?1",
                "DELETE FROM textures WHERE pbo_id = ?1",
                "DELETE FROM audio_files WHERE pbo_id = ?1",
                "DELETE FROM model_textures WHERE pbo_id = ?1",
                "DELETE FROM pbo_extensions WHERE pbo_id = ?1",
            };
            for (const char* sql : del_sqls) {
                if (std::strstr(sql, "model_textures") && !table_exists(db, "model_textures"))
                    continue;
                if (std::strstr(sql, "pbo_extensions") && !table_exists(db, "pbo_extensions"))
                    continue;
                SqliteStmt del(db, sql);
                del.bind_int64(1, pbo_id);
                del.exec();
            }
        };

        // Remove PBOs that no longer exist on disk.
        {
            SqliteStmt del_pbo_stmt(db, "DELETE FROM pbos WHERE id = ?1");
            for (const auto& ep : existing) {
                if (current_paths.find(ep.path) == current_paths.end()) {
                    delete_pbo_children(ep.id);
                    del_pbo_stmt.reset();
                    del_pbo_stmt.bind_int64(1, ep.id);
                    del_pbo_stmt.exec();
                    result.removed++;
                }
            }
        }

        // Prepare insert/index statements.
        SqliteStmt pbo_stmt(db,
            "INSERT INTO pbos (path, prefix, file_size, mod_time, source) VALUES (?1, ?2, ?3, ?4, ?5)");
        SqliteStmt file_stmt(db,
            "INSERT INTO files (pbo_id, dir_id, path, original_size, data_size, timestamp)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        SqliteStmt ext_stmt(db,
            "INSERT OR REPLACE INTO pbo_extensions (pbo_id, key, value) VALUES (?1, ?2, ?3)");
        SqliteStmt model_stmt(db,
            "INSERT INTO p3d_models (pbo_id, path, name, format, size_source,"
            " size_x, size_y, size_z,"
            " bbox_min_x, bbox_min_y, bbox_min_z,"
            " bbox_max_x, bbox_max_y, bbox_max_z,"
            " bbox_center_x, bbox_center_y, bbox_center_z, bbox_radius,"
            " mi_max_x, mi_max_y, mi_max_z,"
            " vis_min_x, vis_min_y, vis_min_z,"
            " vis_max_x, vis_max_y, vis_max_z,"
            " vis_center_x, vis_center_y, vis_center_z)"
            " VALUES (?1, ?2, ?3, ?4, ?5,"
            " ?6, ?7, ?8,"
            " ?9, ?10, ?11, ?12, ?13, ?14,"
            " ?15, ?16, ?17, ?18,"
            " ?19, ?20, ?21,"
            " ?22, ?23, ?24, ?25, ?26, ?27,"
            " ?28, ?29, ?30)");
        SqliteStmt mtex_stmt(db,
            "INSERT INTO model_textures (pbo_id, model_path, texture_path, source)"
            " VALUES (?1, ?2, ?3, ?4)");
        SqliteStmt paa_stmt(db,
            "INSERT INTO textures (pbo_id, path, name, format, data_size, width, height)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
        SqliteStmt audio_stmt(db,
            "INSERT INTO audio_files (pbo_id, path, name, format, encoder,"
            " sample_rate, channels, data_size)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

        SqliteStmt del_pbo_stmt(db, "DELETE FROM pbos WHERE id = ?1");

        DirPathCache dir_cache(db);

        int pbo_total = static_cast<int>(pbo_paths.size());
        for (size_t i = 0; i < static_cast<size_t>(pbo_total); ++i) {
            const auto& pbo_entry = pbo_paths[i];
            const auto& pbo_path = pbo_entry.path;
            std::error_code ec;

            auto fsize = fs::file_size(pbo_path, ec);
            if (ec) fsize = 0;
            auto ftime = fs::last_write_time(pbo_path, ec);
            std::string mod_time;
            if (!ec) {
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                auto tt = std::chrono::system_clock::to_time_t(sctp);
                char tbuf[64];
                struct tm tm_val;
                if (gmtime_utc(tt, tm_val)) {
                    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
                    mod_time = tbuf;
                }
            }

            auto eit = existing_map.find(pbo_path);
            if (eit != existing_map.end()) {
                // Check if file changed.
                if (eit->second.file_size == static_cast<int64_t>(fsize) &&
                    eit->second.mod_time == mod_time) {
                    // Unchanged, skip.
                    continue;
                }
                // Changed: remove old entry and all children, re-index.
                delete_pbo_children(eit->second.id);
                del_pbo_stmt.reset();
                del_pbo_stmt.bind_int64(1, eit->second.id);
                del_pbo_stmt.exec();
                result.updated++;
            } else {
                result.added++;
            }

            if (progress) {
                BuildProgress bp;
                bp.phase = "pbo";
                bp.pbo_index = static_cast<int>(i) + 1;
                bp.pbo_total = pbo_total;
                bp.pbo_path = pbo_path;
                progress(bp);
            }

            auto c = index_single_pbo(db, pbo_stmt, file_stmt, ext_stmt,
                                       model_stmt, mtex_stmt, paa_stmt, audio_stmt,
                                       dir_cache,
                                       pbo_path, opts.on_demand_metadata,
                                       progress, static_cast<int>(i) + 1, pbo_total,
                                       pbo_entry.source);
            result.file_count += c.files;
            result.p3d_count += c.p3d;
            result.paa_count += c.paa;
            result.audio_count += c.audio;
        }

        if (progress) {
            BuildProgress bp;
            bp.phase = "commit";
            progress(bp);
        }

        exec_sql(db, "COMMIT");
        sqlite3_close(db);
        db = nullptr;

    } catch (...) {
        if (db) {
            exec_sql(db, "ROLLBACK");
            sqlite3_close(db);
        }
        throw;
    }

    return result;
}

// ---------------------------------------------------------------------------
// DB::query_sources
// ---------------------------------------------------------------------------

std::vector<std::string> DB::query_sources() const {
    std::vector<std::string> sources;

    if (!table_has_column(impl_->db, "pbos", "source"))
        return sources;

    // Collect distinct sources from DB.
    std::unordered_set<std::string> found;
    SqliteStmt stmt(impl_->db,
        "SELECT DISTINCT source FROM pbos WHERE source != ''");
    while (stmt.step() == SQLITE_ROW) {
        const char* v = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt.get(), 0));
        if (v) found.insert(v);
    }

    // Return in chronological order (OFP 2001, Arma 1 2006, Arma 2 2009, Arma 3 2013, then workshop/custom).
    static const std::vector<std::string> order = {
        "ofp", "arma1", "arma2", "arma3", "workshop", "custom"
    };
    for (const auto& src : order) {
        if (found.count(src))
            sources.push_back(src);
    }
    // Any unknown sources at the end.
    for (const auto& src : found) {
        if (std::find(order.begin(), order.end(), src) == order.end())
            sources.push_back(src);
    }

    return sources;
}

// ---------------------------------------------------------------------------
// DB::list_dir_for_source
// ---------------------------------------------------------------------------

std::vector<DirEntry> DB::list_dir_for_source(const std::string& dir,
                                              const std::string& source,
                                              size_t limit,
                                              size_t offset) const {
    std::vector<DirEntry> entries;
    bool has_dirs = table_exists(impl_->db, "dirs");
    bool has_source_col = table_has_column(impl_->db, "pbos", "source");
    const int64_t sql_limit = (limit > 0) ? static_cast<int64_t>(limit) : -1;
    const int64_t sql_offset = static_cast<int64_t>(offset);

    if (!has_source_col) return list_dir(dir, limit, offset);

    if (has_dirs) {
        if (dir.empty()) {
            SqliteStmt stmt(impl_->db,
                "SELECT kind, name, pbo_path, prefix, file_path, data_size FROM ("
                "  SELECT 0 AS kind, root_name AS name,"
                "         '' AS pbo_path, '' AS prefix, '' AS file_path, 0 AS data_size"
                "  FROM ("
                "    SELECT DISTINCT"
                "      CASE WHEN INSTR(d.path, '/') > 0"
                "        THEN SUBSTR(d.path, 1, INSTR(d.path, '/') - 1)"
                "        ELSE d.path END AS root_name"
                "    FROM files f"
                "    JOIN pbos p ON f.pbo_id = p.id"
                "    JOIN dirs d ON f.dir_id = d.id"
                "    WHERE p.source = ?1"
                "  )"
                "  UNION ALL"
                "  SELECT 1 AS kind, f.path AS name,"
                "         p.path AS pbo_path, p.prefix AS prefix,"
                "         f.path AS file_path, f.data_size AS data_size"
                "  FROM files f JOIN pbos p ON f.pbo_id = p.id"
                "  WHERE f.dir_id IS NULL AND p.source = ?1"
                ") ORDER BY kind, name"
                " LIMIT ?2 OFFSET ?3");
            stmt.bind_text(1, source);
            stmt.bind_int64(2, sql_limit);
            stmt.bind_int64(3, sql_offset);
            while (stmt.step() == SQLITE_ROW) {
                int kind = sqlite3_column_int(stmt.get(), 0);
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
                if (!name) continue;
                if (kind == 0) {
                    entries.push_back(DirEntry{.name = name, .is_dir = true, .files = {}});
                    continue;
                }
                const char* pp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
                const char* pfx = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
                const char* fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
                int ds = sqlite3_column_int(stmt.get(), 5);
                if (!fp) continue;
                FindResult fr;
                fr.pbo_path = pp ? pp : "";
                fr.prefix = pfx ? pfx : "";
                fr.file_path = fp;
                fr.data_size = static_cast<uint32_t>(ds);
                entries.push_back(DirEntry{
                    .name = file_basename_lower(fp),
                    .is_dir = false,
                    .files = {fr},
                });
            }
        } else {
            auto prefix = dir + "/";
            SqliteStmt stmt(impl_->db,
                "SELECT kind, name, pbo_path, prefix, file_path, data_size FROM ("
                "  SELECT 0 AS kind, child_name AS name,"
                "         '' AS pbo_path, '' AS prefix, '' AS file_path, 0 AS data_size"
                "  FROM ("
                "    SELECT DISTINCT"
                "      CASE WHEN INSTR(SUBSTR(d.path, LENGTH(?1) + 2), '/') > 0"
                "        THEN SUBSTR(d.path, LENGTH(?1) + 2,"
                "             INSTR(SUBSTR(d.path, LENGTH(?1) + 2), '/') - 1)"
                "        ELSE SUBSTR(d.path, LENGTH(?1) + 2) END AS child_name"
                "    FROM files f"
                "    JOIN pbos p ON f.pbo_id = p.id"
                "    JOIN dirs d ON f.dir_id = d.id"
                "    WHERE p.source = ?2"
                "      AND d.path LIKE ?3"
                "  ) WHERE child_name != ''"
                "  UNION ALL"
                "  SELECT 1 AS kind, f.path AS name,"
                "         p.path AS pbo_path, p.prefix AS prefix,"
                "         f.path AS file_path, f.data_size AS data_size"
                "  FROM files f JOIN pbos p ON f.pbo_id = p.id"
                "  JOIN dirs d ON f.dir_id = d.id"
                "  WHERE d.path = ?1 AND p.source = ?2"
                ") ORDER BY kind, name"
                " LIMIT ?4 OFFSET ?5");
            stmt.bind_text(1, dir);
            stmt.bind_text(2, source);
            stmt.bind_text(3, prefix + "%");
            stmt.bind_int64(4, sql_limit);
            stmt.bind_int64(5, sql_offset);
            while (stmt.step() == SQLITE_ROW) {
                int kind = sqlite3_column_int(stmt.get(), 0);
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
                if (!name) continue;
                if (kind == 0) {
                    entries.push_back(DirEntry{.name = name, .is_dir = true, .files = {}});
                    continue;
                }
                const char* pp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
                const char* pfx = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
                const char* fp = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4));
                int ds = sqlite3_column_int(stmt.get(), 5);
                if (!fp) continue;
                FindResult fr;
                fr.pbo_path = pp ? pp : "";
                fr.prefix = pfx ? pfx : "";
                fr.file_path = fp;
                fr.data_size = static_cast<uint32_t>(ds);
                entries.push_back(DirEntry{
                    .name = file_basename_lower(fp),
                    .is_dir = false,
                    .files = {fr},
                });
            }
        }
    } else {
        // No dirs table: fall back to unfiltered list_dir
        return list_dir(dir, limit, offset);
    }

    return entries;
}

} // namespace armatools::pboindex
