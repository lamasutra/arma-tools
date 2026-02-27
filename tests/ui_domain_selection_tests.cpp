#include "ui_domain/ui_backend_selection.h"
#include "ui_domain/ui_backend_registry.h"

#include <gtest/gtest.h>

namespace {

ui_backend_probe_result_v1 probe_available_score_90() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 1;
    r.score = 90;
    r.reason = "available";
    return r;
}

ui_backend_probe_result_v1 probe_available_score_80() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 1;
    r.score = 80;
    r.reason = "available";
    return r;
}

ui_backend_probe_result_v1 probe_available_score_10() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 1;
    r.score = 10;
    r.reason = "available";
    return r;
}

ui_backend_probe_result_v1 probe_unavailable() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 0;
    r.score = 0;
    r.reason = "not available";
    return r;
}

int create_noop(const ui_backend_create_desc_v1*, ui_backend_instance_v1*) {
    return UI_STATUS_OK;
}

const ui_backend_factory_v1 k_factory_gtk = {
    UI_ABI_VERSION,
    "gtk",
    "GTK",
    probe_available_score_90,
    create_noop,
};

const ui_backend_factory_v1 k_factory_imgui = {
    UI_ABI_VERSION,
    "imgui",
    "ImGui",
    probe_available_score_80,
    create_noop,
};

const ui_backend_factory_v1 k_factory_null = {
    UI_ABI_VERSION,
    "null",
    "Null",
    probe_available_score_10,
    create_noop,
};

const ui_backend_factory_v1 k_factory_unavailable = {
    UI_ABI_VERSION,
    "imgui",
    "ImGui",
    probe_unavailable,
    create_noop,
};

}  // namespace

TEST(UiDomainSelectionTests, AutoPicksHighestScoreAvailableBackend) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_null, "test:null", false);
    registry.register_factory(&k_factory_imgui, "test:imgui", false);
    registry.register_factory(&k_factory_gtk, "test:gtk", false);

    ui_domain::SelectionRequest request;
    request.config_backend = "auto";

    const auto result = ui_domain::select_backend(registry, request);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_backend, "gtk");
}

TEST(UiDomainSelectionTests, ExplicitBackendFailsWhenUnavailable) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_unavailable, "test:imgui", false);

    ui_domain::SelectionRequest request;
    request.config_backend = "imgui";

    const auto result = ui_domain::select_backend(registry, request);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.used_explicit_request);
    EXPECT_NE(result.message.find("unavailable"), std::string::npos);
}

TEST(UiDomainSelectionTests, EnvOverrideWinsOverConfig) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_gtk, "test:gtk", false);
    registry.register_factory(&k_factory_imgui, "test:imgui", false);

    ui_domain::SelectionRequest request;
    request.config_backend = "gtk";
    request.has_env_override = true;
    request.env_backend = "imgui";

    const auto result = ui_domain::select_backend(registry, request);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_backend, "imgui");
    EXPECT_EQ(result.selection_source, "env");
}

TEST(UiDomainSelectionTests, CliOverrideWinsOverEnvAndConfig) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_gtk, "test:gtk", false);
    registry.register_factory(&k_factory_imgui, "test:imgui", false);
    registry.register_factory(&k_factory_null, "test:null", false);

    ui_domain::SelectionRequest request;
    request.config_backend = "gtk";
    request.has_env_override = true;
    request.env_backend = "null";
    request.has_cli_override = true;
    request.cli_backend = "imgui";

    const auto result = ui_domain::select_backend(registry, request);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_backend, "imgui");
    EXPECT_EQ(result.selection_source, "cli");
}
