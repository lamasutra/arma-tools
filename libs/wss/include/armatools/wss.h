#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace armatools::wss {

struct AudioData {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    std::string format; // "PCM", "Delta8", "Delta4"
    std::vector<uint8_t> pcm; // 16-bit signed LE, interleaved
    double duration = 0.0;
};

// read parses a WSS (Bohemia proprietary) or standard RIFF WAVE file.
AudioData read(std::istream& r);

} // namespace armatools::wss
