#include "asset_provider.h"

#include <armatools/armapath.h>
#include <armatools/pbo.h>

#include <fstream>
#include <optional>
#include <sstream>

namespace {

std::string normalize_path(const std::string& path) {
    return armatools::armapath::to_slash_lower(path);
}

} // namespace

AssetProvider::AssetProvider(std::shared_ptr<armatools::pboindex::Index> index,
                             std::shared_ptr<armatools::pboindex::DB> db)
    : index_(std::move(index)), db_(std::move(db)) {}

bool AssetProvider::exists(const std::string& virtual_path) const {
    if (virtual_path.empty()) return false;
    if (!index_ && !db_) return false;

    auto normalized = normalize_path(virtual_path);

    if (index_) {
        armatools::pboindex::ResolveResult rr;
        if (index_->resolve(normalized, rr)) {
            return true;
        }
    }

    if (db_) {
        auto hits = db_->find_files(normalized, "", 1);
        if (!hits.empty()) return true;
    }

    return false;
}

std::optional<std::vector<uint8_t>> AssetProvider::read(const std::string& virtual_path) const {
    if (virtual_path.empty()) return std::nullopt;
    if (!index_ && !db_) return std::nullopt;

    auto normalized = normalize_path(virtual_path);

    if (index_) {
        armatools::pboindex::ResolveResult rr;
        if (index_->resolve(normalized, rr)) {
            if (auto bytes = read_from_pbo(rr.pbo_path, rr.entry_name)) {
                return bytes;
            }
        }
    }

    if (db_) {
        auto hits = db_->find_files(normalized, "", 1);
        for (const auto& hit : hits) {
            if (auto bytes = read_from_pbo(hit.pbo_path, hit.file_path)) {
                return bytes;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> AssetProvider::read_from_pbo(
    const std::string& pbo_path,
    const std::string& entry_name) const {

    std::ifstream f(pbo_path, std::ios::binary);
    if (!f.is_open()) return std::nullopt;

    try {
        auto normalized_target = normalize_path(entry_name);
        auto pbo = armatools::pbo::read(f);
        for (const auto& entry : pbo.entries) {
            if (normalize_path(entry.filename) != normalized_target) continue;
            std::ostringstream out;
            armatools::pbo::extract_file(f, entry, out);
            const std::string data = out.str();
            return std::vector<uint8_t>(data.begin(), data.end());
        }
    } catch (...) {
        return std::nullopt;
    }

    return std::nullopt;
}
