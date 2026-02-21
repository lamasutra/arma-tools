#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace armatools::ogg {

struct Codebook {
    int entries = 0;
    int dimensions = 0;
    int lookup_type = 0;
};

struct Header {
    int channels = 0;
    int sample_rate = 0;
    std::string encoder;
    std::vector<std::string> comments;
    int floor_type = 0;
    std::vector<Codebook> codebooks;
};

// read_header parses OGG pages to extract Vorbis identification, comment,
// and setup headers.
Header read_header(std::istream& r);

// is_pre_one_encoder returns true if the encoder string matches known
// pre-1.0 Vorbis encoder patterns.
bool is_pre_one_encoder(const std::string& encoder);

// lookup1_values_precision_risk checks whether the float-based lookup1Values
// computation would produce a wrong result for these parameters.
bool lookup1_values_precision_risk(int entries, int dims);

} // namespace armatools::ogg
