#include "app/model_view_panel_presenter.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

armatools::p3d::LOD make_lod_with_geometry() {
    armatools::p3d::LOD lod;
    lod.resolution_name = "1.000";
    lod.vertices = {
        armatools::p3d::Vector3P{1.0f, 2.0f, 3.0f},
        armatools::p3d::Vector3P{4.0f, 5.0f, 6.0f},
        armatools::p3d::Vector3P{7.0f, 8.0f, 9.0f},
    };
    lod.faces = {{0u, 1u, 2u}};
    lod.face_count = 1;
    lod.face_data.resize(1);
    lod.named_selections = {"facesel", "vertsel", "missing"};
    lod.named_selection_faces["facesel"] = {0u};
    lod.named_selection_vertices["vertsel"] = {2u};
    return lod;
}

}  // namespace

TEST(ModelViewPanelPresenterTest, ChoosesFirstRenderableLodAsDefault) {
    ModelViewPanelPresenter presenter;
    std::vector<armatools::p3d::LOD> lods(3);
    lods[1].face_count = 1;
    lods[1].face_data.resize(1);
    lods[1].vertices.push_back(armatools::p3d::Vector3P{0.0f, 0.0f, 0.0f});

    EXPECT_EQ(presenter.choose_default_lod_index(lods), 1);
}

TEST(ModelViewPanelPresenterTest, ActiveLodAlwaysKeepsAtLeastOneIndex) {
    ModelViewPanelPresenter presenter;
    presenter.set_single_active_lod(3);

    EXPECT_FALSE(presenter.set_lod_active(3, false));
    EXPECT_TRUE(presenter.is_lod_active(3));

    EXPECT_TRUE(presenter.set_lod_active(4, true));
    EXPECT_TRUE(presenter.set_lod_active(3, false));
    EXPECT_FALSE(presenter.is_lod_active(3));
    EXPECT_TRUE(presenter.is_lod_active(4));
}

TEST(ModelViewPanelPresenterTest, NamedSelectionItemsCarryFaceAndVertexCounts) {
    ModelViewPanelPresenter presenter;
    const auto lod = make_lod_with_geometry();

    presenter.set_named_selection_source(lod);
    const auto& items = presenter.named_selection_items();

    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].label, "facesel (F:1, V:0)");
    EXPECT_EQ(items[1].label, "vertsel (F:0, V:1)");
    EXPECT_EQ(items[2].label, "missing (F:0, V:0)");
}

TEST(ModelViewPanelPresenterTest, HighlightPrefersFaceEdgesThenFallsBackToPoints) {
    ModelViewPanelPresenter presenter;
    const auto lod = make_lod_with_geometry();
    presenter.set_named_selection_source(lod);

    presenter.set_named_selection_active("facesel", true);
    presenter.set_named_selection_active("vertsel", true);
    auto highlight = presenter.build_highlight_geometry();
    EXPECT_EQ(highlight.mode, modelview::HighlightMode::Lines);
    EXPECT_EQ(highlight.positions.size(), 18u);

    presenter.set_named_selection_active("facesel", false);
    highlight = presenter.build_highlight_geometry();
    EXPECT_EQ(highlight.mode, modelview::HighlightMode::Points);
    ASSERT_EQ(highlight.positions.size(), 3u);
    EXPECT_FLOAT_EQ(highlight.positions[0], -7.0f);
    EXPECT_FLOAT_EQ(highlight.positions[1], 8.0f);
    EXPECT_FLOAT_EQ(highlight.positions[2], 9.0f);
}
