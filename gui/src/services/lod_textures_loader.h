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
    struct TextureData {
        std::string path;
        armatools::paa::Header header;
        armatools::paa::Image image;
    };

    std::vector<TextureData> load_textures(armatools::p3d::LOD& lod, const std::string& model_path);

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
};
