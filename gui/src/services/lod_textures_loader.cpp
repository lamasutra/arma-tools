#include "lod_textures_loader.h"

#include <armatools/armapath.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "pbo_util.h"
#include "config.h"
#include "cli_logger.h"

LodTexturesLoaderService::LodTexturesLoaderService(const std::string& db_path_in,
                                     Config* cfg_in,
                                     const std::shared_ptr<armatools::pboindex::DB>& db_in,
                                     const std::shared_ptr<armatools::pboindex::Index>& index_in) {
    db_path = db_path_in;
    cfg = cfg_in;
    db = db_in;
    index = index_in;
}

std::vector<LodTexturesLoaderService::TextureData> 
LodTexturesLoaderService::load_textures(armatools::p3d::LOD& lod, const std::string& model_path) {
    std::vector<TextureData> result;

    for (const auto& tex_path : lod.textures) {
        if (tex_path.empty()) continue;
        if (armatools::armapath::is_procedural_texture(tex_path)) continue;

        auto tex = load_single_texture(tex_path, model_path);
        if (tex) {
            result.push_back(tex.value());
        }
    }

    return result;
}

std::optional<LodTexturesLoaderService::TextureData> 
LodTexturesLoaderService::load_single_texture(const std::string& tex_path, const std::string& model_path) {
    auto try_decode_data = [&](const std::vector<uint8_t>& data)
        -> std::optional<TextureData> {
        if (data.empty()) return std::nullopt;
        try {
            std::string str(data.begin(), data.end());
            std::istringstream stream(str);
            auto [img, hdr] = armatools::paa::decode(stream);
            if (img.width > 0 && img.height > 0)
                return TextureData{tex_path, hdr, img};
        } catch (...) {}
        return std::nullopt;
    };

    auto try_decode_file = [&](const std::filesystem::path& path)
        -> std::optional<TextureData> {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return std::nullopt;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return std::nullopt;
        try {
            auto [img, hdr] = armatools::paa::decode(f);
            if (img.width > 0 && img.height > 0)
                return TextureData{tex_path, hdr, img};
        } catch (...) {}
        return std::nullopt;
    };

    auto normalized = armatools::armapath::to_slash_lower(tex_path);

    // 1) Resolve via index first
    if (index) {
        armatools::pboindex::ResolveResult rr;
        if (index->resolve(normalized, rr)) {
            auto tex = try_decode_data(extract_from_pbo(rr.pbo_path, rr.entry_name));
            if (tex) return tex;
        }
    }

    // 2) Fallback via DB file search
    if (db) {
        auto filename = std::filesystem::path(normalized).filename().string();
        auto results = db->find_files("*" + filename);
        for (const auto& r : results) {
            auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
            if (full == normalized || full.ends_with("/" + normalized)) {
                auto tex = try_decode_data(extract_from_pbo(r.pbo_path, r.file_path));
                if (tex) return tex;
            }
        }
    }

    // 3) Last fallback: disk
    if (cfg && !cfg->drive_root.empty()) {
        auto on_disk = armatools::armapath::to_os(tex_path);
        auto base_dir = std::filesystem::path(model_path).parent_path();
        std::vector<std::filesystem::path> candidates{
            base_dir / on_disk,
            base_dir / on_disk.filename(),
            std::filesystem::path(cfg->drive_root) / on_disk,
        };
        for (const auto& cand : candidates) {
            auto tex = try_decode_file(cand);
            if (tex) return tex;
        }
    }

    return std::nullopt;
}
