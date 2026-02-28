#include "audio_decode.h"
#include "cli_logger.h"
#include "log_panel.h"
#include "cli_logger.h"

#include <armatools/wss.h>
#include "cli_logger.h"
#include <miniaudio.h>
#include "cli_logger.h"
#include <vorbis/vorbisfile.h>
#include "cli_logger.h"

#include <algorithm>
#include "cli_logger.h"
#include <cmath>
#include "cli_logger.h"
#include <cstring>
#include "cli_logger.h"
#include <filesystem>
#include "cli_logger.h"
#include <fstream>
#include "cli_logger.h"
#include <sstream>
#include "cli_logger.h"
#include <stdexcept>
#include "cli_logger.h"

namespace fs = std::filesystem;

static constexpr uint32_t kTargetRate = 44100;
static constexpr uint16_t kTargetChannels = 2;

// ---------------------------------------------------------------------------
// OGG Vorbis decoding via libvorbisfile
// ---------------------------------------------------------------------------

// Decode OGG from a file path using libvorbisfile, normalize to 44100/stereo/s16.
static NormalizedAudio decode_ogg_file(const std::string& path) {
    OggVorbis_File vf;
    int err = ov_fopen(path.c_str(), &vf);
    if (err != 0) {
        std::string msg = "vorbisfile: failed to open '" + path +
                          "' (error " + std::to_string(err) + ")";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        std::string msg = "vorbisfile: failed to get info for '" + path + "'";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    int src_channels = vi->channels;
    long src_rate = vi->rate;

    // Read all PCM as s16le.
    std::vector<int16_t> raw;
    constexpr int kBufSize = 8192;
    char buf[kBufSize];
    int bitstream = 0;

    for (;;) {
        long bytes = ov_read(&vf, buf, kBufSize, /*bigendian=*/0,
                             /*word=*/2, /*sgned=*/1, &bitstream);
        if (bytes <= 0) break;
        auto* samples = reinterpret_cast<const int16_t*>(buf);
        size_t count = static_cast<size_t>(bytes) / 2;
        raw.insert(raw.end(), samples, samples + count);
    }

    ov_clear(&vf);

    if (raw.empty()) {
        std::string msg = "vorbisfile: decoded 0 samples from '" + path + "'";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    size_t src_frames = raw.size() / static_cast<size_t>(src_channels);

    // Resample if needed.
    std::vector<int16_t> resampled;
    auto src_ch = static_cast<size_t>(src_channels);

    if (static_cast<uint32_t>(src_rate) != kTargetRate) {
        double ratio = static_cast<double>(kTargetRate) / static_cast<double>(src_rate);
        auto dst_frames = static_cast<size_t>(
            std::ceil(static_cast<double>(src_frames) * ratio));
        resampled.resize(dst_frames * src_ch);
        for (size_t f = 0; f < dst_frames; ++f) {
            double src_pos = static_cast<double>(f) / ratio;
            auto idx = static_cast<size_t>(src_pos);
            double frac = src_pos - static_cast<double>(idx);
            if (idx + 1 >= src_frames)
                idx = src_frames > 0 ? src_frames - 1 : 0;
            for (size_t c = 0; c < src_ch; ++c) {
                double s0 = raw[idx * src_ch + c];
                double s1 = (idx + 1 < src_frames)
                                ? raw[(idx + 1) * src_ch + c]
                                : s0;
                resampled[f * src_ch + c] =
                    static_cast<int16_t>(std::lround(s0 + frac * (s1 - s0)));
            }
        }
        src_frames = dst_frames;
    } else {
        resampled = std::move(raw);
    }

    // Convert to stereo.
    NormalizedAudio audio;
    audio.sample_rate = kTargetRate;
    audio.channels = kTargetChannels;

    if (src_channels == 1) {
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f];
            audio.samples[f * 2 + 1] = resampled[f];
        }
    } else if (src_channels == 2) {
        audio.samples = std::move(resampled);
    } else {
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f * src_ch];
            audio.samples[f * 2 + 1] =
                src_ch > 1 ? resampled[f * src_ch + 1] : resampled[f * src_ch];
        }
    }

    return audio;
}

// libvorbisfile callbacks for reading from memory.
struct MemOgg {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static size_t mem_read(void* ptr, size_t size, size_t nmemb, void* datasource) {
    auto* m = static_cast<MemOgg*>(datasource);
    size_t bytes = size * nmemb;
    size_t avail = m->size - m->pos;
    if (bytes > avail) bytes = avail;
    std::memcpy(ptr, m->data + m->pos, bytes);
    m->pos += bytes;
    return bytes / size;
}

static int mem_seek(void* datasource, ogg_int64_t offset, int whence) {
    auto* m = static_cast<MemOgg*>(datasource);
    size_t new_pos;
    switch (whence) {
    case SEEK_SET: new_pos = static_cast<size_t>(offset); break;
    case SEEK_CUR: new_pos = m->pos + static_cast<size_t>(offset); break;
    case SEEK_END: new_pos = m->size + static_cast<size_t>(offset); break;
    default: return -1;
    }
    if (new_pos > m->size) return -1;
    m->pos = new_pos;
    return 0;
}

static long mem_tell(void* datasource) {
    auto* m = static_cast<MemOgg*>(datasource);
    return static_cast<long>(m->pos);
}

static int mem_close(void* /*datasource*/) { return 0; }

static const ov_callbacks kMemCallbacks = {mem_read, mem_seek, mem_close, mem_tell};

// Decode OGG from memory using libvorbisfile.
static NormalizedAudio decode_ogg_memory(const uint8_t* data, size_t size) {
    MemOgg mem{data, size, 0};
    OggVorbis_File vf;
    int err = ov_open_callbacks(&mem, &vf, nullptr, 0, kMemCallbacks);
    if (err != 0) {
        std::string msg = "vorbisfile: failed to open memory buffer (error " +
                          std::to_string(err) + ")";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        std::string msg = "vorbisfile: failed to get info from memory buffer";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    int src_channels = vi->channels;
    long src_rate = vi->rate;

    std::vector<int16_t> raw;
    constexpr int kBufSize = 8192;
    char buf[kBufSize];
    int bitstream = 0;

    for (;;) {
        long bytes = ov_read(&vf, buf, kBufSize, 0, 2, 1, &bitstream);
        if (bytes <= 0) break;
        auto* samples = reinterpret_cast<const int16_t*>(buf);
        size_t count = static_cast<size_t>(bytes) / 2;
        raw.insert(raw.end(), samples, samples + count);
    }

    ov_clear(&vf);

    if (raw.empty()) {
        std::string msg = "vorbisfile: decoded 0 samples from memory";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    auto src_ch = static_cast<size_t>(src_channels);
    size_t src_frames = raw.size() / src_ch;

    // Resample if needed.
    std::vector<int16_t> resampled;
    if (static_cast<uint32_t>(src_rate) != kTargetRate) {
        double ratio = static_cast<double>(kTargetRate) / static_cast<double>(src_rate);
        auto dst_frames = static_cast<size_t>(
            std::ceil(static_cast<double>(src_frames) * ratio));
        resampled.resize(dst_frames * src_ch);
        for (size_t f = 0; f < dst_frames; ++f) {
            double src_pos = static_cast<double>(f) / ratio;
            auto idx = static_cast<size_t>(src_pos);
            double frac = src_pos - static_cast<double>(idx);
            if (idx + 1 >= src_frames)
                idx = src_frames > 0 ? src_frames - 1 : 0;
            for (size_t c = 0; c < src_ch; ++c) {
                double s0 = raw[idx * src_ch + c];
                double s1 = (idx + 1 < src_frames)
                                ? raw[(idx + 1) * src_ch + c]
                                : s0;
                resampled[f * src_ch + c] =
                    static_cast<int16_t>(std::lround(s0 + frac * (s1 - s0)));
            }
        }
        src_frames = dst_frames;
    } else {
        resampled = std::move(raw);
    }

    // Convert to stereo.
    NormalizedAudio audio;
    audio.sample_rate = kTargetRate;
    audio.channels = kTargetChannels;

    if (src_channels == 1) {
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f];
            audio.samples[f * 2 + 1] = resampled[f];
        }
    } else if (src_channels == 2) {
        audio.samples = std::move(resampled);
    } else {
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f * src_ch];
            audio.samples[f * 2 + 1] =
                src_ch > 1 ? resampled[f * src_ch + 1] : resampled[f * src_ch];
        }
    }

    return audio;
}

// ---------------------------------------------------------------------------
// WAV decoding via miniaudio (standard WAV only — WSS uses armatools::wss)
// ---------------------------------------------------------------------------

// Read all decoded frames from an initialized ma_decoder into a NormalizedAudio.
static NormalizedAudio read_all_frames(ma_decoder& decoder) {
    NormalizedAudio audio;
    audio.sample_rate = kTargetRate;
    audio.channels = kTargetChannels;

    constexpr size_t kChunkFrames = 8192;
    std::vector<int16_t> chunk(kChunkFrames * kTargetChannels);

    for (;;) {
        ma_uint64 frames_read = 0;
        ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(),
                                                      kChunkFrames, &frames_read);
        if (frames_read == 0) break;
        audio.samples.insert(audio.samples.end(), chunk.begin(),
                             chunk.begin() +
                                 static_cast<ptrdiff_t>(frames_read * kTargetChannels));
        if (result != MA_SUCCESS) break;
    }

    return audio;
}

// Decode a WAV file from disk using miniaudio.
static NormalizedAudio decode_wav_file(const std::string& path) {
    ma_decoder_config config = ma_decoder_config_init(
        ma_format_s16, kTargetChannels, kTargetRate);

    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(path.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        std::string msg = "miniaudio: failed to decode WAV '" + path +
                          "' (" + ma_result_description(result) + ")";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    auto audio = read_all_frames(decoder);
    ma_decoder_uninit(&decoder);
    return audio;
}

// ---------------------------------------------------------------------------
// WSS / Bohemia WAV decoding via armatools::wss
// ---------------------------------------------------------------------------

// Decode WSS using armatools::wss, then normalize to 44100/stereo.
static NormalizedAudio decode_wss(std::istream& stream) {
    auto wss = armatools::wss::read(stream);
    if (wss.pcm.empty() || wss.bits_per_sample != 16) {
        std::string msg = "WSS: unsupported format or empty PCM (bits=" +
                          std::to_string(wss.bits_per_sample) + ", pcm_size=" +
                          std::to_string(wss.pcm.size()) + ")";
        LOGE(msg);
        throw std::runtime_error(msg);
    }

    // Convert raw bytes to int16_t samples.
    size_t num_samples = wss.pcm.size() / 2;
    std::vector<int16_t> raw(num_samples);
    for (size_t i = 0; i < num_samples; ++i) {
        raw[i] = static_cast<int16_t>(
            static_cast<uint16_t>(wss.pcm[i * 2]) |
            (static_cast<uint16_t>(wss.pcm[i * 2 + 1]) << 8));
    }

    size_t src_channels = wss.channels;
    uint32_t src_rate = wss.sample_rate;
    size_t src_frames = src_channels > 0 ? num_samples / src_channels : 0;

    // Resample if needed (simple linear interpolation).
    std::vector<int16_t> resampled;
    if (src_rate != kTargetRate) {
        double ratio = static_cast<double>(kTargetRate) / static_cast<double>(src_rate);
        auto dst_frames = static_cast<size_t>(std::ceil(static_cast<double>(src_frames) * ratio));
        resampled.resize(dst_frames * src_channels);
        for (size_t f = 0; f < dst_frames; ++f) {
            double src_pos = static_cast<double>(f) / ratio;
            auto idx = static_cast<size_t>(src_pos);
            double frac = src_pos - static_cast<double>(idx);
            if (idx + 1 >= src_frames) idx = src_frames > 0 ? src_frames - 1 : 0;
            for (size_t c = 0; c < src_channels; ++c) {
                double s0 = raw[idx * src_channels + c];
                double s1 = (idx + 1 < src_frames)
                                ? raw[(idx + 1) * src_channels + c]
                                : s0;
                auto val = static_cast<int16_t>(std::lround(s0 + frac * (s1 - s0)));
                resampled[f * src_channels + c] = val;
            }
        }
        src_frames = dst_frames;
    } else {
        resampled = std::move(raw);
    }

    // Convert to stereo if mono.
    NormalizedAudio audio;
    audio.sample_rate = kTargetRate;
    audio.channels = kTargetChannels;

    if (src_channels == 1) {
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f];
            audio.samples[f * 2 + 1] = resampled[f];
        }
    } else if (src_channels == 2) {
        audio.samples = std::move(resampled);
    } else {
        // Downmix to stereo: take first two channels.
        audio.samples.resize(src_frames * 2);
        for (size_t f = 0; f < src_frames; ++f) {
            audio.samples[f * 2] = resampled[f * src_channels];
            audio.samples[f * 2 + 1] =
                src_channels > 1 ? resampled[f * src_channels + 1]
                                 : resampled[f * src_channels];
        }
    }

    return audio;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NormalizedAudio decode_file(const std::string& path) {
    auto ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".ogg") {
        return decode_ogg_file(path);
    }

    if (ext == ".wss") {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            LOGE("Cannot open file: " + path);
            throw std::runtime_error("Cannot open file: " + path);
        }
        return decode_wss(f);
    }

    // .wav — try armatools::wss first (handles both standard WAV and Bohemia
    // compressed variants), fall back to miniaudio for non-Arma WAV files.
    if (ext == ".wav") {
        try {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) {
                LOGE("Cannot open file: " + path);
                throw std::runtime_error("Cannot open file: " + path);
            }
            return decode_wss(f);
        } catch (...) {
            LOGD("armatools::wss failed for WAV, trying miniaudio");
            return decode_wav_file(path);
        }
    }

    // Unknown extension — try miniaudio.
    return decode_wav_file(path);
}

NormalizedAudio decode_memory(const uint8_t* data, size_t size,
                              const std::string& ext) {
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

    if (lower_ext == ".ogg") {
        return decode_ogg_memory(data, size);
    }

    // WSS and WAV both go through armatools::wss (handles standard WAV and
    // Bohemia's compressed WSS variants).
    if (lower_ext == ".wss" || lower_ext == ".wav") {
        std::string str(reinterpret_cast<const char*>(data), size);
        std::istringstream stream(str);
        return decode_wss(stream);
    }

    // Unknown — try OGG first (most common in PBOs), then give up.
    try {
        return decode_ogg_memory(data, size);
    } catch (...) {
        std::string msg = "Unsupported audio format: " + ext;
        LOGE(msg);
        throw std::runtime_error(msg);
    }
}

std::vector<float> mix_to_mono(const NormalizedAudio& audio) {
    size_t frames = audio.frame_count();
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float left = audio.samples[i * 2] / 32768.0f;
        float right = audio.samples[i * 2 + 1] / 32768.0f;
        mono[i] = (left + right) * 0.5f;
    }
    return mono;
}
