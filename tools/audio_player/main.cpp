// audio_player - plays OGG, WAV, and WSS (Bohemia proprietary) audio files
// Uses miniaudio for playback and OGG/WAV decoding, armatools::wss for WSS.

#include "armatools/wss.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static std::atomic<bool> g_stop{false};

static void signal_handler(int) {
    g_stop.store(true);
}

// All supported miniaudio backends and their CLI names
struct BackendEntry {
    const char* name;
    ma_backend backend;
};

static const BackendEntry g_all_backends[] = {
    {"pulseaudio", ma_backend_pulseaudio},
    {"alsa",       ma_backend_alsa},
    {"wasapi",     ma_backend_wasapi},
    {"coreaudio",  ma_backend_coreaudio},
    {"jack",       ma_backend_jack},
    {"null",       ma_backend_null},
};

static constexpr size_t g_all_backends_count = sizeof(g_all_backends) / sizeof(g_all_backends[0]);

struct PcmPlayback {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static void pcm_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* pb = static_cast<PcmPlayback*>(device->pUserData);
    size_t bytes_per_frame = static_cast<size_t>(device->playback.channels) * ma_get_bytes_per_sample(device->playback.format);
    size_t bytes_needed = frame_count * bytes_per_frame;
    size_t bytes_available = pb->size - pb->pos;
    size_t to_copy = std::min(bytes_needed, bytes_available);

    std::memcpy(output, pb->data + pb->pos, to_copy);
    if (to_copy < bytes_needed)
        std::memset(static_cast<uint8_t*>(output) + to_copy, 0, bytes_needed - to_copy);
    pb->pos += to_copy;
}

static int play_pcm(const armatools::wss::AudioData& ad, ma_context* ctx, const ma_device_id* dev_id) {
    PcmPlayback pb{ad.pcm.data(), ad.pcm.size(), 0};

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = ad.channels;
    config.sampleRate = ad.sample_rate;
    config.dataCallback = pcm_callback;
    config.pUserData = &pb;
    if (dev_id)
        config.playback.pDeviceID = dev_id;

    ma_device device;
    if (ma_device_init(ctx, &config, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "Error: failed to initialize audio device\n");
        return 1;
    }

    std::fprintf(stderr, "Backend:     %s\n", ma_get_backend_name(ctx->backend));
    std::fprintf(stderr, "Device:      %s\n", device.playback.name);

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        std::fprintf(stderr, "Error: failed to start audio device\n");
        return 1;
    }

    std::fprintf(stderr, "Playing... (Ctrl+C to stop)\n");
    while (!g_stop.load() && pb.pos < pb.size)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ma_device_uninit(&device);
    std::fprintf(stderr, g_stop.load() ? "\nStopped.\n" : "\nDone.\n");
    return 0;
}

static void decoder_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* dec = static_cast<ma_decoder*>(device->pUserData);
    ma_decoder_read_pcm_frames(dec, output, frame_count, nullptr);
}

static int play_file(const std::string& path, ma_context* ctx, const ma_device_id* dev_id) {
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), nullptr, &decoder) != MA_SUCCESS) {
        std::fprintf(stderr, "Error: failed to decode %s\n", path.c_str());
        return 1;
    }

    auto ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const char* format_name = "Unknown";
    if (ext == ".ogg") format_name = "OGG Vorbis";
    else if (ext == ".wav") format_name = "WAV PCM";
    else if (ext == ".mp3") format_name = "MP3";
    else if (ext == ".flac") format_name = "FLAC";

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
    double duration = (decoder.outputSampleRate > 0)
        ? static_cast<double>(total_frames) / decoder.outputSampleRate : 0.0;

    std::fprintf(stderr, "Format:      %s\n", format_name);
    std::fprintf(stderr, "Sample rate: %u Hz\n", decoder.outputSampleRate);
    std::fprintf(stderr, "Channels:    %u\n", decoder.outputChannels);
    std::fprintf(stderr, "Duration:    %.2f s\n", duration);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = decoder.outputFormat;
    config.playback.channels = decoder.outputChannels;
    config.sampleRate = decoder.outputSampleRate;
    config.dataCallback = decoder_callback;
    config.pUserData = &decoder;
    if (dev_id)
        config.playback.pDeviceID = dev_id;

    ma_device device;
    if (ma_device_init(ctx, &config, &device) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        std::fprintf(stderr, "Error: failed to initialize audio device\n");
        return 1;
    }

    std::fprintf(stderr, "Backend:     %s\n", ma_get_backend_name(ctx->backend));
    std::fprintf(stderr, "Device:      %s\n", device.playback.name);

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        std::fprintf(stderr, "Error: failed to start audio device\n");
        return 1;
    }

    std::fprintf(stderr, "Playing... (Ctrl+C to stop)\n");
    ma_uint64 cursor;
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ma_decoder_get_cursor_in_pcm_frames(&decoder, &cursor);
        if (total_frames > 0 && cursor >= total_frames) break;
    }

    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);
    std::fprintf(stderr, g_stop.load() ? "\nStopped.\n" : "\nDone.\n");
    return 0;
}

static int play_wss(const std::string& path, ma_context* ctx, const ma_device_id* dev_id) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "Error: cannot open %s\n", path.c_str());
        return 1;
    }
    try {
        auto ad = armatools::wss::read(f);
        std::fprintf(stderr, "Format:      %s\n", ad.format.c_str());
        std::fprintf(stderr, "Sample rate: %u Hz\n", ad.sample_rate);
        std::fprintf(stderr, "Channels:    %u\n", ad.channels);
        std::fprintf(stderr, "Duration:    %.2f s\n", ad.duration);
        return play_pcm(ad, ctx, dev_id);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}

static void print_usage() {
    std::fprintf(stderr,
        "Usage: audio_player [options] <input.ogg|.wss|.wav>\n"
        "\n"
        "Options:\n"
        "  --backend <name>   Force audio backend (pulse, alsa, wasapi, coreaudio, jack, null, default)\n"
        "  --device <name>    Select output device by name substring match\n"
        "  --list-backends    List available audio backends and exit\n"
        "  --list-devices     List available playback devices and exit\n"
        "  --help             Show this help message\n"
    );
}

static int list_backends() {
    std::fprintf(stdout, "Available audio backends:\n");
    for (size_t i = 0; i < g_all_backends_count; ++i) {
        if (ma_is_backend_enabled(g_all_backends[i].backend))
            std::fprintf(stdout, "  %s\n", g_all_backends[i].name);
    }
    return 0;
}

static int list_devices(ma_context* ctx) {
    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;

    if (ma_context_get_devices(ctx, &playback_devices, &playback_count, nullptr, nullptr) != MA_SUCCESS) {
        std::fprintf(stderr, "Error: failed to enumerate devices\n");
        return 1;
    }

    std::fprintf(stdout, "Playback devices (backend: %s):\n", ma_get_backend_name(ctx->backend));
    for (ma_uint32 i = 0; i < playback_count; ++i) {
        std::fprintf(stdout, "  [%u] %s%s\n", i, playback_devices[i].name,
                     playback_devices[i].isDefault ? " (default)" : "");
    }
    return 0;
}

// Find a device by name substring match; returns true if found
static bool find_device(ma_context* ctx, const std::string& name, ma_device_id* out_id) {
    ma_device_info* playback_devices = nullptr;
    ma_uint32 playback_count = 0;

    if (ma_context_get_devices(ctx, &playback_devices, &playback_count, nullptr, nullptr) != MA_SUCCESS)
        return false;

    // Case-insensitive substring search
    std::string needle = name;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (ma_uint32 i = 0; i < playback_count; ++i) {
        std::string dev_name = playback_devices[i].name;
        std::transform(dev_name.begin(), dev_name.end(), dev_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (dev_name.find(needle) != std::string::npos) {
            *out_id = playback_devices[i].id;
            std::fprintf(stderr, "Matched device: %s\n", playback_devices[i].name);
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    // Parse arguments
    std::string backend_name;
    std::string device_name;
    std::string file_path;
    bool do_list_backends = false;
    bool do_list_devices = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--list-backends") {
            do_list_backends = true;
        } else if (arg == "--list-devices") {
            do_list_devices = true;
        } else if (arg == "--backend" && i + 1 < argc) {
            backend_name = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            device_name = argv[++i];
        } else if (arg[0] != '-') {
            file_path = arg;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage();
            return 1;
        }
    }

    // Handle --list-backends (no context needed)
    if (do_list_backends)
        return list_backends();

    // Determine backend list for context init
    ma_backend backends[1];
    ma_uint32 backend_count = 0;
    ma_backend* backends_ptr = nullptr;

    if (!backend_name.empty() && backend_name != "default") {
        bool found = false;
        for (size_t i = 0; i < g_all_backends_count; ++i) {
            // Allow short aliases: "pulse" matches "pulseaudio"
            std::string full_name = g_all_backends[i].name;
            if (full_name == backend_name || full_name.find(backend_name) == 0) {
                backends[0] = g_all_backends[i].backend;
                backend_count = 1;
                backends_ptr = backends;
                found = true;
                break;
            }
        }
        if (!found) {
            std::fprintf(stderr, "Error: unknown backend '%s'\n", backend_name.c_str());
            std::fprintf(stderr, "Use --list-backends to see available backends.\n");
            return 1;
        }
    }

    // Initialize context
    ma_context ctx;
    ma_context_config ctx_config = ma_context_config_init();
    if (ma_context_init(backends_ptr, backend_count, &ctx_config, &ctx) != MA_SUCCESS) {
        std::fprintf(stderr, "Error: failed to initialize audio context");
        if (!backend_name.empty())
            std::fprintf(stderr, " (backend: %s)", backend_name.c_str());
        std::fprintf(stderr, "\n");
        return 1;
    }

    // Handle --list-devices
    if (do_list_devices) {
        int rc = list_devices(&ctx);
        ma_context_uninit(&ctx);
        return rc;
    }

    // Resolve device if --device was given
    ma_device_id selected_device_id;
    const ma_device_id* dev_id_ptr = nullptr;
    if (!device_name.empty()) {
        if (!find_device(&ctx, device_name, &selected_device_id)) {
            std::fprintf(stderr, "Error: no device matching '%s'\n", device_name.c_str());
            std::fprintf(stderr, "Use --list-devices to see available devices.\n");
            ma_context_uninit(&ctx);
            return 1;
        }
        dev_id_ptr = &selected_device_id;
    }

    // Need a file for playback
    if (file_path.empty()) {
        std::fprintf(stderr, "Error: no input file specified\n");
        print_usage();
        ma_context_uninit(&ctx);
        return 1;
    }

    std::signal(SIGINT, signal_handler);

    auto ext = fs::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int rc;

    // WSS files always use our custom decoder
    if (ext == ".wss") {
        rc = play_wss(file_path, &ctx, dev_id_ptr);
    } else if (ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac") {
        // Known audio formats: use miniaudio's built-in decoders
        rc = play_file(file_path, &ctx, dev_id_ptr);
    } else {
        // Unknown extension: check magic bytes
        std::ifstream f(file_path, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "Error: cannot open %s\n", file_path.c_str());
            ma_context_uninit(&ctx);
            return 1;
        }
        char magic[4]{};
        f.read(magic, 4);
        f.close();

        if (std::memcmp(magic, "WSS0", 4) == 0)
            rc = play_wss(file_path, &ctx, dev_id_ptr);
        else
            rc = play_file(file_path, &ctx, dev_id_ptr);
    }

    ma_context_uninit(&ctx);
    return rc;
}
