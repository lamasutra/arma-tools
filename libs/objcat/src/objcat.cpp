#include "armatools/objcat.h"
#include "armatools/armapath.h"

#include <string>
#include <string_view>
#include <vector>

namespace armatools::objcat {

struct PrefixRule {
    std::string_view prefix;
    std::string_view category;
};

static constexpr PrefixRule prefix_rules[] = {
    // Vegetation - trees
    {"str_", "vegetation"}, {"str ", "vegetation"},
    {"jablon", "vegetation"}, {"oliva", "vegetation"}, {"sakura", "vegetation"},
    // Vegetation - bushes/shrubs
    {"ker ", "vegetation"}, {"ker_", "vegetation"}, {"krovi", "vegetation"},
    // Vegetation - forest blocks
    {"les_", "vegetation"},
    // Buildings (longer prefixes first)
    {"cihlovej_dum", "buildings"}, {"ryb_domek", "buildings"},
    {"strazni_vez", "buildings"}, {"repair_center", "buildings"},
    {"hruzdum", "buildings"}, {"deutshe", "buildings"},
    {"plechbud", "buildings"}, {"fortress", "buildings"},
    {"budova", "buildings"}, {"stodola", "buildings"},
    {"kostel", "buildings"}, {"kostelik", "buildings"},
    {"statek", "buildings"}, {"hangar", "buildings"},
    {"hlaska", "buildings"}, {"bouda", "buildings"},
    {"garaz", "buildings"}, {"kaple", "buildings"},
    {"kasna", "buildings"}, {"afdum", "buildings"},
    {"dum", "buildings"}, {"vez", "buildings"},
    // Rocks
    {"kopa_kameni", "rocks"}, {"kamen", "rocks"}, {"skala", "rocks"},
    // Walls/fences
    {"pletivo", "walls"}, {"newplot", "walls"}, {"barbedwire", "walls"},
    {"ohrada", "walls"}, {"plutek", "walls"}, {"plot", "walls"},
    // Signs
    {"malden_smer_", "signs"}, {"bozi_muka", "signs"},
    {"znacka", "signs"}, {"majak", "signs"},
    // Military
    {"fuelstation_army", "military"}, {"strel_post", "military"},
    {"jehlan", "military"}, {"jezek", "military"},
    // Props
    {"hromada_beden", "props"}, {"hrobecek", "props"},
    {"lampazel", "props"}, {"paletyc", "props"},
    {"podesta", "props"}, {"stoh", "props"},
    // Infrastructure
    {"obihacka", "infrastructure"}, {"podlejzacka", "infrastructure"},
    {"prebehlavka", "infrastructure"}, {"prolejzacka", "infrastructure"},
    {"molo_", "infrastructure"},
};

static std::string category_from_dir(const std::string& dir) {
    std::vector<std::string> words;
    size_t start = 0;
    while (start < dir.size()) {
        auto slash = dir.find('/', start);
        std::string segment = (slash == std::string::npos)
            ? dir.substr(start) : dir.substr(start, slash - start);
        start = (slash == std::string::npos) ? dir.size() : slash + 1;

        size_t ts = 0;
        while (ts < segment.size()) {
            auto us = segment.find('_', ts);
            std::string tok = (us == std::string::npos)
                ? segment.substr(ts) : segment.substr(ts, us - ts);
            ts = (us == std::string::npos) ? segment.size() : us + 1;
            if (tok.size() > 1) words.push_back(tok);
        }
    }
    if (words.empty()) return "unknown";
    std::string result = words[0];
    for (size_t i = 1; i < words.size(); i++) {
        result += ' ';
        result += words[i];
    }
    return result;
}

std::string category(const std::string& model_path) {
    std::string lower = armapath::to_slash_lower(model_path);
    // Trim .p3d suffix
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".p3d")
        lower.resize(lower.size() - 4);

    auto last_slash = lower.rfind('/');
    if (last_slash != std::string::npos && last_slash > 0)
        return category_from_dir(lower.substr(0, last_slash));

    // No directory - OFP basename prefix rules
    for (const auto& rule : prefix_rules) {
        if (lower.starts_with(rule.prefix))
            return std::string(rule.category);
    }
    return "unknown";
}

} // namespace armatools::objcat
