#include "armatools/armapath.h"

#include <sstream>

namespace armatools::armapath {

std::optional<std::filesystem::path> find_file_ci(const std::filesystem::path& root,
                                                   const std::string& rel_path) {
    std::string normalized = to_slash(rel_path);
    std::istringstream ss(normalized);
    std::string part;
    std::filesystem::path cur = root;

    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        // Lowercase the component we're looking for
        std::string lower_part = part;
        std::transform(lower_part.begin(), lower_part.end(), lower_part.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool found = false;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
            std::string name = entry.path().filename().string();
            std::string lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower_name == lower_part) {
                cur = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }

    return cur;
}

} // namespace armatools::armapath
