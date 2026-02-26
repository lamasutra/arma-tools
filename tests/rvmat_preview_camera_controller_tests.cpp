#include "app/rvmat_preview_camera_controller.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {
bool nearly_equal(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

TEST(RvmatPreviewCameraControllerTest, OrbitDragClampsElevation) {
    RvmatPreviewCameraController controller;
    const auto state = controller.camera_state();

    controller.orbit_from_drag(state.azimuth, state.elevation, 0.0, 10000.0);
    const auto updated = controller.camera_state();
    EXPECT_TRUE(nearly_equal(updated.elevation, 1.5f));
}

TEST(RvmatPreviewCameraControllerTest, ZoomScrollHonorsMinimumDistance) {
    RvmatPreviewCameraController controller;
    for (int i = 0; i < 100; ++i) controller.zoom_from_scroll(-1.0);

    const auto state = controller.camera_state();
    EXPECT_GE(state.distance, 0.25f);
    EXPECT_TRUE(nearly_equal(state.distance, 0.25f));
}

TEST(RvmatPreviewCameraControllerTest, PanFromDragChangesPivot) {
    RvmatPreviewCameraController controller;
    const auto start = controller.camera_state();

    controller.pan_from_drag(start.pivot, 20.0, -10.0);
    const auto after = controller.camera_state();
    EXPECT_FALSE(nearly_equal(start.pivot[0], after.pivot[0]) &&
                 nearly_equal(start.pivot[1], after.pivot[1]) &&
                 nearly_equal(start.pivot[2], after.pivot[2]));
}

TEST(RvmatPreviewCameraControllerTest, BuildEyeCenterTargetsPivot) {
    RvmatPreviewCameraController controller;
    rvmatpreview::CameraState state = controller.camera_state();
    state.pivot[0] = 3.0f;
    state.pivot[1] = -2.0f;
    state.pivot[2] = 5.0f;
    controller.set_camera_state(state);

    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    controller.build_eye_center(eye, center);

    EXPECT_TRUE(nearly_equal(center[0], 3.0f));
    EXPECT_TRUE(nearly_equal(center[1], -2.0f));
    EXPECT_TRUE(nearly_equal(center[2], 5.0f));
}
