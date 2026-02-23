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
using armatools::config::StringEntry;

TEST(RvmatTest, ParsesCorePropertiesAndStages) {
    Config cfg;
    cfg.root.entries.push_back({"PixelShaderID", StringEntry{"Super"}});
    cfg.root.entries.push_back({"VertexShaderID", StringEntry{"Super"}});
    cfg.root.entries.push_back({"surfaceInfo", StringEntry{"road"}});
    cfg.root.entries.push_back({"specularPower", FloatEntry{55.0f}});

    ArrayEntry ambient;
    ambient.elements.push_back(FloatElement{0.1f});
    ambient.elements.push_back(FloatElement{0.2f});
    ambient.elements.push_back(FloatElement{0.3f});
    ambient.elements.push_back(FloatElement{1.0f});
    cfg.root.entries.push_back({"ambient", ambient});

    auto stage1 = std::make_unique<ConfigClass>();
    stage1->entries.push_back({"texture", StringEntry{"a3/data/diffuse_co.paa"}});
    stage1->entries.push_back({"uvSource", StringEntry{"tex"}});
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

    ASSERT_EQ(m.stages.size(), 2u);
    EXPECT_EQ(m.stages[0].stage_number, 1);
    EXPECT_EQ(m.stages[0].texture_path, "a3/data/diffuse_co.paa");
    EXPECT_EQ(m.stages[0].uv_source, "tex");
    EXPECT_EQ(m.stages[1].stage_number, 2);
    EXPECT_EQ(m.stages[1].texture_path, "a3/data/normal_nohq.paa");
}

TEST(RvmatTest, AcceptsIntegerSpecularPower) {
    Config cfg;
    cfg.root.entries.push_back({"specularPower", IntEntry{12}});
    auto m = armatools::rvmat::parse(cfg);
    EXPECT_FLOAT_EQ(m.specular_power, 12.0f);
}
