#include "mosaic.h"

#include <algorithm>

std::optional<MosaicResult> build_mosaic(const std::vector<RasterTile>& tiles) {
    if (tiles.empty()) return std::nullopt;

    std::optional<int> min_x;
    std::optional<int> min_y;
    std::optional<int> max_x;
    std::optional<int> max_y;
    int tile_width = 0;
    int tile_height = 0;

    for (const auto& tile : tiles) {
        if (tile.ref.x < 0 || tile.ref.y < 0) continue;
        if (tile.image.width <= 0 || tile.image.height <= 0) continue;
        if (tile_width == 0) {
            tile_width = tile.image.width;
            tile_height = tile.image.height;
        }
        if (tile.image.width != tile_width || tile.image.height != tile_height) continue;

        min_x = min_x ? std::min(*min_x, tile.ref.x) : tile.ref.x;
        max_x = max_x ? std::max(*max_x, tile.ref.x) : tile.ref.x;
        min_y = min_y ? std::min(*min_y, tile.ref.y) : tile.ref.y;
        max_y = max_y ? std::max(*max_y, tile.ref.y) : tile.ref.y;
    }

    if (!min_x || !min_y || !max_x || !max_y || tile_width == 0 || tile_height == 0) {
        return std::nullopt;
    }

    MosaicResult result;
    result.tile_width = tile_width;
    result.tile_height = tile_height;
    result.width = (static_cast<int>(*max_x - *min_x + 1)) * tile_width;
    result.height = (static_cast<int>(*max_y - *min_y + 1)) * tile_height;
    result.pixels.assign(static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4, 0);

    for (const auto& tile : tiles) {
        if (tile.ref.x < 0 || tile.ref.y < 0) continue;
        if (tile.image.width != tile_width || tile.image.height != tile_height) continue;

        int offset_x = (tile.ref.x - *min_x) * tile_width;
        int offset_y = (tile.ref.y - *min_y) * tile_height;

        if (offset_x < 0 || offset_y < 0) continue;
        if (offset_x + tile_width > result.width || offset_y + tile_height > result.height) continue;

        for (int row = 0; row < tile_height; ++row) {
            auto row_offset = static_cast<size_t>(offset_y + row) * static_cast<size_t>(result.width);
            auto dst_index = (row_offset + static_cast<size_t>(offset_x)) * 4;
            size_t src_index = static_cast<size_t>(row) * static_cast<size_t>(tile_width) * 4;
            using Diff = std::vector<uint8_t>::difference_type;
            Diff copy_count = static_cast<Diff>(static_cast<size_t>(tile_width) * 4);
            auto dst = result.pixels.begin() + static_cast<Diff>(dst_index);
            std::copy_n(&tile.image.pixels[src_index], copy_count, dst);
        }
        ++result.placed_tiles;
    }

    return result;
}
