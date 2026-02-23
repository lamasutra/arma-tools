#pragma once

#include <armatools/pboindex.h>
#include <armatools/p3d.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Config;

class P3dModelLoaderService : public std::enable_shared_from_this<P3dModelLoaderService> {
public:
    P3dModelLoaderService(Config* cfg_in,
                          const std::shared_ptr<armatools::pboindex::DB>& db_in,
                          const std::shared_ptr<armatools::pboindex::Index>& index_in);

    armatools::p3d::P3DFile load_p3d(const std::string& model_path);

private:
    std::string db_path;
    Config* cfg = nullptr;
    std::shared_ptr<armatools::pboindex::DB> db;
    std::shared_ptr<armatools::pboindex::Index> index;

    armatools::p3d::P3DFile try_load_p3d_from_data(const std::vector<uint8_t>& data);
};
