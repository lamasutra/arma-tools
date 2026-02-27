#include "render_domain/rd_ui_render_bridge.h"

#include <gtest/gtest.h>

TEST(RenderDomainUiBridgeTests, GlesBridgeIsAvailable) {
    auto bridge = render_domain::make_ui_render_bridge_for_backend("gles");
    ASSERT_TRUE(bridge);
    const auto info = bridge->info();
    EXPECT_EQ(info.renderer_backend, "gles");
    EXPECT_TRUE(info.available);
    const auto* abi = bridge->bridge_abi();
    ASSERT_NE(abi, nullptr);
    EXPECT_EQ(abi->abi_version, UI_RENDER_BRIDGE_ABI_VERSION);
    EXPECT_EQ(abi->is_available(abi->userdata), 1);
    ASSERT_NE(abi->submit_draw_data, nullptr);
    ui_vertex_v1 verts[4] = {
        {10.0f, 10.0f, 0.0f, 0.0f, 0xFF1F1F1Fu},
        {40.0f, 10.0f, 1.0f, 0.0f, 0xFF1F1F1Fu},
        {40.0f, 30.0f, 1.0f, 1.0f, 0xFF1F1F1Fu},
        {10.0f, 30.0f, 0.0f, 1.0f, 0xFF1F1F1Fu},
    };
    uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    ui_draw_cmd_v1 cmd{6u, 0u, 0u, 10.0f, 10.0f, 40.0f, 30.0f};
    ui_draw_data_v1 draw_data{};
    draw_data.struct_size = sizeof(ui_draw_data_v1);
    draw_data.vertices = verts;
    draw_data.vertex_count = 4;
    draw_data.indices = idx;
    draw_data.index_count = 6;
    draw_data.commands = &cmd;
    draw_data.command_count = 1;
    EXPECT_EQ(abi->submit_draw_data(abi->userdata, &draw_data), RD_STATUS_OK);
    EXPECT_EQ(bridge->begin_frame(), RD_STATUS_OK);
    EXPECT_EQ(bridge->draw_overlay(), RD_STATUS_OK);
    EXPECT_EQ(bridge->end_frame(), RD_STATUS_OK);
    EXPECT_EQ(abi->begin_frame(abi->userdata), RD_STATUS_OK);
    EXPECT_EQ(abi->draw_overlay(abi->userdata), RD_STATUS_OK);
    EXPECT_EQ(abi->end_frame(abi->userdata), RD_STATUS_OK);
}

TEST(RenderDomainUiBridgeTests, NullBridgeIsUnavailable) {
    auto bridge = render_domain::make_ui_render_bridge_for_backend("null");
    ASSERT_TRUE(bridge);
    const auto info = bridge->info();
    EXPECT_EQ(info.renderer_backend, "null");
    EXPECT_FALSE(info.available);
    const auto* abi = bridge->bridge_abi();
    ASSERT_NE(abi, nullptr);
    EXPECT_EQ(abi->is_available(abi->userdata), 0);
    ASSERT_NE(abi->submit_draw_data, nullptr);
    ui_draw_data_v1 draw_data{};
    draw_data.struct_size = sizeof(ui_draw_data_v1);
    EXPECT_EQ(abi->submit_draw_data(abi->userdata, &draw_data), RD_STATUS_NOT_IMPLEMENTED);
    EXPECT_EQ(bridge->begin_frame(), RD_STATUS_NOT_IMPLEMENTED);
    EXPECT_EQ(bridge->draw_overlay(), RD_STATUS_NOT_IMPLEMENTED);
    EXPECT_EQ(bridge->end_frame(), RD_STATUS_NOT_IMPLEMENTED);
}

TEST(RenderDomainUiBridgeTests, UnknownBridgeIsUnavailable) {
    auto bridge = render_domain::make_ui_render_bridge_for_backend("dx9");
    ASSERT_TRUE(bridge);
    const auto info = bridge->info();
    EXPECT_EQ(info.renderer_backend, "dx9");
    EXPECT_FALSE(info.available);
}

TEST(RenderDomainUiBridgeTests, GlesBridgeRejectsInvalidDrawData) {
    auto bridge = render_domain::make_ui_render_bridge_for_backend("gles");
    ASSERT_TRUE(bridge);

    EXPECT_EQ(bridge->submit_draw_data(nullptr), RD_STATUS_INVALID_ARGUMENT);

    ui_draw_data_v1 invalid_struct{};
    invalid_struct.struct_size = sizeof(ui_draw_data_v1) - 1;
    EXPECT_EQ(bridge->submit_draw_data(&invalid_struct), RD_STATUS_INVALID_ARGUMENT);

    ui_vertex_v1 verts[1] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
    };
    uint16_t idx[3] = {0, 0, 0};
    ui_draw_cmd_v1 cmd{3u, 1u, 0u, 0.0f, 0.0f, 10.0f, 10.0f};

    ui_draw_data_v1 out_of_range{};
    out_of_range.struct_size = sizeof(ui_draw_data_v1);
    out_of_range.vertices = verts;
    out_of_range.vertex_count = 1;
    out_of_range.indices = idx;
    out_of_range.index_count = 3;
    out_of_range.commands = &cmd;
    out_of_range.command_count = 1;
    EXPECT_EQ(bridge->submit_draw_data(&out_of_range), RD_STATUS_INVALID_ARGUMENT);
}

TEST(RenderDomainUiBridgeTests, GlesBridgeAcceptsDrawDataWithVertexOffset) {
    auto bridge = render_domain::make_ui_render_bridge_for_backend("gles");
    ASSERT_TRUE(bridge);

    ui_vertex_v1 verts[4] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {10.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {10.0f, 10.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {0.0f, 10.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
    };
    uint16_t idx[3] = {0, 1, 2};
    ui_draw_cmd_v1 cmd{3u, 0u, 1u, 0.0f, 0.0f, 10.0f, 10.0f};
    ui_draw_data_v1 draw_data{};
    draw_data.struct_size = sizeof(ui_draw_data_v1);
    draw_data.vertices = verts;
    draw_data.vertex_count = 4;
    draw_data.indices = idx;
    draw_data.index_count = 3;
    draw_data.commands = &cmd;
    draw_data.command_count = 1;

    EXPECT_EQ(bridge->submit_draw_data(&draw_data), RD_STATUS_OK);
}
