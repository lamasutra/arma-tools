#include "audio_engine.h"
#include "log_panel.h"

#include <algorithm>
#include <cstring>
#include <string>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    uninit_device();
}

void AudioEngine::init_device() {
    if (device_inited_) return;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = audio_.channels;
    config.sampleRate = audio_.sample_rate;
    config.dataCallback = data_callback;
    config.pUserData = this;

    ma_result result = ma_device_init(nullptr, &config, &device_);
    if (result != MA_SUCCESS) {
        app_log(LogLevel::Error,
                "miniaudio: failed to init device (" +
                    std::to_string(result) + " " +
                    ma_result_description(result) + ")");
        return;
    }
    device_inited_ = true;
}

void AudioEngine::uninit_device() {
    if (!device_inited_) return;
    ma_device_stop(&device_);
    ma_device_uninit(&device_);
    device_inited_ = false;
}

void AudioEngine::load(NormalizedAudio audio) {
    uninit_device();
    audio_ = std::move(audio);
    play_pos_.store(0);
    state_.store(PlayState::Stopped);
    init_device();
}

void AudioEngine::play() {
    if (!device_inited_ || audio_.samples.empty()) return;

    // If stopped (at end or reset), restart from current position.
    if (state_.load() == PlayState::Stopped) {
        // If at the end, restart from beginning.
        if (play_pos_.load() >= audio_.byte_size()) {
            play_pos_.store(0);
        }
    }

    state_.store(PlayState::Playing);
    ma_result result = ma_device_start(&device_);
    if (result != MA_SUCCESS) {
        state_.store(PlayState::Stopped);
        app_log(LogLevel::Error,
                "miniaudio: failed to start playback (" +
                    std::to_string(result) + " " +
                    ma_result_description(result) + ")");
    }
}

void AudioEngine::pause() {
    if (state_.load() == PlayState::Playing) {
        state_.store(PlayState::Paused);
        ma_device_stop(&device_);
    }
}

void AudioEngine::stop() {
    state_.store(PlayState::Stopped);
    if (device_inited_) {
        ma_device_stop(&device_);
    }
    play_pos_.store(0);
}

void AudioEngine::seek(double fraction) {
    if (audio_.samples.empty()) return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    size_t total = audio_.byte_size();
    size_t frame_size = audio_.channels * sizeof(int16_t);
    auto pos = static_cast<size_t>(fraction * static_cast<double>(total));
    // Align to frame boundary.
    pos = (pos / frame_size) * frame_size;
    play_pos_.store(pos);
}

double AudioEngine::progress() const {
    size_t total = audio_.byte_size();
    if (total == 0) return 0.0;
    size_t pos = play_pos_.load();
    return static_cast<double>(pos) / static_cast<double>(total);
}

void AudioEngine::data_callback(ma_device* device, void* output,
                                 const void* /*input*/,
                                 ma_uint32 frame_count) {
    auto* engine = static_cast<AudioEngine*>(device->pUserData);
    if (engine->state_.load() != PlayState::Playing) {
        std::memset(output, 0,
                    frame_count * device->playback.channels * sizeof(int16_t));
        return;
    }

    size_t total_bytes = engine->audio_.byte_size();
    size_t bytes_needed = frame_count * device->playback.channels * sizeof(int16_t);
    size_t pos = engine->play_pos_.load();

    auto* out = static_cast<uint8_t*>(output);
    auto* src = reinterpret_cast<const uint8_t*>(engine->audio_.samples.data());

    if (pos >= total_bytes) {
        std::memset(out, 0, bytes_needed);
        engine->state_.store(PlayState::Stopped);
        return;
    }

    size_t available = total_bytes - pos;
    size_t to_copy = std::min(bytes_needed, available);
    std::memcpy(out, src + pos, to_copy);

    if (to_copy < bytes_needed) {
        std::memset(out + to_copy, 0, bytes_needed - to_copy);
        engine->play_pos_.store(total_bytes);
        engine->state_.store(PlayState::Stopped);
    } else {
        engine->play_pos_.store(pos + to_copy);
    }
}
