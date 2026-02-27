#include "render_domain/rd_scene_blob_builder.h"

#include "render_domain/rd_scene_blob.h"

#include <gtest/gtest.h>

namespace {

armatools::p3d::LOD make_triangle_lod() {
    armatools::p3d::LOD lod;
    lod.vertices = {
        armatools::p3d::Vector3P{1.0f, 0.0f, 0.0f},
        armatools::p3d::Vector3P{0.0f, 1.0f, 0.0f},
        armatools::p3d::Vector3P{0.0f, 0.0f, 1.0f},
    };
    lod.normals = {
        armatools::p3d::Vector3P{0.0f, 1.0f, 0.0f},
    };

    armatools::p3d::Face face;
    face.texture = "A3\\Data_F\\Test_CA.PAA";
    face.vertices = {
        armatools::p3d::FaceVertex{0, 0, {0.0f, 0.0f}},
        armatools::p3d::FaceVertex{1, 0, {1.0f, 0.0f}},
        armatools::p3d::FaceVertex{2, 0, {0.0f, 1.0f}},
    };
    lod.face_data.push_back(face);
    return lod;
}

}  // namespace

TEST(RenderDomainSceneBlobBuilderTests, BuildsValidBlobForTriangle) {
    const auto lod = make_triangle_lod();

    render_domain::SceneBlobBuildOutput out;
    std::string error;
    ASSERT_TRUE(render_domain::build_scene_blob_v1_from_lods({lod}, &out, &error))
        << error;

    EXPECT_EQ(out.blob.version, RD_SCENE_BLOB_VERSION);
    EXPECT_EQ(out.blob.vertex_count, 3u);
    EXPECT_EQ(out.blob.index_count, 3u);
    EXPECT_EQ(out.blob.mesh_count, 1u);
    EXPECT_EQ(out.blob.material_count, 1u);
    ASSERT_EQ(out.material_texture_keys.size(), 1u);
    EXPECT_EQ(out.material_texture_keys[0], "a3/data_f/test_ca.paa");

    std::string validation_error;
    EXPECT_TRUE(render_domain::validate_scene_blob_v1(out.blob, &validation_error))
        << validation_error;

    const auto* positions = reinterpret_cast<const float*>(out.blob.data + out.blob.positions_offset);
    ASSERT_NE(positions, nullptr);
    EXPECT_FLOAT_EQ(positions[0], -1.0f);
    EXPECT_FLOAT_EQ(positions[1], 0.0f);
    EXPECT_FLOAT_EQ(positions[2], 0.0f);
}

TEST(RenderDomainSceneBlobBuilderTests, TriangulatesPolygonFaces) {
    armatools::p3d::LOD lod;
    lod.vertices = {
        armatools::p3d::Vector3P{0.0f, 0.0f, 0.0f},
        armatools::p3d::Vector3P{1.0f, 0.0f, 0.0f},
        armatools::p3d::Vector3P{1.0f, 1.0f, 0.0f},
        armatools::p3d::Vector3P{0.0f, 1.0f, 0.0f},
    };

    armatools::p3d::Face face;
    face.material = "A3\\Mat\\quad.rvmat";
    face.vertices = {
        armatools::p3d::FaceVertex{0, -1, {0.0f, 0.0f}},
        armatools::p3d::FaceVertex{1, -1, {1.0f, 0.0f}},
        armatools::p3d::FaceVertex{2, -1, {1.0f, 1.0f}},
        armatools::p3d::FaceVertex{3, -1, {0.0f, 1.0f}},
    };
    lod.face_data.push_back(face);

    render_domain::SceneBlobBuildOutput out;
    std::string error;
    ASSERT_TRUE(render_domain::build_scene_blob_v1_from_lods({lod}, &out, &error))
        << error;

    EXPECT_EQ(out.blob.index_count, 6u);
    EXPECT_EQ(out.blob.vertex_count, 6u);
    ASSERT_EQ(out.material_texture_keys.size(), 1u);
    EXPECT_EQ(out.material_texture_keys[0], "a3/mat/quad.rvmat");
}

TEST(RenderDomainSceneBlobBuilderTests, BuildsValidEmptyBlobForNoGeometry) {
    render_domain::SceneBlobBuildOutput out;
    std::string error;
    ASSERT_TRUE(render_domain::build_scene_blob_v1_from_lods({}, &out, &error))
        << error;

    EXPECT_EQ(out.blob.vertex_count, 0u);
    EXPECT_EQ(out.blob.index_count, 0u);
    EXPECT_EQ(out.blob.mesh_count, 0u);
    EXPECT_EQ(out.blob.material_count, 0u);
    EXPECT_EQ(out.blob.data_size, 0u);
    EXPECT_EQ(out.blob.data, nullptr);

    std::string validation_error;
    EXPECT_TRUE(render_domain::validate_scene_blob_v1(out.blob, &validation_error))
        << validation_error;
}
