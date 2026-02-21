#include "armatools/heightpipe.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace hp = armatools::heightpipe;

namespace {

hp::Heightmap make_ramp(int w, int h) {
    hp::Heightmap m(w, h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            m.at(x, y) = static_cast<float>(x + y) / static_cast<float>(w + h);
        }
    }
    return m;
}

hp::Heightmap downsample_avg(const hp::Heightmap& in, int factor) {
    hp::Heightmap out(in.width / factor, in.height / factor, 0.0f);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            float sum = 0.0f;
            for (int j = 0; j < factor; ++j) {
                for (int i = 0; i < factor; ++i) {
                    sum += in.data[static_cast<size_t>((y * factor + j) * in.width + (x * factor + i))];
                }
            }
            out.at(x, y) = sum / static_cast<float>(factor * factor);
        }
    }
    return out;
}

float rmse(const hp::Heightmap& a, const hp::Heightmap& b) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.data.size(); ++i) {
        const float d = a.data[i] - b.data[i];
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<float>(a.data.size()));
}

} // namespace

TEST(Heightpipe, BicubicRampIsMonotonic) {
    hp::Heightmap src(8, 1, 0.0f);
    for (int x = 0; x < src.width; ++x) src.at(x, 0) = static_cast<float>(x);

    const hp::Heightmap up = hp::resample(src, 4, hp::ResampleMethod::Bicubic, hp::EdgeMode::Clamp);
    for (int x = 1; x < up.width; ++x) {
        EXPECT_GE(up.at(x, 0), up.at(x - 1, 0));
    }
}

TEST(Heightpipe, PipelineDeterministicForSeed) {
    const hp::Heightmap src = make_ramp(32, 32);
    hp::PipelineOptions opt;
    opt.scale = 4;
    opt.seed = 1337;
    opt.correction = hp::correction_preset_for_scale(4, hp::CorrectionPreset::RetainDetail);
    opt.erosion = hp::erosion_preset_for_scale(4);

    const hp::PipelineOutputs a = hp::run_pipeline(src, opt);
    const hp::PipelineOutputs b = hp::run_pipeline(src, opt);

    ASSERT_EQ(a.out.data.size(), b.out.data.size());
    for (size_t i = 0; i < a.out.data.size(); ++i) {
        EXPECT_FLOAT_EQ(a.out.data[i], b.out.data[i]);
    }
}

TEST(Heightpipe, MacroShapeRoughlyPreserved) {
    hp::Heightmap src(24, 24, 0.0f);
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const float xf = static_cast<float>(x) / static_cast<float>(src.width);
            const float yf = static_cast<float>(y) / static_cast<float>(src.height);
            src.at(x, y) = 40.0f * std::sin(3.0f * xf) + 25.0f * std::cos(2.0f * yf);
        }
    }

    hp::PipelineOptions opt;
    opt.scale = 4;
    opt.seed = 7;
    opt.correction = hp::correction_preset_for_scale(4, hp::CorrectionPreset::Terrain16x);
    opt.erosion = hp::erosion_preset_for_scale(4);

    const hp::PipelineOutputs out = hp::run_pipeline(src, opt);
    const hp::Heightmap reduced = downsample_avg(out.out, 4);

    EXPECT_LT(rmse(src, reduced), 15.0f);
}
