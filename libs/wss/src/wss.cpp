#include "armatools/wss.h"
#include "armatools/binutil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace armatools::wss {

static std::vector<int16_t> decompress_byte_mono(const std::vector<uint8_t>& data) {
    constexpr double magic = (std::log(10.0) * std::log2(std::exp(1.0))) / 28.12574042515172;
    std::vector<int16_t> out(data.size());
    int16_t last = 0;
    for (size_t i = 0; i < data.size(); i++) {
        auto src = static_cast<int8_t>(data[i]);
        if (src != 0) {
            double af = std::abs(static_cast<double>(src)) * magic;
            double rnd = std::round(af);
            af = std::pow(2.0, af - rnd) * std::pow(2.0, rnd);
            if (src < 0) af *= -1;
            int64_t v = static_cast<int64_t>(std::round(af)) + last;
            v = std::clamp(v, static_cast<int64_t>(std::numeric_limits<int16_t>::min()),
                              static_cast<int64_t>(std::numeric_limits<int16_t>::max()));
            last = static_cast<int16_t>(v);
        }
        out[i] = last;
    }
    return out;
}

static constexpr int16_t pcm_index[15] = {
    -8192, -4096, -2048, -1024, -512, -256, -64, 0, 64, 256, 512, 1024, 2048, 4096, 8192
};

static int16_t clamp_i16(int32_t v) {
    return static_cast<int16_t>(std::clamp(v, static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
                                              static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

static std::vector<int16_t> decompress_nibble_mono(const std::vector<uint8_t>& data) {
    std::vector<int16_t> out;
    out.reserve(data.size() * 2);
    int32_t delta = 0;
    for (uint8_t b : data) {
        int hi = (b >> 4) & 0x0F;
        int lo = b & 0x0F;
        if (hi < 15) delta += pcm_index[hi];
        out.push_back(clamp_i16(delta));
        if (lo < 15) delta += pcm_index[lo];
        out.push_back(clamp_i16(delta));
    }
    return out;
}

static std::vector<uint8_t> samples_to_bytes(const std::vector<int16_t>& samples) {
    std::vector<uint8_t> out(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); i++) {
        auto v = static_cast<uint16_t>(samples[i]);
        out[i * 2] = static_cast<uint8_t>(v & 0xFF);
        out[i * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    return out;
}

using DecompressFunc = std::vector<int16_t>(*)(const std::vector<uint8_t>&);

static std::vector<uint8_t> decompress_channels(const std::vector<uint8_t>& data, int channels,
                                                  DecompressFunc decompress) {
    if (channels <= 1) return samples_to_bytes(decompress(data));

    std::vector<std::vector<uint8_t>> ch_data(static_cast<size_t>(channels));
    for (size_t i = 0; i < data.size(); i++)
        ch_data[i % static_cast<size_t>(channels)].push_back(data[i]);

    std::vector<std::vector<int16_t>> ch_samples(static_cast<size_t>(channels));
    size_t max_len = 0;
    for (int ch = 0; ch < channels; ch++) {
        ch_samples[static_cast<size_t>(ch)] = decompress(ch_data[static_cast<size_t>(ch)]);
        max_len = std::max(max_len, ch_samples[static_cast<size_t>(ch)].size());
    }

    std::vector<uint8_t> out(max_len * static_cast<size_t>(channels) * 2, 0);
    for (size_t i = 0; i < max_len; i++) {
        for (int ch = 0; ch < channels; ch++) {
            auto& cs = ch_samples[static_cast<size_t>(ch)];
            if (i < cs.size()) {
                size_t off = (i * static_cast<size_t>(channels) + static_cast<size_t>(ch)) * 2;
                auto v = static_cast<uint16_t>(cs[i]);
                out[off] = static_cast<uint8_t>(v & 0xFF);
                out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            }
        }
    }
    return out;
}

static AudioData read_wss(std::istream& r) {
    uint32_t compression_raw = binutil::read_u32(r);
    binutil::read_u16(r); // format
    uint16_t channels = binutil::read_u16(r);
    uint32_t sample_rate = binutil::read_u32(r);
    binutil::read_u32(r); // bytes/sec
    binutil::read_u16(r); // block align
    uint16_t bps = binutil::read_u16(r);
    binutil::read_u16(r); // output size

    std::ostringstream buf;
    buf << r.rdbuf();
    std::string s = buf.str();
    std::vector<uint8_t> data(s.begin(), s.end());

    uint32_t compression = compression_raw & 0xFF;
    if (compression == 0 && data.size() % 2 != 0) compression = 4;

    std::vector<uint8_t> pcm;
    std::string format_name;
    switch (compression) {
        case 0: pcm = data; format_name = "PCM"; break;
        case 8: pcm = decompress_channels(data, channels, decompress_byte_mono); format_name = "Delta8"; break;
        case 4: pcm = decompress_channels(data, channels, decompress_nibble_mono); format_name = "Delta4"; break;
        default: throw std::runtime_error(std::format("wss: unsupported compression type {}", compression));
    }

    AudioData ad{sample_rate, channels, bps, format_name, std::move(pcm), 0.0};
    size_t num_samples = ad.pcm.size() / 2;
    if (channels > 0 && sample_rate > 0)
        ad.duration = static_cast<double>(num_samples) / channels / sample_rate;
    return ad;
}

static AudioData read_wav(std::istream& r) {
    binutil::read_u32(r); // file size
    std::string wave = binutil::read_signature(r);
    if (wave != "WAVE") throw std::runtime_error(std::format("wss: expected WAVE, got {}", wave));

    uint16_t audio_format = 0, channels = 0, bps = 0;
    uint32_t sample_rate = 0;
    std::vector<uint8_t> raw_data;
    bool got_fmt = false, got_data = false;

    while (r.peek() != std::char_traits<char>::eof()) {
        std::string chunk_id;
        try { chunk_id = binutil::read_signature(r); } catch (...) { break; }
        uint32_t chunk_size = binutil::read_u32(r);

        if (chunk_id == "fmt ") {
            audio_format = binutil::read_u16(r);
            channels = binutil::read_u16(r);
            sample_rate = binutil::read_u32(r);
            binutil::read_u32(r); binutil::read_u16(r);
            bps = binutil::read_u16(r);
            if (chunk_size > 16) r.seekg(static_cast<std::streamoff>(chunk_size - 16), std::ios::cur);
            got_fmt = true;
        } else if (chunk_id == "data") {
            raw_data = binutil::read_bytes(r, chunk_size);
            got_data = true;
        } else {
            r.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }
        if (chunk_size % 2 != 0) r.seekg(1, std::ios::cur);
    }

    if (!got_fmt) throw std::runtime_error("wss: no fmt chunk");
    if (!got_data) throw std::runtime_error("wss: no data chunk");
    if (audio_format != 1) throw std::runtime_error(std::format("wss: unsupported audio format {}", audio_format));

    std::vector<uint8_t> pcm;
    if (bps == 16) {
        pcm = raw_data;
    } else if (bps == 8) {
        pcm.resize(raw_data.size() * 2);
        for (size_t i = 0; i < raw_data.size(); i++) {
            auto sample = static_cast<int16_t>((static_cast<int16_t>(raw_data[i]) - 128) * 256);
            pcm[i * 2] = static_cast<uint8_t>(static_cast<uint16_t>(sample) & 0xFF);
            pcm[i * 2 + 1] = static_cast<uint8_t>((static_cast<uint16_t>(sample) >> 8) & 0xFF);
        }
    } else {
        throw std::runtime_error(std::format("wss: unsupported PCM bit depth {}", bps));
    }

    AudioData ad{sample_rate, channels, bps, "PCM", std::move(pcm), 0.0};
    size_t num_samples = ad.pcm.size() / 2;
    if (channels > 0 && sample_rate > 0)
        ad.duration = static_cast<double>(num_samples) / channels / sample_rate;
    return ad;
}

AudioData read(std::istream& r) {
    std::string sig = binutil::read_signature(r);
    if (sig == "WSS0") return read_wss(r);
    if (sig == "RIFF") return read_wav(r);
    throw std::runtime_error(std::format("wss: unknown format signature {}", sig));
}

} // namespace armatools::wss
