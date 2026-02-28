#pragma once

#include <armatools/pboindex.h>
#include <armatools/p3d.h>
#include <armatools/paa.h>
#include <armatools/rvmat.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Config;

class TexturesLoaderService : public std::enable_shared_from_this<TexturesLoaderService> {
public:
    struct TerrainTextureLayer {
        bool present = false;
        std::string path;
        armatools::paa::Image image;
        armatools::rvmat::UVTransform uv_transform;
    };

    struct TerrainSurfaceLayer {
        TerrainTextureLayer macro;
        TerrainTextureLayer normal;
        TerrainTextureLayer detail;
    };

    struct TerrainLayeredMaterial {
        bool layered = false;
        std::string source_path;
        int surface_count = 0;
        TerrainTextureLayer satellite;
        TerrainTextureLayer mask;
        std::array<TerrainSurfaceLayer, 4> surfaces{};
    };

    struct MaterialParams {
        float ambient[3]{0.18f, 0.18f, 0.18f};
        float diffuse[3]{1.0f, 1.0f, 1.0f};
        float emissive[3]{0.0f, 0.0f, 0.0f};
        float specular[3]{0.08f, 0.08f, 0.08f};
        float specular_power = 32.0f;
        int shader_mode = 0; // 0=default, 1=normal/spec, 2=emissive, 3=alpha-test
    };

    struct TextureData {
        std::string path;
        armatools::paa::Header header;
        armatools::paa::Image image;
        bool resolved_from_material = false;
        bool has_material = false;
        MaterialParams material;
        bool has_normal_map = false;
        armatools::paa::Image normal_map;
        bool has_specular_map = false;
        armatools::paa::Image specular_map;
    };

    std::vector<std::shared_ptr<const TextureData>> load_textures(armatools::p3d::LOD& lod, const std::string& model_path);
    std::shared_ptr<const TextureData> load_texture(const std::string& texture_path);
    std::shared_ptr<const TextureData> load_terrain_texture_entry(const std::string& entry_path);
    std::shared_ptr<const TerrainLayeredMaterial> load_terrain_layered_material(
        const std::vector<std::string>& entry_paths);

    TexturesLoaderService(const std::string& db_path_in,
                    Config* cfg_in,
                    const std::shared_ptr<armatools::pboindex::DB>& db_in,
                    const std::shared_ptr<armatools::pboindex::Index>& index_in);

private:
    struct TextureCacheItem {
        bool has_value = false;
        std::shared_ptr<const TextureData> value;
        uint64_t last_used = 0;
    };

    std::string db_path;
    Config* cfg = nullptr;
    std::shared_ptr<armatools::pboindex::DB> db;
    std::shared_ptr<armatools::pboindex::Index> index;
    std::mutex texture_cache_mutex_;
    std::unordered_map<std::string, TextureCacheItem> texture_cache_;
    uint64_t texture_cache_tick_ = 1;
    size_t texture_cache_capacity_ = 1024;
    struct TerrainLayeredCacheItem {
        std::shared_ptr<const TerrainLayeredMaterial> value;
        uint64_t last_used = 0;
    };

    std::mutex terrain_layered_cache_mutex_;
    std::unordered_map<std::string, TerrainLayeredCacheItem> terrain_layered_cache_;
    uint64_t terrain_layered_cache_tick_ = 1;
    size_t terrain_layered_cache_capacity_ = 1024;

    std::shared_ptr<const TextureData> load_single_texture(const std::string& tex_path,
                                                   const std::string& model_path);
    std::shared_ptr<const TextureData> load_single_material(const std::string& material_path,
                                                    const std::string& model_path);
};
