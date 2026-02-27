#include "render_domain/rd_scene_blob.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(RenderDomainCameraBlobTests, MakeBuildsValidBlob) {
    float view[16]{};
    float projection[16]{};
    float position[3]{1.0f, 2.0f, 3.0f};
    for (int i = 0; i < 16; ++i) {
        view[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        projection[i] = (i % 5 == 0) ? 2.0f : 0.0f;
    }

    const rd_camera_blob_v1 camera =
        render_domain::make_camera_blob_v1(view, projection, position);

    EXPECT_EQ(camera.struct_size, sizeof(rd_camera_blob_v1));
    EXPECT_EQ(camera.version, RD_CAMERA_BLOB_VERSION);
    EXPECT_FLOAT_EQ(camera.position[0], 1.0f);
    EXPECT_FLOAT_EQ(camera.position[1], 2.0f);
    EXPECT_FLOAT_EQ(camera.position[2], 3.0f);

    std::string error;
    EXPECT_TRUE(render_domain::validate_camera_blob_v1(camera, &error)) << error;
}

TEST(RenderDomainCameraBlobTests, RejectsInvalidVersion) {
    rd_camera_blob_v1 camera = render_domain::make_camera_blob_v1(nullptr, nullptr, nullptr);
    camera.version = 999;

    std::string error;
    EXPECT_FALSE(render_domain::validate_camera_blob_v1(camera, &error));
    EXPECT_NE(error.find("version"), std::string::npos);
}

TEST(RenderDomainCameraBlobTests, RejectsNonFiniteViewValues) {
    rd_camera_blob_v1 camera = render_domain::make_camera_blob_v1(nullptr, nullptr, nullptr);
    camera.view[0] = std::nanf("");

    std::string error;
    EXPECT_FALSE(render_domain::validate_camera_blob_v1(camera, &error));
    EXPECT_NE(error.find("view matrix"), std::string::npos);
}
