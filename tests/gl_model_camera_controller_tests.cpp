#include "app/gl_model_camera_controller.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {
bool nearly_equal(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}
}  // namespace

TEST(GlModelCameraControllerTest, ResetRestoresDefaultCameraWithoutBounds) {
    GlModelCameraController controller;
    glmodel::CameraState changed;
    changed.azimuth = 1.2f;
    changed.elevation = -0.5f;
    changed.distance = 42.0f;
    changed.pivot[0] = 9.0f;
    changed.pivot[1] = 8.0f;
    changed.pivot[2] = 7.0f;
    controller.set_camera_state(changed);

    controller.reset_camera();
    const auto state = controller.camera_state();
    EXPECT_TRUE(nearly_equal(state.azimuth, 0.4f));
    EXPECT_TRUE(nearly_equal(state.elevation, 0.3f));
    EXPECT_TRUE(nearly_equal(state.distance, 5.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[0], 0.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[1], 0.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[2], 0.0f));
}

TEST(GlModelCameraControllerTest, SetCameraFromBoundsSeedsOrbitDefaults) {
    GlModelCameraController controller;
    controller.set_camera_from_bounds(10.0f, 20.0f, 30.0f, 4.0f);

    const auto state = controller.camera_state();
    EXPECT_TRUE(nearly_equal(state.distance, 8.0f));
    EXPECT_TRUE(nearly_equal(state.azimuth, 0.4f));
    EXPECT_TRUE(nearly_equal(state.elevation, 0.3f));
    EXPECT_TRUE(nearly_equal(state.pivot[0], 10.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[1], 20.0f));
    EXPECT_TRUE(nearly_equal(state.pivot[2], 30.0f));
}

TEST(GlModelCameraControllerTest, ModeSwitchPreservesEyePosition) {
    GlModelCameraController controller;
    controller.set_camera_from_bounds(0.0f, 0.0f, 0.0f, 2.0f);

    float eye_before[3], center_before[3];
    controller.build_eye_center(eye_before, center_before);
    EXPECT_TRUE(controller.set_camera_mode(glmodel::CameraMode::FirstPerson));

    float eye_after[3], center_after[3];
    controller.build_eye_center(eye_after, center_after);
    EXPECT_TRUE(nearly_equal(eye_before[0], eye_after[0]));
    EXPECT_TRUE(nearly_equal(eye_before[1], eye_after[1]));
    EXPECT_TRUE(nearly_equal(eye_before[2], eye_after[2]));

    EXPECT_TRUE(controller.set_camera_mode(glmodel::CameraMode::Orbit));
    const auto state = controller.camera_state();
    EXPECT_GT(state.distance, 0.0f);
}

TEST(GlModelCameraControllerTest, OrbitDragAndZoomWorkInOrbitMode) {
    GlModelCameraController controller;
    const auto start = controller.camera_state();

    controller.orbit_from_drag(start.azimuth, start.elevation, 0.0, 10000.0);
    auto dragged = controller.camera_state();
    EXPECT_TRUE(nearly_equal(dragged.elevation, 1.5f));

    const float before_zoom = dragged.distance;
    EXPECT_TRUE(controller.scroll_zoom(1.0));
    dragged = controller.camera_state();
    EXPECT_GT(dragged.distance, before_zoom);
}

TEST(GlModelCameraControllerTest, ZoomIgnoredInFirstPersonAndMoveLocalChangesPivot) {
    GlModelCameraController controller;
    ASSERT_TRUE(controller.set_camera_mode(glmodel::CameraMode::FirstPerson));

    const auto before = controller.camera_state();
    EXPECT_FALSE(controller.scroll_zoom(1.0));
    controller.move_local(1.0f, 0.0f, 0.0f);
    const auto after = controller.camera_state();

    EXPECT_FALSE(nearly_equal(before.pivot[0], after.pivot[0]) &&
                 nearly_equal(before.pivot[1], after.pivot[1]) &&
                 nearly_equal(before.pivot[2], after.pivot[2]));
}
