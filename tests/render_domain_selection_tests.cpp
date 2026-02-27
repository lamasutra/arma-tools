#include "render_domain/rd_backend_selection.h"
#include "render_domain/rd_backend_registry.h"

#include <gtest/gtest.h>

namespace {

rd_backend_probe_result_v1 probe_available_score_30() {
    rd_backend_probe_result_v1 r{};
    r.struct_size = sizeof(rd_backend_probe_result_v1);
    r.available = 1;
    r.score = 30;
    r.reason = "available";
    return r;
}

rd_backend_probe_result_v1 probe_available_score_80() {
    rd_backend_probe_result_v1 r{};
    r.struct_size = sizeof(rd_backend_probe_result_v1);
    r.available = 1;
    r.score = 80;
    r.reason = "available";
    return r;
}

rd_backend_probe_result_v1 probe_unavailable() {
    rd_backend_probe_result_v1 r{};
    r.struct_size = sizeof(rd_backend_probe_result_v1);
    r.available = 0;
    r.score = 0;
    r.reason = "not available";
    return r;
}

int create_noop(const rd_backend_create_desc_v1*, rd_backend_instance_v1*) {
    return RD_STATUS_OK;
}

const rd_backend_factory_v1 k_factory_gles = {
    RD_ABI_VERSION,
    "gles",
    "OpenGL ES",
    probe_available_score_30,
    create_noop,
};

const rd_backend_factory_v1 k_factory_dx9 = {
    RD_ABI_VERSION,
    "dx9",
    "Direct3D 9",
    probe_available_score_80,
    create_noop,
};

const rd_backend_factory_v1 k_factory_null = {
    RD_ABI_VERSION,
    "null",
    "Null",
    probe_unavailable,
    create_noop,
};

}  // namespace

TEST(RenderDomainSelectionTests, AutoPicksHighestScoreAvailableBackend) {
    render_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_gles, "test:gles", false);
    registry.register_factory(&k_factory_dx9, "test:dx9", false);

    render_domain::SelectionRequest request;
    request.config_backend = "auto";

    const auto result = render_domain::select_backend(registry, request);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_backend, "dx9");
}

TEST(RenderDomainSelectionTests, ExplicitBackendFailsWhenUnavailable) {
    render_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_null, "test:null", false);

    render_domain::SelectionRequest request;
    request.config_backend = "null";

    const auto result = render_domain::select_backend(registry, request);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.used_explicit_request);
    EXPECT_NE(result.message.find("unavailable"), std::string::npos);
}

TEST(RenderDomainSelectionTests, ExplicitBackendFailsWhenMissing) {
    render_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_gles, "test:gles", false);

    render_domain::SelectionRequest request;
    request.config_backend = "dx9";

    const auto result = render_domain::select_backend(registry, request);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.used_explicit_request);
    EXPECT_NE(result.message.find("not available"), std::string::npos);
}

TEST(RenderDomainSelectionTests, ExplicitCliOverrideWinsOverConfig) {
    render_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_gles, "test:gles", false);
    registry.register_factory(&k_factory_dx9, "test:dx9", false);

    render_domain::SelectionRequest request;
    request.config_backend = "gles";
    request.has_cli_override = true;
    request.cli_backend = "dx9";

    const auto result = render_domain::select_backend(registry, request);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.selected_backend, "dx9");
}
