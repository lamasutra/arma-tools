#include "armatools/config.h"
#include "armatools/rvmat.h"

#include <gtest/gtest.h>

using armatools::config::ArrayEntry;
using armatools::config::ClassEntryOwned;
using armatools::config::Config;
using armatools::config::ConfigClass;
using armatools::config::FloatElement;
using armatools::config::FloatEntry;
using armatools::config::IntEntry;
using armatools::config::StringElement;
using armatools::config::StringEntry;

TEST(RvmatTest, ParsesCorePropertiesAndStages) {
    Config cfg;
    cfg.root.entries.push_back({"PixelShaderID", StringEntry{"Super"}});
    cfg.root.entries.push_back({"VertexShaderID", StringEntry{"Super"}});
    cfg.root.entries.push_back({"surfaceInfo", StringEntry{"road"}});
    cfg.root.entries.push_back({"specularPower", FloatEntry{55.0f}});
    cfg.root.entries.push_back({"mainLight", StringEntry{"Sun"}});
    cfg.root.entries.push_back({"fogMode", StringEntry{"Fog"}});

    ArrayEntry renderFlags;
    renderFlags.elements.push_back(StringElement{"NoZWrite"});
    renderFlags.elements.push_back(StringElement{"AddBlend"});
    cfg.root.entries.push_back({"renderFlags", renderFlags});

    ArrayEntry ambient;
    ambient.elements.push_back(FloatElement{0.1f});
    ambient.elements.push_back(FloatElement{0.2f});
    ambient.elements.push_back(FloatElement{0.3f});
    ambient.elements.push_back(FloatElement{1.0f});
    cfg.root.entries.push_back({"ambient", ambient});

    auto stage1 = std::make_unique<ConfigClass>();
    stage1->entries.push_back({"texture", StringEntry{"a3/data/diffuse_co.paa"}});
    stage1->entries.push_back({"uvSource", StringEntry{"tex"}});
    ArrayEntry uv;
    uv.elements.push_back(FloatElement{2.0f});
    uv.elements.push_back(FloatElement{0.0f});
    uv.elements.push_back(FloatElement{0.0f});
    uv.elements.push_back(FloatElement{0.0f});
    uv.elements.push_back(FloatElement{2.0f});
    uv.elements.push_back(FloatElement{0.0f});
    uv.elements.push_back(FloatElement{0.1f});
    uv.elements.push_back(FloatElement{0.2f});
    uv.elements.push_back(FloatElement{0.0f});
    stage1->entries.push_back({"uvTransform", uv});
    cfg.root.entries.push_back({"Stage1", ClassEntryOwned{std::move(stage1)}});

    auto stage2 = std::make_unique<ConfigClass>();
    stage2->entries.push_back({"texture", StringEntry{"a3/data/normal_nohq.paa"}});
    cfg.root.entries.push_back({"Stage2", ClassEntryOwned{std::move(stage2)}});

    auto m = armatools::rvmat::parse(cfg);
    EXPECT_EQ(m.pixel_shader, "Super");
    EXPECT_EQ(m.vertex_shader, "Super");
    EXPECT_EQ(m.surface, "road");
    EXPECT_FLOAT_EQ(m.specular_power, 55.0f);
    EXPECT_FLOAT_EQ(m.ambient[0], 0.1f);
    EXPECT_FLOAT_EQ(m.ambient[1], 0.2f);
    EXPECT_FLOAT_EQ(m.ambient[2], 0.3f);
    EXPECT_FLOAT_EQ(m.ambient[3], 1.0f);
    EXPECT_EQ(m.main_light, "Sun");
    EXPECT_EQ(m.fog_mode, "Fog");
    ASSERT_EQ(m.render_flags.size(), 2u);
    EXPECT_EQ(m.render_flags[0], "NoZWrite");
    EXPECT_EQ(m.render_flags[1], "AddBlend");

    ASSERT_EQ(m.stages.size(), 2u);
    EXPECT_EQ(m.stages[0].stage_number, 1);
    EXPECT_EQ(m.stages[0].texture_path, "a3/data/diffuse_co.paa");
    EXPECT_EQ(m.stages[0].uv_source, "tex");
    EXPECT_TRUE(m.stages[0].uv_transform.valid);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.aside[0], 2.0f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.up[1], 2.0f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.pos[0], 0.1f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.pos[1], 0.2f);
    EXPECT_EQ(m.stages[1].stage_number, 2);
    EXPECT_EQ(m.stages[1].texture_path, "a3/data/normal_nohq.paa");
}

TEST(RvmatTest, AcceptsIntegerSpecularPower) {
    Config cfg;
    cfg.root.entries.push_back({"specularPower", IntEntry{12}});
    auto m = armatools::rvmat::parse(cfg);
    EXPECT_FLOAT_EQ(m.specular_power, 12.0f);
}

TEST(RvmatTest, ParsesTexGenAnd12ElementUV) {
    Config cfg;
    
    // TexGen0 with class-style uvTransform
    auto tg0 = std::make_unique<ConfigClass>();
    tg0->entries.push_back({"uvSource", StringEntry{"worldPos"}});
    auto uv0 = std::make_unique<ConfigClass>();
    ArrayEntry aside; aside.elements = {FloatElement{1}, FloatElement{2}, FloatElement{3}};
    ArrayEntry dir;   dir.elements   = {FloatElement{4}, FloatElement{5}, FloatElement{6}};
    uv0->entries.push_back({"aside", aside});
    uv0->entries.push_back({"dir", dir});
    tg0->entries.push_back({"uvTransform", ClassEntryOwned{std::move(uv0)}});
    cfg.root.entries.push_back({"TexGen0", ClassEntryOwned{std::move(tg0)}});

    // Stage0 referencing TexGen0
    auto stage0 = std::make_unique<ConfigClass>();
    stage0->entries.push_back({"texGen", IntEntry{0}});
    // 12-element array uvTransform
    ArrayEntry uv12;
    for (int i = 0; i < 12; ++i) uv12.elements.push_back(FloatElement{static_cast<float>(i + 1)});
    stage0->entries.push_back({"uvTransform", uv12});
    cfg.root.entries.push_back({"Stage0", ClassEntryOwned{std::move(stage0)}});

    auto m = armatools::rvmat::parse(cfg);
    
    ASSERT_EQ(m.tex_gens.size(), 1u);
    EXPECT_EQ(m.tex_gens[0].index, 0);
    EXPECT_EQ(m.tex_gens[0].uv_source, "worldPos");
    EXPECT_FLOAT_EQ(m.tex_gens[0].uv_transform.aside[1], 2.0f);
    EXPECT_FLOAT_EQ(m.tex_gens[0].uv_transform.dir[2], 6.0f);

    ASSERT_EQ(m.stages.size(), 1u);
    EXPECT_EQ(m.stages[0].tex_gen, "0");
    EXPECT_TRUE(m.stages[0].uv_transform.valid);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.aside[0], 1.0f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.up[0], 4.0f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.dir[0], 7.0f);
    EXPECT_FLOAT_EQ(m.stages[0].uv_transform.pos[0], 10.0f);
}
