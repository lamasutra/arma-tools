#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::unordered_map<std::string, std::string> parse_object_type_blocks(const std::string& yaml) {
    std::unordered_map<std::string, std::string> blocks;

    std::istringstream in(yaml);
    std::string line;
    std::string current_type;
    std::ostringstream current_block;

    auto flush_block = [&]() {
        if (!current_type.empty()) {
            blocks[current_type] = current_block.str();
        }
        current_type.clear();
        current_block.str("");
        current_block.clear();
    };

    while (std::getline(in, line)) {
        if (line.rfind("  ", 0) == 0 && line.find(':') != std::string::npos &&
            line.find("    ") != 0 && line.find("  - ") != 0) {
            const auto key = line.substr(2, line.find(':') - 2);
            const bool all_caps_or_uscore = !key.empty() &&
                key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos;

            if (all_caps_or_uscore && key != "SOURCES") {
                flush_block();
                current_type = key;
                continue;
            }
        }

        if (!current_type.empty()) {
            current_block << line << '\n';
        }
    }

    flush_block();
    return blocks;
}

} // namespace

TEST(spec_validation, files_exist_and_non_empty) {
    const fs::path root = ARMATOOLS_SOURCE_DIR;
    const std::vector<fs::path> required = {
        root / "spec/lod_catalog.yaml",
        root / "spec/object_types.yaml",
        root / "spec/sources.md",
        root / "docs/lods.md",
        root / "docs/object-types.md",
        root / "docs/validator-rules.md",
    };

    for (const auto& path : required) {
        EXPECT_TRUE(fs::exists(path)) << path;
        auto text = read_text(path);
        EXPECT_FALSE(text.empty()) << path;
    }
}

TEST(spec_validation, lod_catalog_contains_core_ids) {
    const fs::path root = ARMATOOLS_SOURCE_DIR;
    const auto catalog = read_text(root / "spec/lod_catalog.yaml");

    const std::vector<std::string> required_ids = {
        "id: VISUAL_RESOLUTION",
        "id: SHADOW_VOLUME",
        "id: GEOMETRY",
        "id: FIRE_GEOMETRY",
        "id: VIEW_GEOMETRY",
        "id: MEMORY",
        "id: LANDCONTACT",
        "id: ROADWAY",
        "id: PATH",
        "id: PHYSX",
        "id: HITPOINTS",
    };

    for (const auto& id : required_ids) {
        EXPECT_NE(catalog.find(id), std::string::npos) << id;
    }
}

TEST(spec_validation, object_types_have_required_sections) {
    const fs::path root = ARMATOOLS_SOURCE_DIR;
    const auto object_types = read_text(root / "spec/object_types.yaml");
    ASSERT_FALSE(object_types.empty());
    EXPECT_EQ(object_types.find("TODO"), std::string::npos);

    const auto blocks = parse_object_type_blocks(object_types);

    const std::vector<std::string> expected_types = {
        "STATIC_PROP",
        "BUILDING",
        "VEHICLE_CAR",
        "VEHICLE_TANK",
        "VEHICLE_AIR",
        "VEHICLE_SHIP",
        "STATIC_WEAPON",
        "HANDHELD_WEAPON",
        "CHARACTER_MAN",
        "VEGETATION_TREE",
        "VEGETATION_CLUTTER",
        "PROXY_MODEL",
        "SIMPLEOBJECT_OPTIMIZED",
    };

    for (const auto& type : expected_types) {
        auto it = blocks.find(type);
        ASSERT_NE(it, blocks.end()) << type;
        const auto& block = it->second;
        EXPECT_NE(block.find("validation_profile:"), std::string::npos) << type;
        EXPECT_NE(block.find("capabilities:"), std::string::npos) << type;
        EXPECT_NE(block.find("required_lods:"), std::string::npos) << type;
        EXPECT_NE(block.find("optional_lods:"), std::string::npos) << type;
        EXPECT_NE(block.find("conditional_lods:"), std::string::npos) << type;
        EXPECT_NE(block.find("required_selections:"), std::string::npos) << type;
        EXPECT_NE(block.find("required_memory_points:"), std::string::npos) << type;
        EXPECT_NE(block.find("required_named_properties:"), std::string::npos) << type;
        EXPECT_NE(block.find("autofix_suggestions:"), std::string::npos) << type;
        EXPECT_NE(block.find("sources:"), std::string::npos) << type;
    }
}

TEST(spec_validation, sources_include_bi_wiki_references) {
    const fs::path root = ARMATOOLS_SOURCE_DIR;
    const auto sources = read_text(root / "spec/sources.md");
    ASSERT_FALSE(sources.empty());
    EXPECT_NE(sources.find("community.bistudio.com/wiki"), std::string::npos);
    EXPECT_NE(sources.find("CARS_CFG"), std::string::npos);
    EXPECT_NE(sources.find("TANKS_CFG"), std::string::npos);
    EXPECT_NE(sources.find("SHIPS_CFG"), std::string::npos);
    EXPECT_NE(sources.find("CHARS_MODDING"), std::string::npos);
}
