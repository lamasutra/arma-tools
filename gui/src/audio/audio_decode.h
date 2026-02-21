#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Normalized audio data: always 44100 Hz, 2 channels, s16le interleaved.
struct NormalizedAudio {
    std::vector<int16_t> samples; // interleaved stereo samples
    uint32_t sample_rate = 44100;
    uint16_t channels = 2;

    // Total number of frames (samples.size() / channels).
    size_t frame_count() const {
        return channels > 0 ? samples.size() / channels : 0;
    }

    // Duration in seconds.
    double duration() const {
        return sample_rate > 0 ? static_cast<double>(frame_count()) / sample_rate : 0.0;
    }

    // Total size in bytes of the PCM buffer.
    size_t byte_size() const { return samples.size() * sizeof(int16_t); }
};

// Decode an audio file from disk. Supports OGG, WAV, and WSS.
// Returns normalized 44100/stereo/s16le audio.
NormalizedAudio decode_file(const std::string& path);

// Decode audio from an in-memory buffer.
// ext should include the dot, e.g. ".ogg", ".wss", ".wav".
NormalizedAudio decode_memory(const uint8_t* data, size_t size,
                              const std::string& ext);

// Convert stereo s16le audio to mono float [-1, 1] for analysis.
std::vector<float> mix_to_mono(const NormalizedAudio& audio);
