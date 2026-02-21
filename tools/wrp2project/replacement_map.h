#pragma once

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct ReplacementEntry {
    std::string old_model;
    std::string new_model;
};

inline std::string rmap_trim(std::string s) {
    while (!s.empty() &&
           (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i > 0) s.erase(0, i);
    return s;
}

inline std::string rmap_to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string rmap_norm_path(std::string s) {
    s = rmap_trim(std::move(s));
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s[0] == '/') s.erase(0, 1);
    return rmap_to_lower(std::move(s));
}

inline std::string rmap_strip_p3d(std::string s) {
    if (s.size() >= 4 && s.substr(s.size() - 4) == ".p3d") s.resize(s.size() - 4);
    return s;
}

inline std::string rmap_base_name(const std::string& s) {
    for (size_t i = s.size(); i > 0; i--) {
        if (s[i - 1] == '\\' || s[i - 1] == '/') return s.substr(i);
    }
    return s;
}

struct ReplacementMap {
    std::unordered_map<std::string, std::string> exact;     // normalized full path -> new
    std::unordered_map<std::string, std::string> basename;  // normalized basename -> new
    std::vector<ReplacementEntry> entries;

    std::pair<std::string, bool> lookup(const std::string& model_name) const {
        auto norm = rmap_norm_path(model_name);
        if (auto it = exact.find(norm); it != exact.end()) return {it->second, true};

        auto norm_no_ext = rmap_strip_p3d(norm);
        if (auto it = exact.find(norm_no_ext); it != exact.end()) return {it->second, true};

        auto base = rmap_base_name(norm);
        if (auto it = basename.find(base); it != basename.end()) return {it->second, true};

        auto base_no_ext = rmap_strip_p3d(base);
        if (auto it = basename.find(base_no_ext); it != basename.end()) return {it->second, true};

        return {"", false};
    }

    bool is_matched(const std::string& model_name) const {
        auto [n, found] = lookup(model_name);
        return found && rmap_to_lower(n) != "unmatched";
    }

    void add_entry(const std::string& old_model, const std::string& new_model) {
        auto key = rmap_norm_path(old_model);
        auto key_no_ext = rmap_strip_p3d(key);

        for (auto& e : entries) {
            if (rmap_norm_path(e.old_model) == key) {
                e.new_model = new_model;
                exact[key] = new_model;
                exact[key_no_ext] = new_model;
                return;
            }
        }

        exact[key] = new_model;
        exact[key_no_ext] = new_model;
        entries.push_back({old_model, new_model});

        if (rmap_to_lower(new_model) != "unmatched") {
            auto base = rmap_base_name(key);
            auto base_no_ext = rmap_strip_p3d(base);
            if (basename.find(base) == basename.end()) basename[base] = new_model;
            if (basename.find(base_no_ext) == basename.end()) basename[base_no_ext] = new_model;
        }
    }

    int len() const { return static_cast<int>(entries.size()); }
};

inline ReplacementMap load_replacements(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("opening replacement file: " + path);

    ReplacementMap rm;
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        line_no++;

        // UTF-8 BOM support.
        if (line_no == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }

        line = rmap_trim(std::move(line));
        if (line.empty()) continue;

        // Comment with backward compat.
        if (line[0] == '#') {
            auto body = line.substr(1);
            body = rmap_trim(std::move(body));
            auto idx = body.find(" (instances:");
            if (idx != std::string::npos && idx > 0) {
                auto old_name = body.substr(0, idx);
                old_name = rmap_trim(std::move(old_name));
                if (!old_name.empty()) {
                    auto key = rmap_norm_path(old_name);
                    if (rm.exact.find(key) == rm.exact.end()) {
                        rm.exact[key] = "unmatched";
                        rm.exact[rmap_strip_p3d(key)] = "unmatched";
                        rm.entries.push_back({old_name, "unmatched"});
                    }
                }
            }
            continue;
        }

        auto tab = line.find('\t');
        if (tab == std::string::npos) {
            throw std::runtime_error(
                std::format("{}:{}: expected tab-separated old<TAB>new, got: {}", path, line_no, line));
        }

        auto old_name = rmap_trim(line.substr(0, tab));
        auto new_name = rmap_trim(line.substr(tab + 1));
        if (old_name.empty() || new_name.empty()) {
            throw std::runtime_error(
                std::format("{}:{}: empty old or new model name", path, line_no));
        }

        rm.add_entry(old_name, new_name);
    }

    return rm;
}
