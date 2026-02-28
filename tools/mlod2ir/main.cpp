#include "armatools/p3d.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/cli_logger.h"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

struct ConditionalRequirement {
    std::string key;
    std::vector<std::string> require;
};

struct AutofixSuggestion {
    std::string when_missing_lod;
    std::string hook;
};

struct ObjectTypeSpec {
    std::string description;
    std::string validation_profile;
    std::vector<std::string> required_lods;
    std::vector<std::string> optional_lods;
    std::vector<ConditionalRequirement> conditional_lods;
    std::vector<std::string> required_selections;
    std::vector<std::string> required_memory_points;
    std::vector<std::string> required_named_properties;
    std::vector<AutofixSuggestion> autofix_suggestions;
};

struct ValidationFlags {
    bool is_enterable = false;
    bool has_walkable_surfaces = false;
    bool supports_ai_pathing = false;
    bool has_damage_zones = false;
    bool has_driver_view = false;
    bool has_cargo_view = false;
    bool has_gunner_view = false;
    bool has_commander_view = false;
};

struct ValidationIssue {
    std::string severity;
    std::string rule_id;
    std::string message;
    std::string lod_id;
};

std::string to_upper_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string canonical_token(std::string value) {
    std::string out;
    out.reserve(value.size());
    bool prev_underscore = false;

    for (char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            prev_underscore = false;
        } else {
            if (!prev_underscore) {
                out.push_back('_');
                prev_underscore = true;
            }
        }
    }

    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

bool is_vehicle_like(const std::string& object_type) {
    return object_type.rfind("VEHICLE_", 0) == 0 || object_type == "STATIC_WEAPON";
}

std::unordered_map<std::string, bool> to_flag_map(const ValidationFlags& flags) {
    return {
        {"is_enterable", flags.is_enterable},
        {"has_walkable_surfaces", flags.has_walkable_surfaces},
        {"supports_ai_pathing", flags.supports_ai_pathing},
        {"has_damage_zones", flags.has_damage_zones},
        {"has_driver_view", flags.has_driver_view},
        {"has_cargo_view", flags.has_cargo_view},
        {"has_gunner_view", flags.has_gunner_view},
        {"has_commander_view", flags.has_commander_view},
    };
}

bool has_face_and_vertex_content(const armatools::p3d::LOD& lod) {
    return lod.vertex_count > 0 && lod.face_count > 0;
}

bool has_point_content(const armatools::p3d::LOD& lod) {
    return lod.vertex_count > 0;
}

bool lod_has_minimum_content(const std::string& lod_id, const armatools::p3d::LOD& lod) {
    if (lod_id == "MEMORY" || lod_id == "LANDCONTACT") return has_point_content(lod);
    if (lod_id == "PATH") return has_point_content(lod) || lod.face_count > 0;
    return has_face_and_vertex_content(lod);
}

bool contains_regex_match(const std::set<std::string>& names,
                          const std::string& expr,
                          bool canonicalize) {
    try {
        std::regex re(expr, std::regex::ECMAScript | std::regex::icase);
        for (const auto& n : names) {
            const auto candidate = canonicalize ? canonical_token(n) : n;
            if (std::regex_match(candidate, re)) return true;
        }
    } catch (const std::regex_error&) {
        return false;
    }
    return false;
}

bool requirement_matched(const std::set<std::string>& names,
                         const std::string& requirement,
                         bool canonicalize) {
    if (requirement.rfind("re:", 0) == 0) {
        return contains_regex_match(names, requirement.substr(3), canonicalize);
    }

    const auto needle = canonicalize ? canonical_token(requirement) : to_lower_ascii(requirement);
    for (const auto& name : names) {
        const auto candidate = canonicalize ? canonical_token(name) : to_lower_ascii(name);
        if (candidate == needle) return true;
    }
    return false;
}

std::string classify_lod_id(const armatools::p3d::LOD& lod) {
    const auto& name = lod.resolution_name;

    if (name == "Geometry") return "GEOMETRY";
    if (name == "Memory") return "MEMORY";
    if (name == "LandContact") return "LANDCONTACT";
    if (name == "Roadway") return "ROADWAY";
    if (name == "Paths") return "PATH";
    if (name == "HitPoints") return "HITPOINTS";
    if (name == "ViewGeometry") return "VIEW_GEOMETRY";
    if (name == "FireGeometry") return "FIRE_GEOMETRY";
    if (name == "ViewCargoGeometry") return "VIEW_CARGO_GEOMETRY";
    if (name == "ViewCargoFireGeometry") return "VIEW_CARGO_FIRE_GEOMETRY";
    if (name == "ViewCommander") return "VIEW_COMMANDER";
    if (name == "ViewCommanderGeometry") return "VIEW_COMMANDER_GEOMETRY";
    if (name == "ViewCommanderFireGeometry") return "VIEW_COMMANDER_FIRE_GEOMETRY";
    if (name == "ViewPilotGeometry") return "VIEW_PILOT_GEOMETRY";
    if (name == "ViewPilotFireGeometry") return "VIEW_PILOT_FIRE_GEOMETRY";
    if (name == "ViewGunnerGeometry") return "VIEW_GUNNER_GEOMETRY";
    if (name == "ViewGunnerFireGeometry") return "VIEW_GUNNER_FIRE_GEOMETRY";
    if (name == "PhysX") return "PHYSX";
    if (name == "Buoyancy") return "BUOYANCY";
    if (name == "Wreck") return "WRECK";

    if (name.rfind("ShadowVolume", 0) == 0) return "SHADOW_VOLUME";

    char* end = nullptr;
    const auto resolution = std::strtof(name.c_str(), &end);
    if (end != nullptr && *end == '\0') {
        if (resolution < 1.0e4f) return "VISUAL_RESOLUTION";
        if (resolution >= 1.0e4f && resolution < 2.0e4f) return "SHADOW_VOLUME";
    }

    return "UNKNOWN";
}

std::unordered_map<std::string, ObjectTypeSpec> build_object_type_specs() {
    using CR = ConditionalRequirement;
    using AF = AutofixSuggestion;

    return {
        {"STATIC_PROP",
         ObjectTypeSpec{
             .description = "Small static prop.",
             .validation_profile = "relaxed",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY"},
             .optional_lods = {"VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "HITPOINTS"},
             .conditional_lods = {},
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
             },
         }},

        {"BUILDING",
         ObjectTypeSpec{
             .description = "Building model profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY"},
             .optional_lods = {"LANDCONTACT", "ROADWAY", "PATH", "HITPOINTS"},
             .conditional_lods = {
                 CR{"is_enterable", {"ROADWAY"}},
                 CR{"supports_ai_pathing", {"PATH"}},
                 CR{"has_walkable_surfaces", {"ROADWAY"}},
             },
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"ROADWAY", "ensure_roadway_from_visual_planes"},
                 AF{"PATH", "ensure_path_lod_from_roadway"},
                 AF{"MEMORY", "ensure_memory_points_from_bbox"},
             },
         }},

        {"VEHICLE_CAR",
         ObjectTypeSpec{
             .description = "Cars profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "PHYSX"},
             .optional_lods = {"HITPOINTS", "VIEW_PILOT_GEOMETRY", "VIEW_CARGO_GEOMETRY", "VIEW_GUNNER_GEOMETRY", "WRECK"},
             .conditional_lods = {
                 CR{"has_driver_view", {"VIEW_PILOT_GEOMETRY"}},
                 CR{"has_cargo_view", {"VIEW_CARGO_GEOMETRY"}},
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"re:^component[0-9]{2,4}$"},
             .required_memory_points = {"pos_driver", "pos_driver_dir", "wheel_1_1_axis", "wheel_1_1_bound"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"PHYSX", "ensure_physx_from_geometry_components"},
                 AF{"LANDCONTACT", "ensure_landcontact_from_wheels_or_bbox"},
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"HITPOINTS", "ensure_hitpoints_from_named_selections"},
             },
         }},

        {"VEHICLE_TANK",
         ObjectTypeSpec{
             .description = "Tanks profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "PHYSX"},
             .optional_lods = {"HITPOINTS", "VIEW_PILOT_GEOMETRY", "VIEW_GUNNER_GEOMETRY", "VIEW_COMMANDER_GEOMETRY", "WRECK"},
             .conditional_lods = {
                 CR{"has_driver_view", {"VIEW_PILOT_GEOMETRY"}},
                 CR{"has_gunner_view", {"VIEW_GUNNER_GEOMETRY"}},
                 CR{"has_commander_view", {"VIEW_COMMANDER_GEOMETRY"}},
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"re:^component[0-9]{2,4}$", "turret", "gun"},
             .required_memory_points = {"pos_driver", "pos_driver_dir", "gun_axis", "gun_begin", "gun_end"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"PHYSX", "ensure_physx_from_geometry_components"},
                 AF{"LANDCONTACT", "ensure_landcontact_from_tracks_or_bbox"},
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"HITPOINTS", "ensure_hitpoints_from_named_selections"},
             },
         }},

        {"VEHICLE_AIR",
         ObjectTypeSpec{
             .description = "Aircraft profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "PHYSX"},
             .optional_lods = {"HITPOINTS", "VIEW_PILOT_GEOMETRY", "VIEW_GUNNER_GEOMETRY", "VIEW_CARGO_GEOMETRY", "WRECK"},
             .conditional_lods = {
                 CR{"has_driver_view", {"VIEW_PILOT_GEOMETRY"}},
                 CR{"has_gunner_view", {"VIEW_GUNNER_GEOMETRY"}},
                 CR{"has_cargo_view", {"VIEW_CARGO_GEOMETRY"}},
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"re:^component[0-9]{2,4}$"},
             .required_memory_points = {"pos_pilot", "pos_pilot_dir"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"PHYSX", "ensure_physx_from_geometry_components"},
                 AF{"LANDCONTACT", "ensure_landcontact_from_gear_points"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"HITPOINTS", "ensure_hitpoints_from_named_selections"},
             },
         }},

        {"VEHICLE_SHIP",
         ObjectTypeSpec{
             .description = "Ship profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "PHYSX"},
             .optional_lods = {"BUOYANCY", "ROADWAY", "PATH", "HITPOINTS", "VIEW_PILOT_GEOMETRY", "VIEW_GUNNER_GEOMETRY", "VIEW_CARGO_GEOMETRY", "WRECK"},
             .conditional_lods = {
                 CR{"has_walkable_surfaces", {"ROADWAY"}},
                 CR{"supports_ai_pathing", {"PATH"}},
                 CR{"has_driver_view", {"VIEW_PILOT_GEOMETRY"}},
                 CR{"has_gunner_view", {"VIEW_GUNNER_GEOMETRY"}},
                 CR{"has_cargo_view", {"VIEW_CARGO_GEOMETRY"}},
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"re:^component[0-9]{2,4}$"},
             .required_memory_points = {"pos_driver", "pos_driver_dir"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"PHYSX", "ensure_physx_from_geometry_components"},
                 AF{"BUOYANCY", "ensure_buoyancy_from_hull"},
                 AF{"ROADWAY", "ensure_roadway_from_visual_deck"},
                 AF{"PATH", "ensure_path_lod_from_roadway"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"HITPOINTS", "ensure_hitpoints_from_named_selections"},
             },
         }},

        {"STATIC_WEAPON",
         ObjectTypeSpec{
             .description = "Static weapon profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT"},
             .optional_lods = {"HITPOINTS", "VIEW_GUNNER_GEOMETRY", "WRECK"},
             .conditional_lods = {
                 CR{"has_gunner_view", {"VIEW_GUNNER_GEOMETRY"}},
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"turret", "gun", "re:^component[0-9]{2,4}$"},
             .required_memory_points = {"gun_axis", "gun_begin", "gun_end", "pos_gunner"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"LANDCONTACT", "ensure_landcontact_from_bbox"},
             },
         }},

        {"HANDHELD_WEAPON",
         ObjectTypeSpec{
             .description = "Handheld weapon profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "MEMORY"},
             .optional_lods = {"VIEW_GEOMETRY", "HITPOINTS"},
             .conditional_lods = {},
             .required_selections = {},
             .required_memory_points = {"eye", "usti_hlavne", "konec_hlavne", "nabojnicestart", "nabojniceend"},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"MEMORY", "ensure_weapon_memory_points_from_axis_estimation"},
             },
         }},

        {"CHARACTER_MAN",
         ObjectTypeSpec{
             .description = "Character profile.",
             .validation_profile = "strict",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT", "ROADWAY", "PATH"},
             .optional_lods = {"HITPOINTS"},
             .conditional_lods = {
                 CR{"has_damage_zones", {"HITPOINTS"}},
             },
             .required_selections = {"pelvis", "spine", "head"},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
                 AF{"LANDCONTACT", "ensure_landcontact_from_feet"},
                 AF{"ROADWAY", "ensure_roadway_from_footprint"},
                 AF{"PATH", "ensure_path_lod_from_roadway"},
             },
         }},

        {"VEGETATION_TREE",
         ObjectTypeSpec{
             .description = "Tree vegetation profile.",
             .validation_profile = "relaxed",
             .required_lods = {"VISUAL_RESOLUTION", "SHADOW_VOLUME"},
             .optional_lods = {"GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY", "LANDCONTACT"},
             .conditional_lods = {},
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
             },
         }},

        {"VEGETATION_CLUTTER",
         ObjectTypeSpec{
             .description = "Clutter vegetation profile.",
             .validation_profile = "relaxed",
             .required_lods = {"VISUAL_RESOLUTION"},
             .optional_lods = {"SHADOW_VOLUME"},
             .conditional_lods = {},
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
             },
         }},

        {"PROXY_MODEL",
         ObjectTypeSpec{
             .description = "Proxy helper profile.",
             .validation_profile = "relaxed",
             .required_lods = {"VISUAL_RESOLUTION"},
             .optional_lods = {"MEMORY"},
             .conditional_lods = {},
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"VISUAL_RESOLUTION", "fail_manual_authoring_required"},
             },
         }},

        {"SIMPLEOBJECT_OPTIMIZED",
         ObjectTypeSpec{
             .description = "SimpleObject-optimized profile.",
             .validation_profile = "relaxed",
             .required_lods = {"VISUAL_RESOLUTION"},
             .optional_lods = {"SHADOW_VOLUME", "GEOMETRY", "FIRE_GEOMETRY", "VIEW_GEOMETRY", "MEMORY"},
             .conditional_lods = {
                 CR{"has_walkable_surfaces", {"ROADWAY"}},
                 CR{"supports_ai_pathing", {"PATH"}},
             },
             .required_selections = {},
             .required_memory_points = {},
             .required_named_properties = {},
             .autofix_suggestions = {
                 AF{"SHADOW_VOLUME", "ensure_shadow_volume_from_visual"},
                 AF{"GEOMETRY", "ensure_geometry_from_visual_convex_decompose"},
                 AF{"FIRE_GEOMETRY", "ensure_fire_geometry_from_geometry"},
                 AF{"VIEW_GEOMETRY", "ensure_view_geometry_from_geometry"},
             },
         }},
    };
}

void print_usage() {
    armatools::cli::print("Usage: mlod2ir [flags] <input.p3d>");
    armatools::cli::print("Converts a P3D model to normalized IR JSON and validates LOD requirements.");
    armatools::cli::print("");
    armatools::cli::print("Flags:");
    armatools::cli::print("  --object-type <TYPE>  Object type profile (default: STATIC_PROP)");
    armatools::cli::print("  --list-object-types   Print supported object type IDs");
    armatools::cli::print("  --output <path>       Output JSON path (default: <input>_mlod2ir/model_ir.json)");
    armatools::cli::print("  --json                Write JSON to stdout");
    armatools::cli::print("  --pretty              Pretty-print JSON output");
    armatools::cli::print("  --validate-only       Keep full validation output but skip large optional sections");
    armatools::cli::print("  --enterable           Enable enterable conditional LOD checks");
    armatools::cli::print("  --walkable            Enable walkable conditional LOD checks");
    armatools::cli::print("  --ai-pathing          Enable AI pathing conditional LOD checks");
    armatools::cli::print("  --damage-zones        Enable hitpoint conditional LOD checks");
    armatools::cli::print("  --driver-view         Enable driver/pilot view conditional checks");
    armatools::cli::print("  --cargo-view          Enable cargo view conditional checks");
    armatools::cli::print("  --gunner-view         Enable gunner view conditional checks");
    armatools::cli::print("  --commander-view      Enable commander view conditional checks");
    armatools::cli::print("  -v, --verbose         Verbose logging");
    armatools::cli::print("  -vv, --debug          Debug logging");
    armatools::cli::print("  -h, --help            Show help");
}

json issue_to_json(const ValidationIssue& issue) {
    json out = {
        {"severity", issue.severity},
        {"ruleId", issue.rule_id},
        {"message", issue.message},
    };
    if (!issue.lod_id.empty()) out["lodId"] = issue.lod_id;
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    auto specs = build_object_type_specs();

    std::string object_type = "STATIC_PROP";
    std::string output_path;
    bool json_stdout = false;
    bool pretty = false;
    bool validate_only = false;
    bool list_object_types = false;
    int verbosity = 0;
    ValidationFlags flags;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--object-type") == 0) {
            if (i + 1 >= argc) {
                LOGE("missing value for --object-type");
                return 1;
            }
            object_type = to_upper_ascii(argv[++i]);
        } else if (std::strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                LOGE("missing value for --output");
                return 1;
            }
            output_path = argv[++i];
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_stdout = true;
        } else if (std::strcmp(argv[i], "--pretty") == 0) {
            pretty = true;
        } else if (std::strcmp(argv[i], "--validate-only") == 0) {
            validate_only = true;
        } else if (std::strcmp(argv[i], "--list-object-types") == 0) {
            list_object_types = true;
        } else if (std::strcmp(argv[i], "--enterable") == 0) {
            flags.is_enterable = true;
        } else if (std::strcmp(argv[i], "--walkable") == 0) {
            flags.has_walkable_surfaces = true;
        } else if (std::strcmp(argv[i], "--ai-pathing") == 0) {
            flags.supports_ai_pathing = true;
        } else if (std::strcmp(argv[i], "--damage-zones") == 0) {
            flags.has_damage_zones = true;
        } else if (std::strcmp(argv[i], "--driver-view") == 0) {
            flags.has_driver_view = true;
        } else if (std::strcmp(argv[i], "--cargo-view") == 0) {
            flags.has_cargo_view = true;
        } else if (std::strcmp(argv[i], "--gunner-view") == 0) {
            flags.has_gunner_view = true;
        } else if (std::strcmp(argv[i], "--commander-view") == 0) {
            flags.has_commander_view = true;
        } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            verbosity = std::min(verbosity + 1, 2);
        } else if (std::strcmp(argv[i], "-vv") == 0 || std::strcmp(argv[i], "--debug") == 0) {
            verbosity = 2;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            positional.push_back(argv[i]);
        }
    }

    armatools::cli::set_verbosity(verbosity);

    if (list_object_types) {
        std::vector<std::string> keys;
        keys.reserve(specs.size());
        for (const auto& [k, _] : specs) {
            keys.push_back(k);
        }
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) {
            armatools::cli::print(k);
        }
        return 0;
    }

    const auto spec_it = specs.find(object_type);
    if (spec_it == specs.end()) {
        LOGE("unknown --object-type", object_type);
        LOGE("use --list-object-types to inspect valid IDs");
        return 1;
    }
    const auto& spec = spec_it->second;

    if (positional.size() != 1) {
        print_usage();
        return 1;
    }

    const auto input = positional.front();

    armatools::p3d::P3DFile model;
    try {
        if (input == "-") {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            std::istringstream in(ss.str(), std::ios::binary);
            model = armatools::p3d::read(in);
        } else {
            std::ifstream f(input, std::ios::binary);
            if (!f.is_open()) {
                LOGE("cannot open input", input);
                return 1;
            }
            model = armatools::p3d::read(f);
        }
    } catch (const std::exception& e) {
        LOGE("parse failed:", e.what());
        return 1;
    }

    std::unordered_map<std::string, std::vector<const armatools::p3d::LOD*>> lod_by_id;
    std::set<std::string> all_named_selections;
    std::set<std::string> memory_named_points;

    json lods_json = json::array();

    for (const auto& lod : model.lods) {
        const auto lod_id = classify_lod_id(lod);
        lod_by_id[lod_id].push_back(&lod);

        json lod_json = {
            {"id", "lod_" + std::to_string(lod.index)},
            {"index", lod.index},
            {"resolution", lod.resolution},
            {"resolutionName", lod.resolution_name},
            {"lodId", lod_id},
            {"vertexCount", lod.vertex_count},
            {"faceCount", lod.face_count},
            {"namedSelections", lod.named_selections},
        };

        if (!validate_only) {
            json named_properties = json::array();
            for (const auto& prop : lod.named_properties) {
                named_properties.push_back({{"name", prop.name}, {"value", prop.value}});
            }
            lod_json["namedProperties"] = std::move(named_properties);

            // Roundtrip mesh payload used by ir2mlod.
            json positions = json::array();
            for (const auto& p : lod.vertices) {
                positions.push_back(json::array({p[0], p[1], p[2]}));
            }

            json indices = json::array();
            for (const auto& face : lod.faces) {
                json f = json::array();
                for (const auto idx : face) {
                    f.push_back(idx);
                }
                indices.push_back(std::move(f));
            }

            json mesh = {
                {"positions", std::move(positions)},
                {"indices", std::move(indices)},
            };

            if (lod.normals.size() == lod.vertices.size() && !lod.normals.empty()) {
                json normals = json::array();
                for (const auto& n : lod.normals) {
                    normals.push_back(json::array({n[0], n[1], n[2]}));
                }
                mesh["normals"] = std::move(normals);
            }

            if (!lod.uv_sets.empty() && lod.uv_sets[0].size() == lod.vertices.size()) {
                json uv0 = json::array();
                for (const auto& uv : lod.uv_sets[0]) {
                    uv0.push_back(json::array({uv[0], uv[1]}));
                }
                mesh["uv0"] = std::move(uv0);
            }

            lod_json["mesh"] = std::move(mesh);

            json materials = json::array();
            std::unordered_map<std::string, int> material_to_id;
            std::vector<int> face_material_ids;
            face_material_ids.reserve(lod.face_data.size());

            auto add_material = [&](const std::string& name) -> int {
                if (name.empty()) return -1;
                auto it = material_to_id.find(name);
                if (it != material_to_id.end()) return it->second;
                const auto id = static_cast<int>(materials.size());
                materials.push_back(name);
                material_to_id.emplace(name, id);
                return id;
            };

            for (const auto& face : lod.face_data) {
                auto mat_name = face.material;
                if (mat_name.empty() && !lod.materials.empty()) {
                    mat_name = lod.materials.front();
                }
                face_material_ids.push_back(add_material(mat_name));
            }

            if (materials.empty()) {
                for (const auto& m : lod.materials) {
                    add_material(m);
                }
                if (!materials.empty()) {
                    face_material_ids.assign(lod.faces.size(), 0);
                }
            }

            if (!materials.empty()) {
                lod_json["materials"] = std::move(materials);

                json face_mat_json = json::array();
                for (const auto id : face_material_ids) {
                    face_mat_json.push_back(id < 0 ? 0 : id);
                }
                lod_json["face_material_ids"] = std::move(face_mat_json);
            }

            json named_sel_obj = json::object();
            for (const auto& [name, vertices] : lod.named_selection_vertices) {
                json vv = json::array();
                for (const auto idx : vertices) {
                    vv.push_back(idx);
                }
                named_sel_obj[name] = std::move(vv);
            }
            if (!named_sel_obj.empty()) {
                lod_json["named_selections"] = std::move(named_sel_obj);
            }
        }

        lods_json.push_back(std::move(lod_json));

        for (const auto& sel : lod.named_selections) {
            all_named_selections.insert(sel);
            if (lod_id == "MEMORY") memory_named_points.insert(sel);
        }
    }

    if (memory_named_points.empty()) {
        memory_named_points = all_named_selections;
    }

    auto flag_map = to_flag_map(flags);

    std::set<std::string> required_lods(spec.required_lods.begin(), spec.required_lods.end());
    std::set<std::string> conditional_required;

    for (const auto& cond : spec.conditional_lods) {
        auto it = flag_map.find(cond.key);
        if (it != flag_map.end() && it->second) {
            for (const auto& lod : cond.require) {
                required_lods.insert(lod);
                conditional_required.insert(lod);
            }
        }
    }

    std::vector<ValidationIssue> issues;
    std::vector<std::string> missing_required;
    std::vector<std::string> missing_conditional;

    for (const auto& lod : spec.required_lods) {
        if (!lod_by_id.contains(lod)) {
            missing_required.push_back(lod);
            issues.push_back({
                "error",
                "LOD_REQUIRED_PRESENT",
                "missing required LOD: " + lod,
                lod,
            });
        }
    }

    for (const auto& lod : conditional_required) {
        if (!lod_by_id.contains(lod)) {
            missing_conditional.push_back(lod);
            issues.push_back({
                "error",
                "LOD_CONDITIONAL_PRESENT",
                "missing conditional LOD: " + lod,
                lod,
            });
        }
    }

    for (const auto& lod : required_lods) {
        auto it = lod_by_id.find(lod);
        if (it == lod_by_id.end()) continue;

        bool has_content = false;
        for (const auto* lod_ptr : it->second) {
            if (lod_has_minimum_content(lod, *lod_ptr)) {
                has_content = true;
                break;
            }
        }

        if (!has_content) {
            issues.push_back({
                "error",
                "LOD_REQUIRED_NON_EMPTY",
                "required LOD exists but has no usable content: " + lod,
                lod,
            });
        }
    }

    const bool strict = spec.validation_profile == "strict";
    const auto selection_severity = strict ? "error" : "warning";

    for (const auto& req : spec.required_selections) {
        if (!requirement_matched(all_named_selections, req, true)) {
            issues.push_back({
                selection_severity,
                "SELECTION_REQUIRED",
                "missing required selection: " + req,
                "",
            });
        }
    }

    for (const auto& req : spec.required_memory_points) {
        if (!requirement_matched(memory_named_points, req, true)) {
            issues.push_back({
                selection_severity,
                "MEMORY_POINT_REQUIRED",
                "missing required memory point: " + req,
                "MEMORY",
            });
        }
    }

    if (is_vehicle_like(object_type)) {
        std::set<std::string> geom_component_names;

        for (const auto lod_id : {"GEOMETRY", "PHYSX"}) {
            auto it = lod_by_id.find(lod_id);
            if (it == lod_by_id.end()) continue;
            for (const auto* lod : it->second) {
                for (const auto& name : lod->named_selections) {
                    geom_component_names.insert(name);
                }
            }
        }

        if (!contains_regex_match(geom_component_names, "^component[0-9]{2,4}$", true)) {
            issues.push_back({
                strict ? "error" : "warning",
                "GEOMETRY_COMPONENT_NAMES",
                "geometry/physx LODs are missing componentXX-style selections",
                "GEOMETRY",
            });
        }
    }

    if (model.format != "MLOD") {
        issues.push_back({
            "warning",
            "FORMAT_EXPECTED_MLOD",
            "input format is " + model.format + "; tool is intended for MLOD-first workflows",
            "",
        });
    }

    bool has_error = false;
    bool has_warning = false;
    for (const auto& issue : issues) {
        if (issue.severity == "error") has_error = true;
        if (issue.severity == "warning") has_warning = true;
    }

    std::set<std::string> missing_lods_total;
    missing_lods_total.insert(missing_required.begin(), missing_required.end());
    missing_lods_total.insert(missing_conditional.begin(), missing_conditional.end());

    json autofix_plan = json::array();
    for (const auto& missing : missing_lods_total) {
        bool added = false;
        for (const auto& suggestion : spec.autofix_suggestions) {
            if (suggestion.when_missing_lod == missing) {
                autofix_plan.push_back({
                    {"lodId", missing},
                    {"hook", suggestion.hook},
                });
                added = true;
            }
        }
        if (!added) {
            autofix_plan.push_back({
                {"lodId", missing},
                {"hook", "manual_authoring_required"},
            });
        }
    }

    json lod_counts = json::object();
    for (const auto& [lod_id, lods] : lod_by_id) {
        lod_counts[lod_id] = static_cast<int>(lods.size());
    }

    json flags_json = {
        {"is_enterable", flags.is_enterable},
        {"has_walkable_surfaces", flags.has_walkable_surfaces},
        {"supports_ai_pathing", flags.supports_ai_pathing},
        {"has_damage_zones", flags.has_damage_zones},
        {"has_driver_view", flags.has_driver_view},
        {"has_cargo_view", flags.has_cargo_view},
        {"has_gunner_view", flags.has_gunner_view},
        {"has_commander_view", flags.has_commander_view},
    };

    json issues_json = json::array();
    for (const auto& issue : issues) {
        issues_json.push_back(issue_to_json(issue));
    }

    json out = {
        {"schemaVersion", 1},
        {"tool", "mlod2ir"},
        {"input", input},
        {"format", model.format},
        {"version", model.version},
        {"objectType", object_type},
        {"validationProfile", spec.validation_profile},
        {"description", spec.description},
        {"flags", flags_json},
        {"lodCountsById", lod_counts},
        {"requiredLods", spec.required_lods},
        {"conditionalLodsActivated", std::vector<std::string>(conditional_required.begin(), conditional_required.end())},
        {"missingRequiredLods", missing_required},
        {"missingConditionalLods", missing_conditional},
        {"autofixPlan", autofix_plan},
        {"issues", issues_json},
        {"status", has_error ? "error" : (has_warning ? "warning" : "ok")},
    };

    if (!validate_only) {
        out["lods"] = std::move(lods_json);
    }

    if (json_stdout) {
        if (pretty)
            std::cout << std::setw(2) << out << '\n';
        else
            std::cout << out << '\n';
    } else {
        fs::path out_path;
        if (!output_path.empty()) {
            out_path = fs::path(output_path);
        } else {
            if (input == "-") {
                LOGE("stdin input requires --json or --output");
                return 1;
            }
            fs::path in_path(input);
            auto stem = in_path.stem().string();
            auto dir = in_path.parent_path() / (stem + "_mlod2ir");
            out_path = dir / "model_ir.json";
        }

        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) {
            LOGE("cannot create output directory", out_path.parent_path().string(), ec.message());
            return 1;
        }

        std::ofstream f(out_path);
        if (!f.is_open()) {
            LOGE("cannot write output", out_path.string());
            return 1;
        }

        if (pretty)
            f << std::setw(2) << out << '\n';
        else
            f << out << '\n';

        armatools::cli::log_stdout("wrote", out_path.string());
    }

    return has_error ? 1 : 0;
}
