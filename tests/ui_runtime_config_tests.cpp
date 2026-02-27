#include "ui_domain/ui_runtime_config.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value)
        : name_(name), had_original_(false) {
        const char* original = std::getenv(name);
        if (original) {
            had_original_ = true;
            original_value_ = original;
        }
        set(value);
    }

    ~ScopedEnvVar() {
        if (had_original_) {
            set(original_value_);
        } else {
            clear();
        }
    }

private:
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void clear() {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    bool had_original_;
    std::string original_value_;
};

fs::path unique_test_root() {
    const auto base = fs::temp_directory_path() / "arma-tools-ui-runtime-config-tests";
    const auto unique = base / std::to_string(static_cast<unsigned long long>(std::rand()));
    fs::create_directories(unique);
    return unique;
}

void write_text_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open());
    out << text;
}

}  // namespace

TEST(UiRuntimeConfigTests, RuntimeConfigPathUsesEnvironmentOverrideWhenSet) {
    const auto root = unique_test_root();
    const auto config_path = root / "custom-ui.json";
    ScopedEnvVar env("ARMA_TOOLS_UI_CONFIG", config_path.string());

    EXPECT_EQ(ui_domain::runtime_config_path(), config_path);
}

TEST(UiRuntimeConfigTests, SaveAndLoadRoundTripsNestedUiSchema) {
    const auto root = unique_test_root();
    const auto config_path = root / "ui.json";
    ScopedEnvVar env("ARMA_TOOLS_UI_CONFIG", config_path.string());

    ui_domain::RuntimeConfig to_save;
    to_save.preferred = "GTK";
    to_save.imgui_overlay_enabled = false;
    to_save.imgui_docking_enabled = false;
    to_save.scale = 1.5f;

    ASSERT_TRUE(ui_domain::save_runtime_config(to_save));
    ASSERT_TRUE(fs::exists(config_path));

    std::ifstream in(config_path);
    ASSERT_TRUE(in.is_open());
    const json parsed = json::parse(in);
    ASSERT_TRUE(parsed.contains("ui"));
    ASSERT_TRUE(parsed.at("ui").is_object());
    EXPECT_EQ(parsed.at("ui").at("preferred").get<std::string>(), "gtk");
    EXPECT_EQ(parsed.at("ui").at("imgui_overlay").get<bool>(), false);
    EXPECT_EQ(parsed.at("ui").at("imgui_docking").get<bool>(), false);
    EXPECT_FLOAT_EQ(parsed.at("ui").at("scale").get<float>(), 1.5f);

    const auto loaded = ui_domain::load_runtime_config();
    EXPECT_EQ(loaded.preferred, "gtk");
    EXPECT_EQ(loaded.imgui_overlay_enabled, false);
    EXPECT_EQ(loaded.imgui_docking_enabled, false);
    EXPECT_FLOAT_EQ(loaded.scale, 1.5f);
}

TEST(UiRuntimeConfigTests, LoadSupportsFlatLegacySchemaAndAliasKeys) {
    const auto root = unique_test_root();
    const auto config_path = root / "ui-legacy.json";
    ScopedEnvVar env("ARMA_TOOLS_UI_CONFIG", config_path.string());

    write_text_file(
        config_path,
        R"({
  "preferred": "IMGUI",
  "imgui_overlay_enabled": false,
  "imgui_docking_enabled": false,
  "scale": 2.0
})");

    const auto loaded = ui_domain::load_runtime_config();
    EXPECT_EQ(loaded.preferred, "imgui");
    EXPECT_EQ(loaded.imgui_overlay_enabled, false);
    EXPECT_EQ(loaded.imgui_docking_enabled, false);
    EXPECT_FLOAT_EQ(loaded.scale, 2.0f);
}

TEST(UiRuntimeConfigTests, InvalidJsonFallsBackToDefaults) {
    const auto root = unique_test_root();
    const auto config_path = root / "broken-ui.json";
    ScopedEnvVar env("ARMA_TOOLS_UI_CONFIG", config_path.string());

    write_text_file(config_path, "{ this is not valid json ");

    const auto loaded = ui_domain::load_runtime_config();
    EXPECT_EQ(loaded.preferred, "auto");
    EXPECT_EQ(loaded.imgui_overlay_enabled, true);
    EXPECT_EQ(loaded.imgui_docking_enabled, true);
    EXPECT_FLOAT_EQ(loaded.scale, 1.0f);
}

TEST(UiRuntimeConfigTests, InvalidScaleIsIgnored) {
    const auto root = unique_test_root();
    const auto config_path = root / "invalid-scale-ui.json";
    ScopedEnvVar env("ARMA_TOOLS_UI_CONFIG", config_path.string());

    write_text_file(
        config_path,
        R"({
  "ui": {
    "preferred": "gtk",
    "imgui_overlay": true,
    "imgui_docking": true,
    "scale": -3.0
  }
})");

    const auto loaded = ui_domain::load_runtime_config();
    EXPECT_EQ(loaded.preferred, "gtk");
    EXPECT_EQ(loaded.imgui_overlay_enabled, true);
    EXPECT_EQ(loaded.imgui_docking_enabled, true);
    EXPECT_FLOAT_EQ(loaded.scale, 1.0f);
}
