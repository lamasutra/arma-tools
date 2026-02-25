#pragma once

#include <armatools/pboindex.h>
#include <armatools/p3d.h>
#include <armatools/paa.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Config;

class LodTexturesLoaderService : public std::enable_shared_from_this<LodTexturesLoaderService> {
public:
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

    std::vector<TextureData> load_textures(armatools::p3d::LOD& lod, const std::string& model_path);
    std::optional<TextureData> load_texture(const std::string& texture_path);
    std::optional<TextureData> load_terrain_texture_entry(const std::string& entry_path);

    LodTexturesLoaderService(const std::string& db_path_in,
                    Config* cfg_in,
                    const std::shared_ptr<armatools::pboindex::DB>& db_in,
                    const std::shared_ptr<armatools::pboindex::Index>& index_in);

private:
    std::string db_path;
    Config* cfg = nullptr;
    std::shared_ptr<armatools::pboindex::DB> db;
    std::shared_ptr<armatools::pboindex::Index> index;

    std::optional<TextureData> load_single_texture(const std::string& tex_path,
                                                   const std::string& model_path);
    std::optional<TextureData> load_single_material(const std::string& material_path,
                                                    const std::string& model_path);
};
