#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace armatools::surface {

enum class Category {
    Road, Water, Forest, Farmland, Rock, Dirt, Grass, Unknown
};

struct RGB { uint8_t r, g, b; };

struct Info {
    Category category;
    RGB color;
};

// category_name returns the string name of a category.
std::string_view category_name(Category c);

// category_table returns all categories with their default display colors.
const std::vector<Info>& category_table();

// classify classifies a texture/rvmat filename into a surface category.
Info classify(const std::string& filename);

// hex returns "#rrggbb" for an RGB color.
std::string hex(RGB c);

} // namespace armatools::surface
