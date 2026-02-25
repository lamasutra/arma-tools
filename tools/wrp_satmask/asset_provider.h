#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <armatools/pboindex.h>

class AssetProvider {
public:
    AssetProvider(std::shared_ptr<armatools::pboindex::Index> index,
                  std::shared_ptr<armatools::pboindex::DB> db);

    std::optional<std::vector<uint8_t>> read(const std::string& virtual_path) const;

private:
    std::optional<std::vector<uint8_t>> read_from_pbo(const std::string& pbo_path,
                                                       const std::string& entry_name) const;

    std::shared_ptr<armatools::pboindex::Index> index_;
    std::shared_ptr<armatools::pboindex::DB> db_;
};
