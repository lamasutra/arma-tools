#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

namespace armatools::armapath {

// to_slash converts backslashes to forward slashes and trims a leading slash.
inline std::string to_slash(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    if (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

// to_slash_lower is like to_slash but also lowercases the result.
inline std::string to_slash_lower(const std::string& p) {
    std::string s = to_slash(p);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// to_os converts an Arma path to OS-native separators.
inline std::filesystem::path to_os(const std::string& p) {
    return std::filesystem::path(to_slash(p)).make_preferred();
}

// find_file_ci resolves an Arma-style relative path under root using
// case-insensitive matching for each path component.
std::optional<std::filesystem::path> find_file_ci(const std::filesystem::path& root,
                                                   const std::string& rel_path);

// is_procedural_texture returns true if the string is a procedural texture definition.
inline bool is_procedural_texture(const std::string& s) {
    return s.size() >= 2 && s[0] == '#' && s[1] == '(';
}

} // namespace armatools::armapath
