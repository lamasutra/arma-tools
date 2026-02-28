#pragma once

#include "audio_decode.h"

#include <miniaudio.h>

#include <atomic>
#include <cstddef>

// Playback states for the audio engine.
enum class PlayState {
    Stopped, // No audio is loaded or has reached the end.
    Playing, // Audio is actively streaming to the audio device.
    Paused,  // Audio is loaded and can be resumed with play().
};

// AudioEngine drives real-time audio playback using the miniaudio library.
//
// Usage:
//   1. Call load() with a NormalizedAudio (decoded by audio_decode.h).
//   2. Call play() to start streaming to the system audio device.
//   3. Use progress() to update a playback position UI element.
//   4. Call seek() / pause() / stop() as needed.
//
// Threading:
//   miniaudio calls data_callback() on a dedicated audio thread to fill the
//   hardware buffer.  play_pos_ and state_ use std::atomic so they can be
//   written from the GTK main thread and read from the audio thread safely.
//   Do NOT call any method from inside data_callback().
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
