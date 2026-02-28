#include "textures_loader.h"

#include <armatools/armapath.h>
#include <armatools/config.h>
#include <armatools/rvmat.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

#include "pbo_util.h"
#include "config.h"
#include "cli_logger.h"
#include "log_panel.h"
#include "procedural_texture.h"

namespace {

std::string normalize_asset_path(std::string p) {
    p = armatools::armapath::to_slash_lower(std::move(p));
    while (!p.empty() && (p.front() == '/' || p.front() == '\\'))
        p.erase(p.begin());
    return p;
}

std::string join_cache_key(const std::vector<std::string>& values) {
    std::string key;
    key.reserve(values.size() * 32);
    for (const auto& v : values) {
        if (v.empty()) continue;
        if (!key.empty()) key.push_back('|');
        key += v;
    }
    return key;
}

bool starts_with_ascii(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size()
        && s.compare(0, prefix.size(), prefix) == 0;
}

std::string filename_lower(const std::string& p) {
    return armatools::armapath::to_slash_lower(std::filesystem::path(p).filename().string());
}

} // namespace

TexturesLoaderService::TexturesLoaderService(const std::string& db_path_in,
                                     Config* cfg_in,
                                     const std::shared_ptr<armatools::pboindex::DB>& db_in,
                                     const std::shared_ptr<armatools::pboindex::Index>& index_in) {
    db_path = db_path_in;
    cfg = cfg_in;
    db = db_in;
    index = index_in;
}

std::vector<std::shared_ptr<const TexturesLoaderService::TextureData>> 
TexturesLoaderService::load_textures(armatools::p3d::LOD& lod, const std::string& model_path) {
    std::vector<std::shared_ptr<const TextureData>> result;
    LOGD(            "LodTextures: load_textures model=" + model_path
            + " lod_textures=" + std::to_string(lod.textures.size())
            + " lod_materials=" + std::to_string(lod.materials.size()));

    auto add_if_loaded = [&](const std::shared_ptr<const TextureData>& tex) {
        if (!tex) return;
        auto key = armatools::armapath::to_slash_lower(tex->path);
        for (const auto& it : result) {
            if (armatools::armapath::to_slash_lower(it->path) == key) return;
        }
        result.push_back(tex);
    };

    for (const auto& tex_path : lod.textures) {
        if (tex_path.empty()) continue;
        if (armatools::armapath::is_procedural_texture(tex_path)) {
            if (auto img = procedural_texture::generate(tex_path)) {
                armatools::paa::Header hdr;
                hdr.width = img->width;
                hdr.height = img->height;
                add_if_loaded(std::make_shared<const TextureData>(TextureData{tex_path, hdr, *img, false, false, {}, false, {}, false, {}}));
            }
            continue;
        }
        add_if_loaded(load_single_texture(tex_path, model_path));
    }

    for (const auto& mat_path : lod.materials) {
        if (mat_path.empty()) continue;
        add_if_loaded(load_single_material(mat_path, model_path));
    }

    LOGD(            "LodTextures: loaded textures total=" + std::to_string(result.size())
            + " for model=" + model_path);

    return result;
}

std::shared_ptr<const TexturesLoaderService::TextureData> 
TexturesLoaderService::load_single_texture(const std::string& tex_path, 
                                            const std::string& model_path) 
                                            {
    auto normalized = armatools::armapath::to_slash_lower(tex_path);
    while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '\\'))
        normalized.erase(normalized.begin());

    {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        auto it = texture_cache_.find(normalized);
        if (it != texture_cache_.end()) {
            it->second.last_used = texture_cache_tick_++;
            if (it->second.has_value) return it->second.value;
            return nullptr;
        }
    }

    auto cache_result = [&](std::shared_ptr<const TextureData> tex) {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        TextureCacheItem item;
        item.has_value = (tex != nullptr);
        item.value = tex;
        item.last_used = texture_cache_tick_++;
        texture_cache_[normalized] = std::move(item);

        while (texture_cache_.size() > texture_cache_capacity_) {
            auto victim = texture_cache_.end();
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (auto it = texture_cache_.begin(); it != texture_cache_.end(); ++it) {
                if (it->second.last_used < oldest) {
                    oldest = it->second.last_used;
                    victim = it;
                }
            }
            if (victim == texture_cache_.end()) break;
            texture_cache_.erase(victim);
        }
        return tex;
    };

    auto try_decode_data = [&](const std::vector<uint8_t>& data)
        -> std::shared_ptr<const TextureData> {
        if (data.empty()) return nullptr;
        try {
            std::string str(data.begin(), data.end());
            std::istringstream stream(str);
            auto [img, hdr] = armatools::paa::decode(stream);
            if (img.width > 0 && img.height > 0)
                return std::make_shared<const TextureData>(TextureData{tex_path, hdr, img, false, false, {}, false, {}, false, {}});
        } catch (...) {}
        return nullptr;
    };

    auto try_decode_file = [&](const std::filesystem::path& path)
        -> std::shared_ptr<const TextureData> {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return nullptr;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return nullptr;
        try {
            auto [img, hdr] = armatools::paa::decode(f);
            if (img.width > 0 && img.height > 0)
                return std::make_shared<const TextureData>(TextureData{tex_path, hdr, img, false, false, {}, false, {}, false, {}});
        } catch (...) {}
        return nullptr;
    };

    // 1) Resolve via index first
    if (index) {
        armatools::pboindex::ResolveResult rr;
        if (index->resolve(normalized, rr)) {
            auto tex = try_decode_data(extract_from_pbo(rr.pbo_path, rr.entry_name));
            if (tex) return cache_result(tex);
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
                if (tex) return cache_result(tex);
            }
        }
    }

    // 3) Last fallback: disk
    if (!model_path.empty() && cfg && !cfg->drive_root.empty()) {
        auto on_disk = armatools::armapath::to_os(tex_path);
        auto base_dir = std::filesystem::path(model_path).parent_path();
        std::vector<std::filesystem::path> candidates{
            base_dir / on_disk,
            base_dir / on_disk.filename(),
            std::filesystem::path(cfg->drive_root) / on_disk,
        };
        for (const auto& cand : candidates) {
            auto tex = try_decode_file(cand);
            if (tex) return cache_result(tex);
        }
    }

    return cache_result(nullptr);
}

std::shared_ptr<const TexturesLoaderService::TextureData>
TexturesLoaderService::load_single_material(const std::string& material_path,
                                               const std::string& model_path) {
    auto normalize = [](std::string p) {
        p = armatools::armapath::to_slash_lower(p);
        while (!p.empty() && (p.front() == '/' || p.front() == '\\'))
            p.erase(p.begin());
        return p;
    };
    auto mat_norm = normalize(material_path);
    if (mat_norm.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        auto it = texture_cache_.find(mat_norm);
        if (it != texture_cache_.end()) {
            it->second.last_used = texture_cache_tick_++;
            if (it->second.has_value) return it->second.value;
            return nullptr;
        }
    }

    auto cache_result = [&](std::shared_ptr<const TextureData> tex) {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        TextureCacheItem item;
        item.has_value = (tex != nullptr);
        item.value = tex;
        item.last_used = texture_cache_tick_++;
        texture_cache_[mat_norm] = std::move(item);

        while (texture_cache_.size() > texture_cache_capacity_) {
            auto victim = texture_cache_.end();
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (auto it = texture_cache_.begin(); it != texture_cache_.end(); ++it) {
                if (it->second.last_used < oldest) {
                    oldest = it->second.last_used;
                    victim = it;
                }
            }
            if (victim == texture_cache_.end()) break;
            texture_cache_.erase(victim);
        }
        return tex;
    };

    LOGD(            "LodTextures: material begin raw='" + material_path
            + "' model='" + model_path + "'");
    auto shader_mode_from_ids = [](const std::string& ps, const std::string& vs) {
        auto sps = armatools::armapath::to_slash_lower(ps);
        auto svs = armatools::armapath::to_slash_lower(vs);
        if (sps.find("normal") != std::string::npos || sps.find("spec") != std::string::npos
            || svs.find("normal") != std::string::npos)
            return 1;
        if (sps.find("emis") != std::string::npos || sps.find("self") != std::string::npos)
            return 2;
        if (sps.find("alpha") != std::string::npos || sps.find("alphatest") != std::string::npos
            || sps.find("foliage") != std::string::npos || sps.find("tree") != std::string::npos)
            return 3;
        return 0;
    };

    auto parse_material = [&](const std::vector<uint8_t>& data,
                              TextureData& io_out,
                              std::vector<std::string>& out_stage_textures) -> bool {
        if (data.size() < 4) return false;
        try {
            std::string str(data.begin(), data.end());
            std::istringstream ss(str, std::ios::binary);
            armatools::config::Config cfg_parsed;
            if (data[0] == 0 && data[1] == 'r' && data[2] == 'a' && data[3] == 'P')
                cfg_parsed = armatools::config::read(ss);
            else
                cfg_parsed = armatools::config::parse_text(ss);
            auto mat = armatools::rvmat::parse(cfg_parsed);

            io_out.has_material = true;
            io_out.material.ambient[0] = mat.ambient[0];
            io_out.material.ambient[1] = mat.ambient[1];
            io_out.material.ambient[2] = mat.ambient[2];
            io_out.material.diffuse[0] = mat.diffuse[0];
            io_out.material.diffuse[1] = mat.diffuse[1];
            io_out.material.diffuse[2] = mat.diffuse[2];
            io_out.material.emissive[0] = mat.emissive[0];
            io_out.material.emissive[1] = mat.emissive[1];
            io_out.material.emissive[2] = mat.emissive[2];
            io_out.material.specular[0] = mat.specular[0];
            io_out.material.specular[1] = mat.specular[1];
            io_out.material.specular[2] = mat.specular[2];
            io_out.material.specular_power = std::max(2.0f, mat.specular_power);
            io_out.material.shader_mode = shader_mode_from_ids(mat.pixel_shader, mat.vertex_shader);
            out_stage_textures.clear();
            for (const auto& st : mat.stages) {
                if (st.texture_path.empty()) continue;
                out_stage_textures.push_back(st.texture_path);
            }
            return true;
        } catch (...) {
            return false;
        }
        return false;
    };

    auto load_bytes = [&](const std::string& asset_path) -> std::vector<uint8_t> {
        auto normalized = normalize(asset_path);
        if (normalized.empty()) return {};

        if (index) {
            armatools::pboindex::ResolveResult rr;
            if (index->resolve(normalized, rr)) {
                auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
                if (!data.empty()) {
                    LOGD(                            "LodTextures: material asset resolved via index '" + normalized
                            + "' -> '" + rr.pbo_path + ":" + rr.entry_name + "'");
                    return data;
                }
            }
        }
        if (db) {
            auto filename = std::filesystem::path(normalized).filename().string();
            auto results = db->find_files("*" + filename);
            for (const auto& r : results) {
                auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
                if (full == normalized || full.ends_with("/" + normalized)) {
                    auto data = extract_from_pbo(r.pbo_path, r.file_path);
                    if (!data.empty()) {
                        LOGD(                                "LodTextures: material asset resolved via db '" + normalized
                                + "' -> '" + r.pbo_path + ":" + r.file_path + "'");
                        return data;
                    }
                }
            }
        }
        if (cfg && !cfg->drive_root.empty()) {
            auto resolved = armatools::armapath::find_file_ci(
                std::filesystem::path(cfg->drive_root), normalized);
            if (resolved) {
                std::ifstream f(resolved->string(), std::ios::binary);
                if (f.is_open()) {
                    LOGD(                            "LodTextures: material asset resolved on disk '" + normalized
                            + "' -> '" + resolved->string() + "'");
                    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                                std::istreambuf_iterator<char>());
                }
            }
        }
        return {};
    };

    auto decode_texture_bytes = [&](const std::vector<uint8_t>& data,
                                    const std::string& key)
        -> std::shared_ptr<TextureData> {
        if (data.empty()) return nullptr;
        try {
            std::string str(data.begin(), data.end());
            std::istringstream stream(str);
            auto [img, hdr] = armatools::paa::decode(stream);
            if (img.width > 0 && img.height > 0)
                return std::make_shared<TextureData>(TextureData{key, hdr, img, false, false, {}, false, {}, false, {}});
        } catch (...) {}
        return nullptr;
    };

    auto resolve_relative = [&](const std::string& base, const std::string& rel) {
        auto nrel = normalize(rel);
        if (nrel.empty()) return std::string{};
        if (nrel.starts_with("a3/") || nrel.starts_with("ca/")
            || nrel.starts_with("cup/") || nrel.starts_with("dz/")) {
            return nrel;
        }
        std::filesystem::path out =
            std::filesystem::path(normalize(base)).parent_path() / std::filesystem::path(nrel);
        return normalize(out.generic_string());
    };

    LOGD("LodTextures: material normalized='" + mat_norm + "'");

    std::vector<std::string> mat_candidates{mat_norm};
    if (std::filesystem::path(mat_norm).extension().empty())
        mat_candidates.push_back(mat_norm + ".rvmat");
    {
        std::string joined;
        for (size_t i = 0; i < mat_candidates.size(); ++i) {
            if (i) joined += ", ";
            joined += mat_candidates[i];
        }
        LOGD("LodTextures: material candidates=[" + joined + "]");
    }

    // Common fallback for paths like "\buildings\data\..." from CA models.
    if (!model_path.empty()) {
        auto model_norm = normalize(model_path);
        auto model_parts = std::filesystem::path(model_norm);
        auto root_part = model_parts.begin() != model_parts.end()
            ? model_parts.begin()->string() : std::string();
        if (!root_part.empty() && !mat_norm.starts_with(root_part + "/")) {
            auto prefixed = normalize(root_part + "/" + mat_norm);
            mat_candidates.push_back(prefixed);
            if (std::filesystem::path(prefixed).extension().empty())
                mat_candidates.push_back(prefixed + ".rvmat");
            LOGD(                    "LodTextures: added model-root material fallback '" + prefixed + "'");
        }
    }

    std::vector<uint8_t> mat_bytes;
    std::string mat_used;
    for (const auto& m : mat_candidates) {
        mat_bytes = load_bytes(m);
        if (!mat_bytes.empty()) {
            mat_used = m;
            break;
        }
    }
    if (mat_bytes.empty()) {
        LOGW(                "LodTextures: material not found raw='" + material_path
                + "' normalized='" + mat_norm + "'");
        return cache_result(nullptr);
    }
    LOGD("LodTextures: material loaded as '" + mat_used + "'");

    TextureData material_result;
    material_result.path = mat_norm;
    std::vector<std::string> stage_textures;
    parse_material(mat_bytes, material_result, stage_textures);
    if (!stage_textures.empty()) {
        LOGD(                "LodTextures: rvmat parsed stages=" + std::to_string(stage_textures.size())
                + " material='" + mat_used + "'");
    } else {
        LOGW(                "LodTextures: rvmat parser produced no stage textures '" + mat_used + "'");
    }

    std::vector<std::string> candidates = stage_textures;
    if (candidates.empty()) {
        // Legacy fallback for non-standard / partially parsed rvmat content.
        std::string text(reinterpret_cast<const char*>(mat_bytes.data()), mat_bytes.size());
        static const std::regex rx("\"([^\"]+\\.(?:paa|pac))\"", std::regex_constants::icase);
        for (auto it = std::sregex_iterator(text.begin(), text.end(), rx);
             it != std::sregex_iterator(); ++it) {
            if (it->size() >= 2) candidates.push_back((*it)[1].str());
        }
    }
    if (candidates.empty()) {
        LOGW(                "LodTextures: rvmat has no stage textures '" + mat_used + "'");
        return cache_result(nullptr);
    }
    LOGD(            "LodTextures: rvmat stage texture candidates count="
            + std::to_string(candidates.size()) + " material='" + mat_used + "'");

    auto stage_kind = [](const std::string& p) {
        auto s = armatools::armapath::to_slash_lower(p);
        if (armatools::armapath::is_procedural_texture(s) &&
            (s.find("normal") != std::string::npos || s.find("nohq") != std::string::npos))
            return std::string("normal");
        if (armatools::armapath::is_procedural_texture(s) &&
            (s.find("spec") != std::string::npos || s.find("smdi") != std::string::npos))
            return std::string("spec");
        if (s.find("_nohq.") != std::string::npos) return std::string("normal");
        if (s.find("_smdi.") != std::string::npos) return std::string("spec");
        return std::string("diffuse");
    };
    auto stage_score = [](const std::string& p, const std::string& kind) {
        auto s = armatools::armapath::to_slash_lower(p);
        if (kind == "normal")
            return s.find("_nohq.") != std::string::npos ? 100 : 0;
        if (kind == "spec")
            return s.find("_smdi.") != std::string::npos ? 100 : 0;
        int v = 0;
        if (s.find("_mco.") != std::string::npos) v += 40;
        else if (s.find("_co.") != std::string::npos) v += 30;
        else if (s.find("_ca.") != std::string::npos) v += 20;
        if (s.find("_smdi.") != std::string::npos) v -= 25;
        if (s.find("_nohq.") != std::string::npos) v -= 25;
        if (s.find("_as.") != std::string::npos) v -= 20;
        return v;
    };

    auto load_image = [&](const std::string& tex) -> std::optional<armatools::paa::Image> {
        if (armatools::armapath::is_procedural_texture(tex)) {
            return procedural_texture::generate(tex);
        }
        auto decode_img = [&](const std::vector<uint8_t>& bytes)
            -> std::optional<armatools::paa::Image> {
            if (bytes.empty()) return std::nullopt;
            try {
                std::string str(bytes.begin(), bytes.end());
                std::istringstream stream(str);
                auto [img, _hdr] = armatools::paa::decode(stream);
                if (img.width > 0 && img.height > 0) return img;
            } catch (...) {}
            return std::nullopt;
        };
        if (auto img = decode_img(load_bytes(tex))) return img;
        if (std::filesystem::path(tex).extension().empty()) {
            if (auto img = decode_img(load_bytes(tex + ".paa"))) return img;
            if (auto img = decode_img(load_bytes(tex + ".pac"))) return img;
        }
        return std::nullopt;
    };

    std::string best_diff, best_nrm, best_spec;
    int best_diff_score = -1, best_nrm_score = -1, best_spec_score = -1;
    for (const auto& c : candidates) {
        auto kind = stage_kind(c);
        auto s = stage_score(c, kind);
        if (kind == "diffuse" && s > best_diff_score) {
            best_diff_score = s;
            best_diff = c;
        } else if (kind == "normal" && s > best_nrm_score) {
            best_nrm_score = s;
            best_nrm = c;
        } else if (kind == "spec" && s > best_spec_score) {
            best_spec_score = s;
            best_spec = c;
        }
    }
    if (best_diff.empty()) best_diff = candidates.front();

    std::vector<std::string> diffuse_order{best_diff};
    for (const auto& c : candidates) {
        if (c != best_diff && stage_kind(c) == "diffuse") diffuse_order.push_back(c);
    }

    for (const auto& c : diffuse_order) {
        auto tex = armatools::armapath::is_procedural_texture(c) ? c : resolve_relative(mat_used, c);
        if (tex.empty()) continue;
        LOGD(                "LodTextures: try rvmat texture stage='" + c + "' resolved='" + tex + "'");
        if (auto out = decode_texture_bytes(load_bytes(tex), mat_norm)) {
            out->has_material = material_result.has_material;
            out->resolved_from_material = true;
            out->material = material_result.material;
            if (!best_nrm.empty()) {
                auto nrm = resolve_relative(mat_used, best_nrm);
                if (auto img = load_image(nrm)) {
                    out->has_normal_map = true;
                    out->normal_map = std::move(*img);
                    LOGD(                            "LodTextures: rvmat normal map loaded '" + nrm + "' for material '" + mat_used + "'");
                }
            }
            if (!best_spec.empty()) {
                auto sm = resolve_relative(mat_used, best_spec);
                if (auto img = load_image(sm)) {
                    out->has_specular_map = true;
                    out->specular_map = std::move(*img);
                    LOGD(                            "LodTextures: rvmat spec map loaded '" + sm + "' for material '" + mat_used + "'");
                }
            }
            LOGD(                    "LodTextures: rvmat texture loaded '" + tex + "' for material '" + mat_used + "'");
            return cache_result(out);
        }
        if (std::filesystem::path(tex).extension().empty()) {
            if (auto out = decode_texture_bytes(load_bytes(tex + ".paa"), mat_norm)) {
                out->has_material = material_result.has_material;
                out->resolved_from_material = true;
                out->material = material_result.material;
                if (!best_nrm.empty()) {
                    auto nrm = resolve_relative(mat_used, best_nrm);
                    if (auto img = load_image(nrm)) {
                        out->has_normal_map = true;
                        out->normal_map = std::move(*img);
                    }
                }
                if (!best_spec.empty()) {
                    auto sm = resolve_relative(mat_used, best_spec);
                    if (auto img = load_image(sm)) {
                        out->has_specular_map = true;
                        out->specular_map = std::move(*img);
                    }
                }
                LOGD(                        "LodTextures: rvmat texture loaded '" + tex + ".paa' for material '" + mat_used + "'");
                return cache_result(out);
            }
            if (auto out = decode_texture_bytes(load_bytes(tex + ".pac"), mat_norm)) {
                out->has_material = material_result.has_material;
                out->resolved_from_material = true;
                out->material = material_result.material;
                if (!best_nrm.empty()) {
                    auto nrm = resolve_relative(mat_used, best_nrm);
                    if (auto img = load_image(nrm)) {
                        out->has_normal_map = true;
                        out->normal_map = std::move(*img);
                    }
                }
                if (!best_spec.empty()) {
                    auto sm = resolve_relative(mat_used, best_spec);
                    if (auto img = load_image(sm)) {
                        out->has_specular_map = true;
                        out->specular_map = std::move(*img);
                    }
                }
                LOGD(                        "LodTextures: rvmat texture loaded '" + tex + ".pac' for material '" + mat_used + "'");
                return cache_result(out);
            }
        }
    }
    LOGW(            "LodTextures: failed to load any rvmat texture for material '" + mat_used + "'");
    return cache_result(nullptr);
}

std::shared_ptr<const TexturesLoaderService::TextureData>
TexturesLoaderService::load_texture(const std::string& texture_path) {
    if (texture_path.empty()) return nullptr;
    return load_single_texture(texture_path, "");
}

std::optional<TexturesLoaderService::TerrainLayeredMaterial>
TexturesLoaderService::load_terrain_layered_material(const std::vector<std::string>& entry_paths) {
    std::vector<std::string> candidates;
    candidates.reserve(entry_paths.size());
    for (const auto& path : entry_paths) {
        const auto normalized = normalize_asset_path(path);
        if (normalized.empty()) continue;
        if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
            candidates.push_back(normalized);
    }
    if (candidates.empty()) return std::nullopt;

    const auto cache_key = join_cache_key(candidates);
    if (cache_key.empty()) return std::nullopt;

    {
        std::lock_guard<std::mutex> lock(terrain_layered_cache_mutex_);
        auto it = terrain_layered_cache_.find(cache_key);
        if (it != terrain_layered_cache_.end()) {
            terrain_layered_cache_last_used_[cache_key] = terrain_layered_cache_tick_++;
            return it->second;
        }
        if (terrain_layered_cache_missing_.find(cache_key) != terrain_layered_cache_missing_.end()) {
            return std::nullopt;
        }
    }

    auto resolve_relative = [](const std::string& base, const std::string& rel) {
        auto nrel = normalize_asset_path(rel);
        if (nrel.empty()) return std::string{};
        if (nrel.starts_with("a3/") || nrel.starts_with("ca/")
            || nrel.starts_with("cup/") || nrel.starts_with("dz/")) {
            return nrel;
        }
        std::filesystem::path out =
            std::filesystem::path(normalize_asset_path(base)).parent_path()
            / std::filesystem::path(nrel);
        return normalize_asset_path(out.generic_string());
    };

    auto load_asset_bytes = [&](const std::string& raw_path) -> std::vector<uint8_t> {
        const auto normalized = normalize_asset_path(raw_path);
        if (normalized.empty()) return {};

        if (index) {
            armatools::pboindex::ResolveResult rr;
            if (index->resolve(normalized, rr)) {
                auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
                if (!data.empty()) return data;
            }
        }
        if (db) {
            auto filename = std::filesystem::path(normalized).filename().string();
            auto results = db->find_files("*" + filename);
            for (const auto& r : results) {
                auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
                if (full == normalized || full.ends_with("/" + normalized)) {
                    auto data = extract_from_pbo(r.pbo_path, r.file_path);
                    if (!data.empty()) return data;
                }
            }
        }
        if (cfg && !cfg->drive_root.empty()) {
            auto resolved = armatools::armapath::find_file_ci(
                std::filesystem::path(cfg->drive_root), normalized);
            if (resolved) {
                std::ifstream f(resolved->string(), std::ios::binary);
                if (f.is_open()) {
                    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                                std::istreambuf_iterator<char>());
                }
            }
        }
        return {};
    };

    auto decode_image_bytes = [](const std::vector<uint8_t>& bytes) -> std::optional<armatools::paa::Image> {
        if (bytes.empty()) return std::nullopt;
        try {
            std::string str(bytes.begin(), bytes.end());
            std::istringstream stream(str);
            auto [img, _hdr] = armatools::paa::decode(stream);
            if (img.width > 0 && img.height > 0) return img;
        } catch (...) {}
        return std::nullopt;
    };

    auto decode_texture_layer =
        [&](const std::string& material_path,
            const armatools::rvmat::TextureStage& stage)
            -> TexturesLoaderService::TerrainTextureLayer {
        TerrainTextureLayer out;
        if (stage.texture_path.empty()) return out;
        std::string resolved = armatools::armapath::is_procedural_texture(stage.texture_path)
            ? stage.texture_path
            : resolve_relative(material_path, stage.texture_path);
        if (resolved.empty()) return out;

        if (armatools::armapath::is_procedural_texture(resolved)) {
            if (auto img = procedural_texture::generate(resolved)) {
                out.present = true;
                out.path = resolved;
                out.image = std::move(*img);
                out.uv_transform = stage.uv_transform;
            }
            return out;
        }

        if (auto img = decode_image_bytes(load_asset_bytes(resolved))) {
            out.present = true;
            out.path = resolved;
            out.image = std::move(*img);
            out.uv_transform = stage.uv_transform;
            return out;
        }

        if (std::filesystem::path(resolved).extension().empty()) {
            if (auto img = decode_image_bytes(load_asset_bytes(resolved + ".paa"))) {
                out.present = true;
                out.path = resolved + ".paa";
                out.image = std::move(*img);
                out.uv_transform = stage.uv_transform;
                return out;
            }
            if (auto img = decode_image_bytes(load_asset_bytes(resolved + ".pac"))) {
                out.present = true;
                out.path = resolved + ".pac";
                out.image = std::move(*img);
                out.uv_transform = stage.uv_transform;
                return out;
            }
        }
        return out;
    };

    auto parse_layered_from_material =
        [&](const std::string& material_path, const std::vector<uint8_t>& bytes)
        -> std::optional<TerrainLayeredMaterial> {
        if (bytes.empty()) return std::nullopt;
        armatools::rvmat::Material mat;
        try {
            std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            mat = armatools::rvmat::parse_bytes(text);
        } catch (...) {
            return std::nullopt;
        }

        std::vector<armatools::rvmat::TextureStage> stages;
        stages.reserve(mat.stages.size());
        for (const auto& st : mat.stages) {
            if (st.texture_path.empty()) continue;
            stages.push_back(st);
        }

        TerrainLayeredMaterial out;
        out.source_path = material_path;

        if (stages.size() >= 5
            && starts_with_ascii(filename_lower(stages[0].texture_path), "s_")
            && starts_with_ascii(filename_lower(stages[1].texture_path), "m_")
            && ((stages.size() - 2) % 3) == 0) {
            const int grouped_surfaces = static_cast<int>((stages.size() - 2) / 3);
            out.surface_count = std::clamp(grouped_surfaces, 0, 4);
            out.layered = out.surface_count > 0;
        }

        if (out.layered) {
            out.satellite = decode_texture_layer(material_path, stages[0]);
            out.mask = decode_texture_layer(material_path, stages[1]);
            for (int i = 0; i < out.surface_count; ++i) {
                const int base = 2 + i * 3;
                if ((base + 2) >= static_cast<int>(stages.size())) break;
                const auto s0 = static_cast<size_t>(base + 0);
                const auto s1 = static_cast<size_t>(base + 1);
                const auto s2 = static_cast<size_t>(base + 2);
                out.surfaces[static_cast<size_t>(i)].macro = decode_texture_layer(material_path, stages[s0]);
                out.surfaces[static_cast<size_t>(i)].normal = decode_texture_layer(material_path, stages[s1]);
                out.surfaces[static_cast<size_t>(i)].detail = decode_texture_layer(material_path, stages[s2]);
            }
            if (out.satellite.present || out.mask.present) {
                return out;
            }
        }

        auto score_stage = [](const armatools::rvmat::TextureStage& stage) {
            auto s = armatools::armapath::to_slash_lower(stage.texture_path);
            int v = 0;
            if (s.find("_mco.") != std::string::npos) v += 40;
            else if (s.find("_co.") != std::string::npos) v += 30;
            else if (s.find("_ca.") != std::string::npos) v += 20;
            if (s.find("_smdi.") != std::string::npos) v -= 30;
            if (s.find("_nohq.") != std::string::npos) v -= 30;
            return v;
        };

        if (!stages.empty()) {
            auto best_it = std::max_element(
                stages.begin(), stages.end(),
                [&](const auto& a, const auto& b) { return score_stage(a) < score_stage(b); });
            out.layered = false;
            out.surface_count = 0;
            out.satellite = decode_texture_layer(material_path, *best_it);
            if (out.satellite.present) return out;
        }

        return std::nullopt;
    };

    std::optional<TerrainLayeredMaterial> resolved;
    for (const auto& raw : candidates) {
        std::vector<std::string> mat_candidates{raw};
        if (std::filesystem::path(raw).extension().empty())
            mat_candidates.push_back(raw + ".rvmat");
        for (const auto& mat_candidate : mat_candidates) {
            auto bytes = load_asset_bytes(mat_candidate);
            if (bytes.empty()) continue;
            if (auto layered = parse_layered_from_material(mat_candidate, bytes)) {
                resolved = std::move(*layered);
                break;
            }
        }
        if (resolved) break;

        std::vector<std::string> tex_candidates{raw};
        if (std::filesystem::path(raw).extension().empty()) {
            tex_candidates.push_back(raw + ".paa");
            tex_candidates.push_back(raw + ".pac");
        }
        for (const auto& tex : tex_candidates) {
            auto bytes = load_asset_bytes(tex);
            if (auto img = decode_image_bytes(bytes)) {
                TerrainLayeredMaterial fallback;
                fallback.layered = false;
                fallback.source_path = tex;
                fallback.surface_count = 0;
                fallback.satellite.present = true;
                fallback.satellite.path = tex;
                fallback.satellite.image = std::move(*img);
                resolved = std::move(fallback);
                break;
            }
        }
        if (resolved) break;
    }

    {
        std::lock_guard<std::mutex> lock(terrain_layered_cache_mutex_);
        if (resolved) {
            terrain_layered_cache_[cache_key] = *resolved;
            terrain_layered_cache_last_used_[cache_key] = terrain_layered_cache_tick_++;
            terrain_layered_cache_missing_.erase(cache_key);
        } else {
            terrain_layered_cache_missing_.insert(cache_key);
        }

        while (terrain_layered_cache_.size() > terrain_layered_cache_capacity_) {
            auto victim = terrain_layered_cache_last_used_.end();
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (auto it = terrain_layered_cache_last_used_.begin();
                 it != terrain_layered_cache_last_used_.end(); ++it) {
                if (it->second < oldest) {
                    oldest = it->second;
                    victim = it;
                }
            }
            if (victim == terrain_layered_cache_last_used_.end()) break;
            terrain_layered_cache_.erase(victim->first);
            terrain_layered_cache_missing_.erase(victim->first);
            terrain_layered_cache_last_used_.erase(victim);
        }
    }

    return resolved;
}

std::shared_ptr<const TexturesLoaderService::TextureData>
TexturesLoaderService::load_terrain_texture_entry(const std::string& entry_path) {
    if (entry_path.empty()) return nullptr;
    auto normalized = normalize_asset_path(entry_path);
    if (normalized.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        auto it = texture_cache_.find(normalized);
        if (it != texture_cache_.end()) {
            it->second.last_used = texture_cache_tick_++;
            if (it->second.has_value) return it->second.value;
            return nullptr;
        }
    }

    const auto ext = std::filesystem::path(normalized).extension().string();
    std::shared_ptr<const TextureData> resolved;

    if (ext == ".rvmat") resolved = load_single_material(entry_path, "");
    else if (ext == ".paa" || ext == ".pac") resolved = load_single_texture(entry_path, "");
    else {
        if (auto from_mat_entry = load_single_material(entry_path, "")) resolved = std::move(from_mat_entry);
        else if (auto from_tex_entry = load_single_texture(entry_path, "")) resolved = std::move(from_tex_entry);
        else if (ext.empty()) {
            if (auto from_mat_ext = load_single_material(entry_path + ".rvmat", "")) resolved = std::move(from_mat_ext);
            else if (auto from_tex_paa = load_single_texture(entry_path + ".paa", "")) resolved = std::move(from_tex_paa);
            else if (auto from_tex_pac = load_single_texture(entry_path + ".pac", "")) resolved = std::move(from_tex_pac);
        }
    }

    {
        std::lock_guard<std::mutex> lock(texture_cache_mutex_);
        TextureCacheItem item;
        item.has_value = (resolved != nullptr);
        item.value = resolved;
        item.last_used = texture_cache_tick_++;
        texture_cache_[normalized] = std::move(item);

        while (texture_cache_.size() > texture_cache_capacity_) {
            auto victim = texture_cache_.end();
            uint64_t oldest = std::numeric_limits<uint64_t>::max();
            for (auto it = texture_cache_.begin(); it != texture_cache_.end(); ++it) {
                if (it->second.last_used < oldest) {
                    oldest = it->second.last_used;
                    victim = it;
                }
            }
            if (victim == texture_cache_.end()) break;
            texture_cache_.erase(victim);
        }
    }

    return resolved;
}
