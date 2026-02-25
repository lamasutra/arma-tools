#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct TileRef {
    std::string path;
    int x = -1;
    int y = -1;
};

struct TileCollections {
    std::vector<TileRef> sat_tiles;
    std::vector<TileRef> mask_tiles;
};

TileCollections extract_tiles_from_mapinfo(const std::vector<uint8_t>& data);
