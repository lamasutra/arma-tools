#include "spectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static constexpr size_t kFFTSize = 4096;
static constexpr size_t kHop = 256;
static constexpr size_t kFreqBins = 1024;
static constexpr float kMinFreq = 20.0f;

// In-place radix-2 Cooley-Tukey FFT.
// real[] and imag[] must have n elements, n must be a power of 2.
static void fft(float* real, float* imag, size_t n) {
    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    // Butterfly stages.
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        float wreal = std::cos(angle);
        float wimag = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            float cur_r = 1.0f, cur_i = 0.0f;
            for (size_t j = 0; j < len / 2; ++j) {
                size_t u = i + j;
                size_t v = i + j + len / 2;
                float tr = cur_r * real[v] - cur_i * imag[v];
                float ti = cur_r * imag[v] + cur_i * real[v];
                real[v] = real[u] - tr;
                imag[v] = imag[u] - ti;
                real[u] += tr;
                imag[u] += ti;
                float new_r = cur_r * wreal - cur_i * wimag;
                float new_i = cur_r * wimag + cur_i * wreal;
                cur_r = new_r;
                cur_i = new_i;
            }
        }
    }
}

SpectrogramData compute_spectrogram(const float* mono, size_t count,
                                     uint32_t sample_rate) {
    SpectrogramData data;
    if (count < kFFTSize) return data;

    float nyquist = static_cast<float>(sample_rate) / 2.0f;

    // Precompute Hann window.
    std::vector<float> window(kFFTSize);
    for (size_t i = 0; i < kFFTSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f *
                                              static_cast<float>(i) /
                                              static_cast<float>(kFFTSize)));
    }

    // Precompute log-spaced frequency bin edges → FFT bin indices.
    float log_min = std::log(kMinFreq);
    float log_max = std::log(nyquist);
    std::vector<size_t> bin_edges(kFreqBins + 1);
    for (size_t i = 0; i <= kFreqBins; ++i) {
        float freq = std::exp(log_min + static_cast<float>(i) /
                                            static_cast<float>(kFreqBins) *
                                            (log_max - log_min));
        auto fft_bin = static_cast<size_t>(freq / nyquist * (kFFTSize / 2));
        bin_edges[i] = std::min(fft_bin, kFFTSize / 2);
    }

    size_t cols = (count - kFFTSize) / kHop + 1;
    data.cols = cols;
    data.freq_bins = kFreqBins;
    data.db.resize(cols * kFreqBins);

    std::vector<float> real(kFFTSize);
    std::vector<float> imag(kFFTSize);
    std::vector<float> magnitude(kFFTSize / 2 + 1);

    for (size_t col = 0; col < cols; ++col) {
        size_t offset = col * kHop;

        // Apply window and fill FFT input.
        for (size_t i = 0; i < kFFTSize; ++i) {
            real[i] = mono[offset + i] * window[i];
            imag[i] = 0.0f;
        }

        fft(real.data(), imag.data(), kFFTSize);

        // Compute magnitudes.
        for (size_t i = 0; i <= kFFTSize / 2; ++i) {
            magnitude[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        }

        // Map to log-spaced frequency bins.
        for (size_t b = 0; b < kFreqBins; ++b) {
            size_t lo = bin_edges[b];
            size_t hi = bin_edges[b + 1];
            if (hi <= lo) hi = lo + 1;
            if (hi > kFFTSize / 2 + 1) hi = kFFTSize / 2 + 1;

            float sum = 0.0f;
            for (size_t i = lo; i < hi; ++i) sum += magnitude[i];
            float avg = sum / static_cast<float>(hi - lo);

            float db_val = (avg > 1e-10f) ? 20.0f * std::log10(avg / static_cast<float>(kFFTSize)) : -80.0f;
            data.db[col * kFreqBins + b] = std::clamp(db_val, -80.0f, 0.0f);
        }
    }

    return data;
}

// 7-stop gradient: black → dark blue → purple → red → orange → yellow → white
// mapped from -80 dB to 0 dB.
static void db_to_color(float db, uint8_t& r, uint8_t& g, uint8_t& b) {
    float t = (db + 80.0f) / 80.0f; // 0..1
    t = std::clamp(t, 0.0f, 1.0f);

    struct Stop { float pos; uint8_t r, g, b; };
    static constexpr Stop stops[] = {
        {0.0f / 6.0f, 0, 0, 0},         // black
        {1.0f / 6.0f, 0, 0, 128},       // dark blue
        {2.0f / 6.0f, 128, 0, 128},     // purple
        {3.0f / 6.0f, 255, 0, 0},       // red
        {4.0f / 6.0f, 255, 165, 0},     // orange
        {5.0f / 6.0f, 255, 255, 0},     // yellow
        {6.0f / 6.0f, 255, 255, 255},   // white
    };

    size_t i = 0;
    for (i = 0; i < 6; ++i) {
        if (t <= stops[i + 1].pos) break;
    }
    if (i >= 6) i = 5;

    float local = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    local = std::clamp(local, 0.0f, 1.0f);

    r = static_cast<uint8_t>(stops[i].r + local * (stops[i + 1].r - stops[i].r));
    g = static_cast<uint8_t>(stops[i].g + local * (stops[i + 1].g - stops[i].g));
    b = static_cast<uint8_t>(stops[i].b + local * (stops[i + 1].b - stops[i].b));
}

SpectrogramImage render_spectrogram(const SpectrogramData& data) {
    SpectrogramImage img;
    if (data.cols == 0 || data.freq_bins == 0) return img;

    img.width = static_cast<int>(data.cols);
    img.height = static_cast<int>(data.freq_bins);
    img.rgba.resize(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4);

    for (size_t col = 0; col < data.cols; ++col) {
        for (size_t bin = 0; bin < data.freq_bins; ++bin) {
            float db = data.db[col * data.freq_bins + bin];
            // Flip vertically: high frequencies at top.
            size_t y = data.freq_bins - 1 - bin;
            size_t idx = (y * data.cols + col) * 4;
            db_to_color(db, img.rgba[idx], img.rgba[idx + 1], img.rgba[idx + 2]);
            img.rgba[idx + 3] = 255;
        }
    }

    return img;
}
