#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct SpectrogramData {
    size_t cols = 0;          // number of time columns
    size_t freq_bins = 1024;  // number of frequency bins per column
    std::vector<float> db;    // cols * freq_bins, row-major (col * freq_bins + bin)
    float db_min = -80.0f;
    float db_max = 0.0f;
};

struct SpectrogramImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

// Compute a spectrogram from mono float audio data.
// FFT size: 4096, hop: 256, 1024 log-spaced frequency bins.
SpectrogramData compute_spectrogram(const float* mono, size_t count,
                                     uint32_t sample_rate);

// Render spectrogram data to an RGBA image.
SpectrogramImage render_spectrogram(const SpectrogramData& data);
