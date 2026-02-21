#include "armatools/heightpipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace armatools::heightpipe {

namespace {

struct Rng32 {
    uint64_t state;

    explicit Rng32(uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

    [[nodiscard]] uint32_t next_u32() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return static_cast<uint32_t>(z ^ (z >> 31));
    }

    [[nodiscard]] float next_f32() {
        return static_cast<float>((next_u32() >> 8) * (1.0 / 16777216.0));
    }
};

[[nodiscard]] int posmod(int v, int m) {
    const int r = v % m;
    return r < 0 ? r + m : r;
}

[[nodiscard]] int mirror_idx(int v, int n) {
    if (n <= 1) return 0;
    const int p = n * 2 - 2;
    int t = posmod(v, p);
    if (t >= n) t = p - t;
    return t;
}

[[nodiscard]] int edge_index(int v, int n, EdgeMode mode) {
    if (n <= 0) return 0;
    switch (mode) {
        case EdgeMode::Clamp: return std::clamp(v, 0, n - 1);
        case EdgeMode::Wrap: return posmod(v, n);
        case EdgeMode::Mirror: return mirror_idx(v, n);
    }
    return std::clamp(v, 0, n - 1);
}

[[nodiscard]] float sample_nearest(const Heightmap& h, int x, int y, EdgeMode mode) {
    const int sx = edge_index(x, h.width, mode);
    const int sy = edge_index(y, h.height, mode);
    return h.data[static_cast<size_t>(sy * h.width + sx)];
}

[[nodiscard]] float cubic_weight(float x) {
    constexpr float a = -0.5f;  // Catmull-Rom
    const float ax = std::fabs(x);
    if (ax < 1.0f) {
        return (a + 2.0f) * ax * ax * ax - (a + 3.0f) * ax * ax + 1.0f;
    }
    if (ax < 2.0f) {
        return a * ax * ax * ax - 5.0f * a * ax * ax + 8.0f * a * ax - 4.0f * a;
    }
    return 0.0f;
}

[[nodiscard]] float sinc(float x) {
    if (std::fabs(x) < 1e-6f) return 1.0f;
    const float px = std::numbers::pi_v<float> * x;
    return std::sin(px) / px;
}

[[nodiscard]] float lanczos_weight(float x, int a) {
    const float ax = std::fabs(x);
    if (ax >= static_cast<float>(a)) return 0.0f;
    return sinc(x) * sinc(x / static_cast<float>(a));
}

[[nodiscard]] float bicubic_sample(const Heightmap& h, float x, float y, EdgeMode mode) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float sum = 0.0f;
    float wsum = 0.0f;
    for (int j = -1; j <= 2; ++j) {
        const float wy = cubic_weight(y - static_cast<float>(iy + j));
        for (int i = -1; i <= 2; ++i) {
            const float wx = cubic_weight(x - static_cast<float>(ix + i));
            const float w = wx * wy;
            sum += w * sample_nearest(h, ix + i, iy + j, mode);
            wsum += w;
        }
    }
    return wsum != 0.0f ? sum / wsum : sample_nearest(h, ix, iy, mode);
}

[[nodiscard]] float lanczos3_sample(const Heightmap& h, float x, float y, EdgeMode mode) {
    constexpr int a = 3;
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    float sum = 0.0f;
    float wsum = 0.0f;
    for (int j = -a + 1; j <= a; ++j) {
        const float wy = lanczos_weight(y - static_cast<float>(iy + j), a);
        for (int i = -a + 1; i <= a; ++i) {
            const float wx = lanczos_weight(x - static_cast<float>(ix + i), a);
            const float w = wx * wy;
            sum += w * sample_nearest(h, ix + i, iy + j, mode);
            wsum += w;
        }
    }
    return wsum != 0.0f ? sum / wsum : sample_nearest(h, ix, iy, mode);
}

[[nodiscard]] Heightmap resample_to(const Heightmap& in, int out_w, int out_h, ResampleMethod method, EdgeMode edge_mode) {
    if (in.empty()) return {};
    Heightmap out(out_w, out_h, 0.0f);
    const float sx = static_cast<float>(in.width) / static_cast<float>(out_w);
    const float sy = static_cast<float>(in.height) / static_cast<float>(out_h);
    for (int y = 0; y < out_h; ++y) {
        const float src_y = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
        for (int x = 0; x < out_w; ++x) {
            const float src_x = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            out.at(x, y) = (method == ResampleMethod::Lanczos3)
                ? lanczos3_sample(in, src_x, src_y, edge_mode)
                : bicubic_sample(in, src_x, src_y, edge_mode);
        }
    }
    return out;
}

[[nodiscard]] std::vector<float> gaussian_kernel(float sigma) {
    const float clamped_sigma = std::max(0.05f, sigma);
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0f * clamped_sigma)));
    std::vector<float> k(static_cast<size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float w = std::exp(-(static_cast<float>(i * i)) / (2.0f * clamped_sigma * clamped_sigma));
        k[static_cast<size_t>(i + radius)] = w;
        sum += w;
    }
    for (float& v : k) v /= sum;
    return k;
}

[[nodiscard]] Heightmap convolve_separable(const Heightmap& in, const std::vector<float>& kernel, EdgeMode edge_mode) {
    const int radius = static_cast<int>(kernel.size() / 2);
    Heightmap tmp(in.width, in.height, 0.0f);
    Heightmap out(in.width, in.height, 0.0f);
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            float s = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                s += kernel[static_cast<size_t>(i + radius)] * sample_nearest(in, x + i, y, edge_mode);
            }
            tmp.at(x, y) = s;
        }
    }
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            float s = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                s += kernel[static_cast<size_t>(i + radius)] * sample_nearest(tmp, x, y + i, edge_mode);
            }
            out.at(x, y) = s;
        }
    }
    return out;
}

[[nodiscard]] Heightmap gaussian_blur(const Heightmap& in, float sigma, EdgeMode edge_mode) {
    return convolve_separable(in, gaussian_kernel(sigma), edge_mode);
}

[[nodiscard]] Heightmap slope_map(const Heightmap& in, EdgeMode mode) {
    Heightmap out(in.width, in.height, 0.0f);
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            const float dx = 0.5f * (sample_nearest(in, x + 1, y, mode) - sample_nearest(in, x - 1, y, mode));
            const float dy = 0.5f * (sample_nearest(in, x, y + 1, mode) - sample_nearest(in, x, y - 1, mode));
            out.at(x, y) = std::sqrt(dx * dx + dy * dy);
        }
    }
    return out;
}

[[nodiscard]] Heightmap curvature_map(const Heightmap& in, EdgeMode mode) {
    Heightmap out(in.width, in.height, 0.0f);
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            const float c = sample_nearest(in, x, y, mode);
            out.at(x, y) =
                sample_nearest(in, x - 1, y, mode) +
                sample_nearest(in, x + 1, y, mode) +
                sample_nearest(in, x, y - 1, mode) +
                sample_nearest(in, x, y + 1, mode) -
                4.0f * c;
        }
    }
    return out;
}

[[nodiscard]] std::pair<float, float> min_max(const Heightmap& in) {
    if (in.data.empty()) return {0.0f, 0.0f};
    const auto [mn_it, mx_it] = std::minmax_element(in.data.begin(), in.data.end());
    return {*mn_it, *mx_it};
}

[[nodiscard]] float smoothstep(float lo, float hi, float x) {
    if (hi <= lo) return x >= hi ? 1.0f : 0.0f;
    float t = (x - lo) / (hi - lo);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] uint32_t hash2d(int x, int y, uint32_t seed) {
    uint32_t h = seed ^ 0x9E3779B9u;
    h ^= static_cast<uint32_t>(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(y) * 0xC2B2AE35u;
    h ^= (h >> 16);
    h *= 0x7FEB352Du;
    h ^= (h >> 15);
    h *= 0x846CA68Bu;
    return h ^ (h >> 16);
}

[[nodiscard]] float value_noise(float x, float y, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    const auto v = [seed](int xx, int yy) {
        return static_cast<float>((hash2d(xx, yy, seed) & 0x00FFFFFFu) / 8388607.5 - 1.0);
    };

    const float a = v(ix, iy);
    const float b = v(ix + 1, iy);
    const float c = v(ix, iy + 1);
    const float d = v(ix + 1, iy + 1);

    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float ab = a + (b - a) * ux;
    const float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
}

[[nodiscard]] float fbm_noise(float x, float y, int octaves, uint32_t seed) {
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value_noise(x * freq, y * freq, seed + static_cast<uint32_t>(i * 1013));
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

[[nodiscard]] float bilinear_sample(const Heightmap& h, float x, float y, EdgeMode mode) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float h00 = sample_nearest(h, x0, y0, mode);
    const float h10 = sample_nearest(h, x0 + 1, y0, mode);
    const float h01 = sample_nearest(h, x0, y0 + 1, mode);
    const float h11 = sample_nearest(h, x0 + 1, y0 + 1, mode);

    const float hx0 = h00 + (h10 - h00) * fx;
    const float hx1 = h01 + (h11 - h01) * fx;
    return hx0 + (hx1 - hx0) * fy;
}

void add_brush(Heightmap& h, float x, float y, float amount, float radius, EdgeMode mode) {
    const int ir = std::max(1, static_cast<int>(std::ceil(radius)));
    float wsum = 0.0f;
    std::array<float, 81> weights{};
    const int dim = 2 * ir + 1;
    for (int j = -ir; j <= ir; ++j) {
        for (int i = -ir; i <= ir; ++i) {
            const float d = std::sqrt(static_cast<float>(i * i + j * j));
            if (d > radius) continue;
            const float w = std::exp(-(d * d) / (2.0f * std::max(0.2f, radius * 0.6f) * std::max(0.2f, radius * 0.6f)));
            const int idx = (j + ir) * dim + (i + ir);
            if (idx < static_cast<int>(weights.size())) {
                weights[static_cast<size_t>(idx)] = w;
                wsum += w;
            }
        }
    }
    if (wsum <= 0.0f) return;

    for (int j = -ir; j <= ir; ++j) {
        for (int i = -ir; i <= ir; ++i) {
            const int idx = (j + ir) * dim + (i + ir);
            if (idx >= static_cast<int>(weights.size())) continue;
            const float w = weights[static_cast<size_t>(idx)];
            if (w == 0.0f) continue;
            const int sx = edge_index(static_cast<int>(std::floor(x)) + i, h.width, mode);
            const int sy = edge_index(static_cast<int>(std::floor(y)) + j, h.height, mode);
            h.at(sx, sy) += amount * (w / wsum);
        }
    }
}

[[nodiscard]] Heightmap guided_like_filter(const Heightmap& in, float radius, float sigma, EdgeMode mode) {
    const int ir = std::max(1, static_cast<int>(std::ceil(radius)));
    Heightmap out(in.width, in.height, 0.0f);
    const float sig2 = std::max(1e-4f, sigma * sigma);

    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            const float center = in.at(x, y);
            float wsum = 0.0f;
            float sum = 0.0f;
            for (int j = -ir; j <= ir; ++j) {
                for (int i = -ir; i <= ir; ++i) {
                    const float v = sample_nearest(in, x + i, y + j, mode);
                    const float ds2 = static_cast<float>(i * i + j * j);
                    const float dr = v - center;
                    const float w = std::exp(-ds2 / (2.0f * radius * radius + 1e-4f) - (dr * dr) / (2.0f * sig2));
                    wsum += w;
                    sum += w * v;
                }
            }
            out.at(x, y) = wsum > 0.0f ? sum / wsum : center;
        }
    }
    return out;
}

void hydraulic_erosion(Heightmap& h, Heightmap* flow, int droplets, const ErosionParams& p, uint32_t seed) {
    Rng32 rng(seed);
    const auto [mn, mx] = min_max(h);
    const float range = std::max(1e-3f, mx - mn);
    const float radius = std::max(1.0f, p.radius_base);

    for (int n = 0; n < droplets; ++n) {
        float x = rng.next_f32() * static_cast<float>(h.width - 1);
        float y = rng.next_f32() * static_cast<float>(h.height - 1);
        float dirx = 0.0f;
        float diry = 0.0f;
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int step = 0; step < p.max_steps; ++step) {
            if (x < 1.0f || y < 1.0f || x >= static_cast<float>(h.width - 2) || y >= static_cast<float>(h.height - 2)) {
                break;
            }

            const float h0 = bilinear_sample(h, x, y, EdgeMode::Clamp);
            const float gx = 0.5f * (bilinear_sample(h, x + 1.0f, y, EdgeMode::Clamp) - bilinear_sample(h, x - 1.0f, y, EdgeMode::Clamp));
            const float gy = 0.5f * (bilinear_sample(h, x, y + 1.0f, EdgeMode::Clamp) - bilinear_sample(h, x, y - 1.0f, EdgeMode::Clamp));

            dirx = dirx * p.inertia - gx * (1.0f - p.inertia);
            diry = diry * p.inertia - gy * (1.0f - p.inertia);
            const float dlen = std::sqrt(dirx * dirx + diry * diry);
            if (dlen < 1e-5f) {
                dirx = rng.next_f32() * 2.0f - 1.0f;
                diry = rng.next_f32() * 2.0f - 1.0f;
            } else {
                dirx /= dlen;
                diry /= dlen;
            }

            const float nx = x + dirx;
            const float ny = y + diry;
            if (nx < 0.0f || ny < 0.0f || nx >= static_cast<float>(h.width - 1) || ny >= static_cast<float>(h.height - 1)) break;

            const float h1 = bilinear_sample(h, nx, ny, EdgeMode::Clamp);
            const float delta = h1 - h0;

            const float cap = std::max(p.min_slope, -delta) * speed * water * p.capacity;
            if (sediment > cap || delta > 0.0f) {
                const float dep = (delta > 0.0f)
                    ? std::min(sediment, delta)
                    : (sediment - cap) * p.deposition;
                if (dep > 0.0f) {
                    add_brush(h, x, y, dep, radius, EdgeMode::Clamp);
                    sediment -= dep;
                }
            } else {
                const float erode = std::min((cap - sediment) * p.erosion, -delta);
                if (erode > 0.0f) {
                    add_brush(h, x, y, -erode, radius, EdgeMode::Clamp);
                    sediment += erode;
                    if (flow) flow->at(static_cast<int>(x), static_cast<int>(y)) += erode / range;
                }
            }

            speed = std::sqrt(std::max(0.0f, speed * speed + delta * p.gravity));
            water *= (1.0f - p.evaporation);
            x = nx;
            y = ny;
            if (water < 0.01f) break;
        }
    }
}

void thermal_erosion(Heightmap& h, int iterations, float talus, float factor) {
    Heightmap delta(h.width, h.height, 0.0f);
    for (int it = 0; it < iterations; ++it) {
        std::fill(delta.data.begin(), delta.data.end(), 0.0f);
        for (int y = 1; y < h.height - 1; ++y) {
            for (int x = 1; x < h.width - 1; ++x) {
                const float c = h.at(x, y);
                const std::array<std::pair<int, int>, 8> n = {{
                    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
                }};
                for (const auto& [dx, dy] : n) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    const float d = c - h.at(nx, ny);
                    if (d > talus) {
                        const float move = (d - talus) * factor * 0.125f;
                        delta.at(x, y) -= move;
                        delta.at(nx, ny) += move;
                    }
                }
            }
        }
        for (size_t i = 0; i < h.data.size(); ++i) h.data[i] += delta.data[i];
    }
}

[[nodiscard]] int scale_levels(int scale) {
    int levels = 0;
    int s = std::max(1, scale);
    while (s > 1) {
        s >>= 1;
        ++levels;
    }
    return levels;
}

} // namespace

Heightmap::Heightmap(int w, int h, float value) : width(w), height(h), data(static_cast<size_t>(w * h), value) {}

bool Heightmap::empty() const {
    return width <= 0 || height <= 0 || data.empty();
}

float& Heightmap::at(int x, int y) {
    return data[static_cast<size_t>(y * width + x)];
}

const float& Heightmap::at(int x, int y) const {
    return data[static_cast<size_t>(y * width + x)];
}

UpscaleCorrectionParams correction_preset_for_scale(int scale, CorrectionPreset preset) {
    UpscaleCorrectionParams p;
    const int levels = scale_levels(scale);
    p.unsharp_amount_base = std::clamp(0.15f + 0.10f * static_cast<float>(levels), 0.1f, 0.8f);

    switch (preset) {
        case CorrectionPreset::None:
            p.mode = CorrectionMode::None;
            p.enable_unsharp = false;
            p.enable_curvature = false;
            p.enable_residual = false;
            p.enable_guided_sharp = false;
            p.enable_noise = false;
            break;
        case CorrectionPreset::Sharp:
            p.mode = CorrectionMode::Preset;
            p.enable_unsharp = false;
            p.enable_curvature = false;
            p.enable_residual = false;
            p.enable_guided_sharp = true;
            p.enable_noise = false;
            break;
        case CorrectionPreset::RetainDetail:
            p.mode = CorrectionMode::Preset;
            p.enable_unsharp = false;
            p.enable_curvature = false;
            p.enable_residual = true;
            p.enable_guided_sharp = true;
            p.enable_noise = false;
            p.guided_sharpen = 1.08f;
            break;
        case CorrectionPreset::Terrain16x:
            p.mode = CorrectionMode::Preset;
            p.enable_unsharp = false;
            p.enable_curvature = true;
            p.enable_residual = true;
            p.enable_guided_sharp = true;
            p.enable_noise = true;
            p.guided_sharpen = 1.12f;
            break;
    }
    return p;
}

ErosionParams erosion_preset_for_scale(int scale) {
    ErosionParams p;
    if (scale <= 2) {
        p.enable_macro = false;
        p.enable_meso = true;
        p.enable_micro = true;
        p.meso_droplets = 14000;
        p.micro_droplets = 2000;
        p.thermal_iters = 3;
    } else if (scale <= 4) {
        p.enable_macro = true;
        p.enable_meso = true;
        p.enable_micro = true;
        p.macro_droplets = 4000;
        p.meso_droplets = 22000;
        p.micro_droplets = 3000;
        p.thermal_iters = 4;
    } else if (scale <= 8) {
        p.enable_macro = true;
        p.enable_meso = true;
        p.enable_micro = true;
        p.macro_droplets = 6000;
        p.meso_droplets = 28000;
        p.micro_droplets = 4500;
        p.thermal_iters = 5;
    } else {
        p.enable_macro = true;
        p.enable_meso = true;
        p.enable_micro = true;
        p.macro_droplets = 9000;
        p.meso_droplets = 36000;
        p.micro_droplets = 6000;
        p.thermal_iters = 6;
    }
    return p;
}

Heightmap resample(const Heightmap& in, int scale, ResampleMethod method, EdgeMode edge_mode) {
    if (scale <= 1) return in;
    if (in.empty()) return {};
    return resample_to(in, in.width * scale, in.height * scale, method, edge_mode);
}

Heightmap apply_upscale_corrections(
    const Heightmap& upsampled,
    const Heightmap& source,
    int scale,
    const UpscaleCorrectionParams& params,
    uint32_t seed) {
    if (upsampled.empty()) return {};
    if (params.mode == CorrectionMode::None) return upsampled;

    Heightmap out = upsampled;
    const int levels = scale_levels(scale);
    const auto [mn, mx] = min_max(source.empty() ? upsampled : source);
    const float range = std::max(1e-4f, mx - mn);

    const float unsharp_sigma = params.unsharp_sigma_base * static_cast<float>(scale);
    const float unsharp_amount = std::clamp(params.unsharp_amount_base, 0.1f, 0.8f);
    const float resid_gain = params.residual_gain_min +
        (params.residual_gain_max - params.residual_gain_min) * (static_cast<float>(levels) / 4.0f);

    if (params.enable_unsharp || params.mode == CorrectionMode::Unsharp || params.mode == CorrectionMode::Hybrid) {
        const Heightmap blur = gaussian_blur(out, unsharp_sigma, EdgeMode::Clamp);
        for (size_t i = 0; i < out.data.size(); ++i) {
            out.data[i] = out.data[i] + unsharp_amount * (out.data[i] - blur.data[i]);
        }
    }

    if (params.enable_guided_sharp || params.mode == CorrectionMode::GuidedSharp || params.mode == CorrectionMode::Hybrid) {
        const float guided_radius = params.guided_radius_base * static_cast<float>(levels + 1);
        const Heightmap base = guided_like_filter(out, guided_radius, params.guided_sigma * range, EdgeMode::Clamp);
        for (size_t i = 0; i < out.data.size(); ++i) {
            const float detail = out.data[i] - base.data[i];
            out.data[i] = base.data[i] + params.guided_sharpen * detail;
        }
    }

    Heightmap slope;
    Heightmap curvature;
    bool have_slope = false;
    bool have_curv = false;

    if (params.enable_curvature || params.mode == CorrectionMode::CurvatureGain || params.mode == CorrectionMode::Hybrid) {
        curvature = curvature_map(out, EdgeMode::Clamp);
        slope = slope_map(out, EdgeMode::Clamp);
        have_slope = true;
        have_curv = true;
        const float k = params.curvature_gain_base * static_cast<float>(levels) * range;
        for (size_t i = 0; i < out.data.size(); ++i) {
            const float s = slope.data[i] / (range + 1e-6f);
            const float mask = smoothstep(params.slope_lo, params.slope_hi, s);
            out.data[i] += k * curvature.data[i] * mask;
        }
    }

    if ((params.enable_residual || params.mode == CorrectionMode::Residual || params.mode == CorrectionMode::Hybrid) && !source.empty()) {
        const float sigma_src = std::max(0.75f, 0.4f * static_cast<float>(scale));
        const Heightmap low = gaussian_blur(source, sigma_src, EdgeMode::Clamp);
        Heightmap resid(source.width, source.height, 0.0f);
        for (size_t i = 0; i < resid.data.size(); ++i) resid.data[i] = source.data[i] - low.data[i];
        const Heightmap up = resample_to(resid, out.width, out.height, ResampleMethod::Bicubic, EdgeMode::Clamp);
        for (size_t i = 0; i < out.data.size(); ++i) out.data[i] += resid_gain * up.data[i];
    }

    if (params.enable_noise && scale >= 4) {
        if (!have_slope) {
            slope = slope_map(out, EdgeMode::Clamp);
            have_slope = true;
        }
        if (!have_curv) {
            curvature = curvature_map(out, EdgeMode::Clamp);
            have_curv = true;
        }
        const float noise_amp = range * (params.noise_base_amp * static_cast<float>(levels));
        const int octaves = std::max(1, levels);
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                const size_t idx = static_cast<size_t>(y * out.width + x);
                const float sn = slope.data[idx] / (range + 1e-6f);
                const float cn = std::fabs(curvature.data[idx]) / (range + 1e-6f);
                const float m = std::clamp(
                    params.noise_slope_weight * sn + params.noise_curv_weight * cn + params.noise_bias,
                    0.0f,
                    1.0f);
                const float n = fbm_noise(static_cast<float>(x) * 0.02f, static_cast<float>(y) * 0.02f, octaves, seed);
                out.at(x, y) += noise_amp * n * m;
            }
        }
    }

    const float src_mean = std::accumulate(upsampled.data.begin(), upsampled.data.end(), 0.0f) / static_cast<float>(upsampled.data.size());
    const float out_mean = std::accumulate(out.data.begin(), out.data.end(), 0.0f) / static_cast<float>(out.data.size());
    const float shift = out_mean - src_mean;
    const float max_shift = 0.01f * range;
    if (std::fabs(shift) > max_shift) {
        const float corr = shift - (shift > 0.0f ? max_shift : -max_shift);
        for (float& v : out.data) v -= corr;
    }

    return out;
}

Heightmap erode_multiscale(const Heightmap& input, int scale, const ErosionParams& params, uint32_t seed, Heightmap* flow_out) {
    if (input.empty()) return {};
    Heightmap out = input;
    Heightmap flow(input.width, input.height, 0.0f);

    if (params.enable_macro) {
        const int factor = std::clamp(scale / 2, 2, 8);
        const int mw = std::max(2, input.width / factor);
        const int mh = std::max(2, input.height / factor);
        Heightmap macro = resample_to(out, mw, mh, ResampleMethod::Bicubic, EdgeMode::Clamp);

        ErosionParams mp = params;
        mp.radius_base = std::max(1.0f, params.radius_base * std::pow(static_cast<float>(factor), 0.75f));
        mp.max_steps = std::max(10, params.max_steps * factor / 2);
        hydraulic_erosion(macro, nullptr, std::max(1000, params.macro_droplets), mp, seed ^ 0xA511E9B3u);

        const Heightmap macro_up = resample_to(macro, out.width, out.height, ResampleMethod::Bicubic, EdgeMode::Clamp);
        const float blend = std::clamp(0.35f + 0.08f * static_cast<float>(scale_levels(scale)), 0.4f, 0.7f);
        for (size_t i = 0; i < out.data.size(); ++i) {
            out.data[i] = out.data[i] + blend * (macro_up.data[i] - out.data[i]);
        }
    }

    if (params.enable_meso) {
        ErosionParams ep = params;
        ep.radius_base = std::max(1.0f, params.radius_base * std::pow(static_cast<float>(scale), 0.6f));
        ep.max_steps = std::max(20, params.max_steps + scale * 2);
        hydraulic_erosion(out, &flow, std::max(4000, params.meso_droplets), ep, seed ^ 0x517CC1B7u);
    }

    if (params.enable_micro) {
        thermal_erosion(out, params.thermal_iters, params.talus, params.thermal_factor);
        ErosionParams micro = params;
        micro.radius_base = std::max(1.0f, params.radius_base * 0.8f);
        micro.max_steps = std::max(12, params.max_steps / 2);
        hydraulic_erosion(out, &flow, std::max(1000, params.micro_droplets), micro, seed ^ 0x91E10DA5u);
    }

    if (flow_out) *flow_out = std::move(flow);
    return out;
}

PipelineOutputs run_pipeline(const Heightmap& in, const PipelineOptions& opt) {
    if (in.empty()) throw std::invalid_argument("run_pipeline: input heightmap is empty");
    if (opt.scale != 2 && opt.scale != 4 && opt.scale != 8 && opt.scale != 16) {
        throw std::invalid_argument("run_pipeline: scale must be 2, 4, 8, or 16");
    }

    PipelineOutputs out;
    const Heightmap up = resample(in, opt.scale, opt.resample, opt.edge_mode);

    UpscaleCorrectionParams cp = opt.correction;
    if (cp.mode == CorrectionMode::Preset) {
        cp = correction_preset_for_scale(opt.scale, cp.preset);
    }

    const Heightmap corrected = apply_upscale_corrections(up, in, opt.scale, cp, opt.seed);

    Heightmap flow;
    out.out = erode_multiscale(corrected, opt.scale, opt.erosion, opt.seed, opt.dump_flow ? &flow : nullptr);

    if (opt.dump_slope) out.slope = slope_map(out.out, opt.edge_mode);
    if (opt.dump_curvature) out.curvature = curvature_map(out.out, opt.edge_mode);
    if (opt.dump_flow) out.flow = std::move(flow);

    for (float v : out.out.data) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("run_pipeline: non-finite value detected");
        }
    }
    return out;
}

} // namespace armatools::heightpipe
