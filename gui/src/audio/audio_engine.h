#pragma once

#include "audio_decode.h"

#include <miniaudio.h>

#include <atomic>
#include <cstddef>

enum class PlayState { Stopped, Playing, Paused };

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Load normalized audio data for playback.
    void load(NormalizedAudio audio);

    void play();
    void pause();
    void stop();

    // Seek to a fractional position [0.0, 1.0].
    void seek(double fraction);

    // Current playback progress as a fraction [0.0, 1.0].
    double progress() const;

    PlayState state() const { return state_.load(); }
    bool has_audio() const { return !audio_.samples.empty(); }
    const NormalizedAudio& audio() const { return audio_; }

private:
    NormalizedAudio audio_;
    std::atomic<size_t> play_pos_{0};  // byte offset into samples buffer
    std::atomic<PlayState> state_{PlayState::Stopped};

    ma_device device_;
    bool device_inited_ = false;

    void init_device();
    void uninit_device();

    static void data_callback(ma_device* device, void* output,
                               const void* input, ma_uint32 frame_count);
};
