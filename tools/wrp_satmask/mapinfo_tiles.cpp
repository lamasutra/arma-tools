#include "mapinfo_tiles.h"

#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

enum class TileType { Sat, Mask, Unknown };

std::string to_lower(std::string_view input) {
    std::string lower;
    lower.reserve(input.size());
    for (char c : input) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    return text.substr(text.size() - suffix.size()) == suffix;
}

TileType classify_tile(std::string_view lower) {
    if (lower.find("_sat_lco") != std::string_view::npos) return TileType::Sat;
    if (lower.find("_mask_lco") != std::string_view::npos) return TileType::Mask;
    return TileType::Unknown;
}

std::optional<int> parse_int(std::string_view token) {
    if (token.empty()) return std::nullopt;
    int value = 0;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec == std::errc()) return value;
    return std::nullopt;
}

std::optional<std::pair<int, int>> parse_tile_coords(std::string_view lower, std::string_view marker) {
    auto marker_pos = lower.find(marker);
    if (marker_pos == std::string_view::npos) return std::nullopt;

    auto prefix = lower.substr(0, marker_pos);
    if (prefix.empty()) return std::nullopt;

    auto last = prefix.find_last_of('_');
    if (last == std::string_view::npos) return std::nullopt;
    if (last == 0) return std::nullopt;

    auto second = prefix.find_last_of('_', last - 1);
    if (second == std::string_view::npos) return std::nullopt;

    auto x_token = prefix.substr(second + 1, last - (second + 1));
    auto y_token = prefix.substr(last + 1);

    if (x_token.empty() || y_token.empty()) return std::nullopt;

    auto x_opt = parse_int(x_token);
    auto y_opt = parse_int(y_token);
    if (!x_opt || !y_opt) return std::nullopt;

    return std::make_pair(*x_opt, *y_opt);
}

bool has_valid_extension(std::string_view lower) {
    return ends_with(lower, ".paa") || ends_with(lower, ".pac") ||
           ends_with(lower, ".png") || ends_with(lower, ".tga");
}

std::optional<std::pair<int, int>> parse_tile_coords_loose(std::string_view lower) {
    auto slash_pos = lower.find_last_of("/\\");
    if (slash_pos != std::string_view::npos) {
        lower = lower.substr(slash_pos + 1);
    }
    auto dot_pos = lower.find_last_of('.');
    if (dot_pos != std::string_view::npos) {
        lower = lower.substr(0, dot_pos);
    }
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start < lower.size()) {
        size_t pos = lower.find('_', start);
        if (pos == std::string_view::npos) pos = lower.size();
        parts.push_back(lower.substr(start, pos - start));
        start = pos + 1;
    }
    for (size_t i = parts.size(); i-- > 1;) {
        auto y = parse_int(parts[i]);
        auto x = parse_int(parts[i - 1]);
        if (x && y) return std::make_pair(*x, *y);
    }
    return std::nullopt;
}

} // namespace

TileCollections extract_tiles_from_mapinfo(const std::vector<uint8_t>& data) {
    TileCollections out;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t start = pos;
        while (pos < data.size() && data[pos] != '\0') {
            ++pos;
        }

        if (pos > start) {
            std::string path(reinterpret_cast<const char*>(&data[start]), pos - start);
            auto lower = to_lower(path);
            if (has_valid_extension(lower)) {
                TileType type = classify_tile(lower);
                if (type != TileType::Unknown) {
                    TileRef ref;
                    ref.path = std::move(path);
                    std::string marker = (type == TileType::Sat) ? "_sat_lco" : "_mask_lco";
                    if (auto coords = parse_tile_coords(lower, marker); coords) {
                        ref.x = coords->first;
                        ref.y = coords->second;
                    } else if (auto loose_coords = parse_tile_coords_loose(lower); loose_coords) {
                        ref.x = loose_coords->first;
                        ref.y = loose_coords->second;
                    }
                    if (type == TileType::Sat) {
                        out.sat_tiles.push_back(std::move(ref));
                    } else {
                        out.mask_tiles.push_back(std::move(ref));
                    }
                }
            }
        }

        if (pos < data.size() && data[pos] == '\0') {
            ++pos;
        }
    }
    return out;
}
