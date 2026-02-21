#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace armatools::roadobj {

// RoadMap classifies model names as road types.
class RoadMap {
public:
    // classify returns the road type for a model, or nullopt if not a road.
    std::optional<std::string> classify(const std::string& model_name) const;

    // is_road returns true if the model matches any road pattern.
    bool is_road(const std::string& model_name) const;

    // types returns sorted unique road type names.
    std::vector<std::string> types() const;

    // Add a rule with a custom match function.
    void add_rule(const std::string& road_type, std::function<bool(const std::string&)> match);

private:
    struct Rule {
        std::string road_type;
        std::function<bool(const std::string&)> match;
    };
    std::vector<Rule> rules_;
};

// default_map returns the built-in OFP road detection rules.
RoadMap default_map();

// load_map reads road patterns from a TSV file.
RoadMap load_map(const std::string& path);

// base_name extracts lowercased filename without extension from a model path.
std::string base_name(const std::string& model_name);

// is_road checks if a model name is a road using the default OFP map.
bool is_road(const std::string& model_name);

} // namespace armatools::roadobj
