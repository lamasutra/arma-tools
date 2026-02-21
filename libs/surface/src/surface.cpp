#include "armatools/surface.h"

#include <algorithm>
#include <format>

namespace armatools::surface {

std::string_view category_name(Category c) {
    switch (c) {
        case Category::Road:     return "road";
        case Category::Water:    return "water";
        case Category::Forest:   return "forest";
        case Category::Farmland: return "farmland";
        case Category::Rock:     return "rock";
        case Category::Dirt:     return "dirt";
        case Category::Grass:    return "grass";
        case Category::Unknown:  return "unknown";
    }
    return "unknown";
}

const std::vector<Info>& category_table() {
    static const std::vector<Info> table = {
        {Category::Road,     {64, 64, 64}},
        {Category::Water,    {30, 144, 255}},
        {Category::Forest,   {0, 100, 0}},
        {Category::Farmland, {154, 205, 50}},
        {Category::Rock,     {128, 128, 128}},
        {Category::Dirt,     {139, 119, 101}},
        {Category::Grass,    {34, 139, 34}},
        {Category::Unknown,  {255, 255, 255}},
    };
    return table;
}

static bool contains_any(const std::string& s, std::initializer_list<std::string_view> keywords) {
    for (auto kw : keywords)
        if (s.find(kw) != std::string::npos) return true;
    return false;
}

static Info info_for(Category c) {
    for (const auto& i : category_table())
        if (i.category == c) return i;
    return {Category::Unknown, {255, 255, 255}};
}

Info classify(const std::string& filename) {
    std::string s = filename;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (contains_any(s, {"road", "asphalt", "concrete", "runway"})) return info_for(Category::Road);
    if (contains_any(s, {"water", "sea", "ocean"})) return info_for(Category::Water);
    if (contains_any(s, {"forest", "tree"})) return info_for(Category::Forest);
    if (contains_any(s, {"crop", "field"})) return info_for(Category::Farmland);
    if (contains_any(s, {"rock", "stone"})) return info_for(Category::Rock);
    if (contains_any(s, {"dirt", "soil", "sand"})) return info_for(Category::Dirt);
    if (s.find("grass") != std::string::npos) return info_for(Category::Grass);
    return info_for(Category::Unknown);
}

std::string hex(RGB c) {
    return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
}

} // namespace armatools::surface
