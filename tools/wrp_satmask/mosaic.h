#pragma once

#include "mapinfo_tiles.h"

#include <armatools/paa.h>

#include <optional>
#include <vector>

struct RasterTile {
    TileRef ref;
    armatools::paa::Image image;
};

struct MosaicResult {
    int width = 0;
    int height = 0;
    int tile_width = 0;
    int tile_height = 0;
    int placed_tiles = 0;
    std::vector<uint8_t> pixels;
};

std::optional<MosaicResult> build_mosaic(const std::vector<RasterTile>& tiles);
