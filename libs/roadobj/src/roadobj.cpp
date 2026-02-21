#include "armatools/roadobj.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace armatools::roadobj {

std::string base_name(const std::string& model_name) {
    std::string s = model_name;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto pos = s.find_last_of("\\/");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    auto dot = s.rfind(".p3d");
    if (dot != std::string::npos && dot + 4 == s.size()) s = s.substr(0, dot);
    return s;
}

static bool is_road_suffix(const std::string& s) {
    if (s.empty()) return false;

    // Dead-end: "6konec"
    auto konec_pos = s.rfind("konec");
    if (konec_pos != std::string::npos && konec_pos + 5 == s.size()) {
        double v;
        auto num_part = s.substr(0, konec_pos);
        auto [ptr, ec] = std::from_chars(num_part.data(), num_part.data() + num_part.size(), v);
        return ec == std::errc{} && ptr == num_part.data() + num_part.size();
    }

    // Curve: "10 100"
    auto space = s.find(' ');
    if (space != std::string::npos && space > 0) {
        double v1, v2;
        auto [p1, e1] = std::from_chars(s.data(), s.data() + space, v1);
        auto rest = s.data() + space + 1;
        auto [p2, e2] = std::from_chars(rest, s.data() + s.size(), v2);
        return e1 == std::errc{} && p1 == s.data() + space &&
               e2 == std::errc{} && p2 == s.data() + s.size();
    }

    // Straight: "25"
    double v;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

std::optional<std::string> RoadMap::classify(const std::string& model_name) const {
    std::string base = base_name(model_name);
    for (const auto& r : rules_) {
        if (r.match(base)) return r.road_type;
    }
    return std::nullopt;
}

bool RoadMap::is_road(const std::string& model_name) const {
    return classify(model_name).has_value();
}

std::vector<std::string> RoadMap::types() const {
    std::set<std::string> seen;
    for (const auto& r : rules_) seen.insert(r.road_type);
    return {seen.begin(), seen.end()};
}

void RoadMap::add_rule(const std::string& road_type, std::function<bool(const std::string&)> match) {
    rules_.push_back({road_type, std::move(match)});
}

struct PrefixDef {
    std::string prefix;
    std::string road_type;
};

static const PrefixDef ofp_prefixes[] = {
    {"asfaltka", "Road"}, {"asfatlka", "Road"}, {"silnice", "MainRoad"},
    {"cesta", "Track"}, {"asf", "Road"}, {"sil", "MainRoad"},
    {"ces", "Track"}, {"kos", "Track"},
};

RoadMap default_map() {
    RoadMap m;

    m.add_rule("Road", [](const std::string& base) { return base.starts_with("kr_"); });
    m.add_rule("Road", [](const std::string& base) {
        return base == "nam_okruzi" || base == "nam_dlazba";
    });

    for (const auto& p : ofp_prefixes) {
        auto prefix = p.prefix;
        auto road_type = p.road_type;
        m.add_rule(road_type, [prefix](const std::string& base) {
            if (!base.starts_with(prefix)) return false;
            return is_road_suffix(base.substr(prefix.size()));
        });
    }

    return m;
}

RoadMap load_map(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("roadobj: cannot open " + path);

    RoadMap m;
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        line_no++;
        // Trim
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        auto tab = line.find('\t');
        if (tab == std::string::npos)
            throw std::runtime_error("roadobj: line " + std::to_string(line_no) + ": expected pattern<TAB>RoadType");

        std::string pattern = line.substr(0, tab);
        std::string road_type = line.substr(tab + 1);
        // Trim both
        while (!pattern.empty() && pattern.back() == ' ') pattern.pop_back();
        while (!road_type.empty() && road_type.front() == ' ') road_type.erase(0, 1);

        std::transform(pattern.begin(), pattern.end(), pattern.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (pattern.empty() || road_type.empty())
            throw std::runtime_error("roadobj: line " + std::to_string(line_no) + ": empty pattern or road type");

        if (pattern.back() == '*') {
            auto prefix = pattern.substr(0, pattern.size() - 1);
            m.add_rule(road_type, [prefix](const std::string& base) { return base.starts_with(prefix); });
        } else {
            m.add_rule(road_type, [pattern](const std::string& base) { return base == pattern; });
        }
    }

    return m;
}

static const RoadMap& get_default_map() {
    static const RoadMap dm = default_map();
    return dm;
}

bool is_road(const std::string& model_name) {
    return get_default_map().is_road(model_name);
}

} // namespace armatools::roadobj
