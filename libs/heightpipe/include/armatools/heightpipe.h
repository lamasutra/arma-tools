#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace armatools::heightpipe {

struct Heightmap {
    int width = 0;
    int height = 0;
    std::vector<float> data;

    Heightmap() = default;
    Heightmap(int w, int h, float value = 0.0f);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] float& at(int x, int y);
    [[nodiscard]] const float& at(int x, int y) const;
};

enum class EdgeMode { Clamp, Wrap, Mirror };
enum class ResampleMethod { Bicubic, Lanczos3 };
enum class CorrectionMode {
    None,
    Unsharp,
    CurvatureGain,
    Residual,
    GuidedSharp,
    Hybrid,
    Preset,
};

enum class CorrectionPreset {
    None,
    Sharp,
    RetainDetail,
    Terrain16x,
};

struct UpscaleCorrectionParams {
    CorrectionMode mode = CorrectionMode::Preset;
    CorrectionPreset preset = CorrectionPreset::Sharp;
    bool enable_unsharp = true;
    bool enable_curvature = false;
    bool enable_residual = false;
    bool enable_guided_sharp = true;
    bool enable_noise = false;

    float unsharp_sigma_base = 0.6f;
    float unsharp_amount_base = 0.15f;
    float curvature_gain_base = 0.05f;
    float residual_gain_min = 0.5f;
    float residual_gain_max = 1.0f;
    float slope_lo = 0.02f;
    float slope_hi = 0.25f;
    float guided_radius_base = 2.0f;
    float guided_sigma = 0.08f;
    float guided_sharpen = 1.15f;
    float noise_base_amp = 0.0005f;
    float noise_slope_weight = 0.7f;
    float noise_curv_weight = 0.3f;
    float noise_bias = 0.05f;
};

struct ErosionParams {
    bool enable_macro = true;
    bool enable_meso = true;
    bool enable_micro = true;

    int macro_droplets = 15000;
    int meso_droplets = 50000;
    int micro_droplets = 10000;

    int max_steps = 40;
    float inertia = 0.05f;
    float capacity = 4.0f;
    float deposition = 0.2f;
    float erosion = 0.25f;
    float evaporation = 0.03f;
    float gravity = 4.0f;
    float min_slope = 0.01f;
    float radius_base = 1.2f;

    int thermal_iters = 6;
    float talus = 0.9f;
    float thermal_factor = 0.2f;
};

struct PipelineOptions {
    int scale = 2;
    ResampleMethod resample = ResampleMethod::Bicubic;
    EdgeMode edge_mode = EdgeMode::Clamp;
    UpscaleCorrectionParams correction;
    ErosionParams erosion;
    uint32_t seed = 1;
    bool dump_slope = false;
    bool dump_curvature = false;
    bool dump_flow = false;
};

struct PipelineOutputs {
    Heightmap out;
    std::optional<Heightmap> slope;
    std::optional<Heightmap> curvature;
    std::optional<Heightmap> flow;
};

Heightmap resample(const Heightmap& in, int scale, ResampleMethod method, EdgeMode edge_mode);
Heightmap apply_upscale_corrections(
    const Heightmap& upsampled,
    const Heightmap& source,
    int scale,
    const UpscaleCorrectionParams& params,
    uint32_t seed);
Heightmap erode_multiscale(const Heightmap& input, int scale, const ErosionParams& params, uint32_t seed, Heightmap* flow_out = nullptr);
PipelineOutputs run_pipeline(const Heightmap& in, const PipelineOptions& opt);

UpscaleCorrectionParams correction_preset_for_scale(int scale, CorrectionPreset preset);
ErosionParams erosion_preset_for_scale(int scale);

} // namespace armatools::heightpipe
