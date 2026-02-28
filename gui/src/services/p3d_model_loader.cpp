#include "p3d_model_loader.h"

#include <armatools/p3d.h>
#include <armatools/armapath.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

#include "pbo_util.h"
#include "config.h"
#include "cli_logger.h"

P3dModelLoaderService::P3dModelLoaderService(Config* cfg_in,
                                            const std::shared_ptr<armatools::pboindex::DB>& db_in,
                                            const std::shared_ptr<armatools::pboindex::Index>& index_in) {
    this->cfg = cfg_in;
    this->db = db_in;
    this->index = index_in;
};

std::shared_ptr<const armatools::p3d::P3DFile> P3dModelLoaderService::load_p3d(const std::string& model_path) {
    if (model_path.empty()) {
        throw std::runtime_error("P3D model path is empty");
    };

    auto normalized = armatools::armapath::to_slash_lower(model_path);

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(normalized);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    std::shared_ptr<const armatools::p3d::P3DFile> loaded_model;

    // Try pboindex resolve first, but fall back if extraction yields no data.
    if (index) {
        armatools::pboindex::ResolveResult rr;
        if (index->resolve(model_path, rr)) {
            LOGD("P3dModelLoaderService: resolved from index" + model_path
                    + " -> " + rr.pbo_path + " : " + rr.entry_name);
            auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
            if (!data.empty())
                loaded_model = try_load_p3d_from_data(data);
        }
    }

    // Fallback: DB find_files with filename match
    if (!loaded_model && db) {
        auto filename = std::filesystem::path(normalized).filename().string();
        auto results = db->find_files("*" + filename);
        
        for (const auto& r : results) {
            auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
            if (full == normalized || full.ends_with("/" + normalized)) {
                LOGD("P3dModelLoaderService: resolved from db" + model_path
                        + " -> " + r.pbo_path + " : " + r.file_path);

                auto data = extract_from_pbo(r.pbo_path, r.file_path);
                loaded_model = try_load_p3d_from_data(data);
                break;
            }
        }
    }

    // Fallback: disk via case-insensitive path resolution
    if (!loaded_model && cfg && !cfg->drive_root.empty()) {
        auto resolved = armatools::armapath::find_file_ci(
            std::filesystem::path(cfg->drive_root), model_path);
        if (resolved) {
            std::ifstream f(resolved->string(), std::ios::binary);
            if (f.is_open()) {
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                            std::istreambuf_iterator<char>());
                LOGD("P3dModelLoaderService: resolved from disk" + model_path
                        + " -> " + resolved->string());                                            
                loaded_model = try_load_p3d_from_data(data);
            }
        }
    }

    if (!loaded_model) {
        throw std::runtime_error("P3D model not found");
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_[normalized] = loaded_model;
    return loaded_model;
}

std::shared_ptr<const armatools::p3d::P3DFile> P3dModelLoaderService::try_load_p3d_from_data(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        throw std::runtime_error("No data to load");
    };
    std::string buf(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream iss(buf, std::ios::binary);

    return std::make_shared<const armatools::p3d::P3DFile>(armatools::p3d::read(iss));
}

void P3dModelLoaderService::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();
}
