#include "app/wrp_terrain_camera_controller.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {
bool nearly_equal(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

TEST(WrpTerrainCameraControllerTest, WorldDefaultsCenterPivotAndDistance) {
    WrpTerrainCameraController controller;
    controller.set_world_defaults(1000.0f, 2000.0f, 10.0f, 30.0f);

    const auto state = controller.camera_state();
    EXPECT_TRUE(nearly_equal(state.pivot[0], 500.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[2], 1000.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[1], 20.0f));
    EXPECT_TRUE(nearly_equal(state.distance, 1500.0f));
    EXPECT_TRUE(nearly_equal(state.azimuth, 0.65f));
    EXPECT_TRUE(nearly_equal(state.elevation, 0.85f));
}

TEST(WrpTerrainCameraControllerTest, OrbitAndZoomClamp) {
    WrpTerrainCameraController controller;
    auto s = controller.camera_state();

    controller.orbit_from_drag(s.azimuth, s.elevation, 0.0, -10000.0);
    s = controller.camera_state();
    EXPECT_TRUE(nearly_equal(s.elevation, 1.57f));

    for (int i = 0; i < 200; ++i) controller.zoom_from_scroll(1.0);
    s = controller.camera_state();
    EXPECT_TRUE(nearly_equal(s.distance, 5.0f));

    for (int i = 0; i < 200; ++i) controller.zoom_from_scroll(-1.0);
    s = controller.camera_state();
    EXPECT_TRUE(nearly_equal(s.distance, 250000.0f));
}

TEST(WrpTerrainCameraControllerTest, PanFromDragChangesPivot) {
    WrpTerrainCameraController controller;
    const auto start = controller.camera_state();

    controller.pan_from_drag(start.pivot, 50.0, -30.0);
    const auto after = controller.camera_state();

    EXPECT_FALSE(nearly_equal(start.pivot[0], after.pivot[0]) &&
                 nearly_equal(start.pivot[1], after.pivot[1]) &&
                 nearly_equal(start.pivot[2], after.pivot[2]));
}

TEST(WrpTerrainCameraControllerTest, BuildEyeCenterAndMoveLocal) {
    WrpTerrainCameraController controller;
    wrpterrain::CameraState state = controller.camera_state();
    state.pivot[0] = 100.0f;
    state.pivot[1] = 50.0f;
    state.pivot[2] = 200.0f;
    state.distance = 500.0f;
    state.azimuth = 0.0f;
    state.elevation = 0.0f;
    controller.set_camera_state(state);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    controller.build_eye_center(eye, center);

    EXPECT_TRUE(nearly_equal(eye[0], 100.0f));
    EXPECT_TRUE(nearly_equal(eye[1], 550.0f));
    EXPECT_TRUE(nearly_equal(eye[2], 200.0f));

    controller.move_local(10.0f, 0.0f, 5.0f);
    const auto moved = controller.camera_state();
    EXPECT_TRUE(nearly_equal(moved.pivot[2], 210.0f));
    EXPECT_TRUE(nearly_equal(moved.pivot[1], 55.0f));
}
