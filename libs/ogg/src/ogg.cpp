#include "armatools/ogg.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace armatools::ogg {

// --- Bit reader (LSB first, Vorbis bit packing) ---
class BitReader {
public:
    explicit BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    std::pair<uint32_t, bool> read_bits(int n) {
        if (n == 0) return {0, true};
        if (n > 32) return {0, false};
        uint32_t result = 0;
        for (int i = 0; i < n; i++) {
            size_t byte_idx = pos_ / 8;
            int bit_idx = pos_ % 8;
            if (byte_idx >= size_) return {0, false};
            if (data_[byte_idx] & (1 << bit_idx)) result |= (1u << i);
            pos_++;
        }
        return {result, true};
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

static int ilog(uint32_t v) {
    int n = 0;
    while (v > 0) { n++; v >>= 1; }
    return n;
}

static int64_t int_pow(int base, int exp) {
    int64_t result = 1;
    for (int i = 0; i < exp; i++) {
        result *= base;
        if (result < 0) return std::numeric_limits<int64_t>::max();
    }
    return result;
}

static int lookup1_values(int entries, int dims) {
    if (dims == 0 || entries == 0) return 0;
    int r = static_cast<int>(std::floor(std::pow(static_cast<double>(entries), 1.0 / dims)));
    while (int_pow(r + 1, dims) <= entries) r++;
    while (r > 0 && int_pow(r, dims) > entries) r--;
    return r;
}

// --- OGG page parsing ---

struct OggPage {
    std::vector<uint8_t> segment_table;
    std::vector<uint8_t> body;
    bool continued = false;
};

static OggPage read_ogg_page(std::istream& r) {
    uint8_t hdr[27];
    if (!r.read(reinterpret_cast<char*>(hdr), 27))
        throw std::runtime_error("ogg: reading page header");
    if (std::memcmp(hdr, "OggS", 4) != 0)
        throw std::runtime_error("ogg: invalid capture pattern");

    OggPage page;
    page.continued = (hdr[5] & 0x01) != 0;
    int n_segments = hdr[26];

    page.segment_table.resize(static_cast<size_t>(n_segments));
    if (!r.read(reinterpret_cast<char*>(page.segment_table.data()), n_segments))
        throw std::runtime_error("ogg: reading segment table");

    size_t body_size = 0;
    for (auto s : page.segment_table) body_size += s;

    page.body.resize(body_size);
    if (body_size > 0 && !r.read(reinterpret_cast<char*>(page.body.data()),
                                  static_cast<std::streamsize>(body_size)))
        throw std::runtime_error("ogg: reading page body");

    return page;
}

static std::vector<std::vector<uint8_t>> extract_packets(const OggPage& page) {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> current;
    size_t offset = 0;

    for (auto seg : page.segment_table) {
        size_t size = seg;
        current.insert(current.end(), page.body.begin() + static_cast<ptrdiff_t>(offset),
                       page.body.begin() + static_cast<ptrdiff_t>(offset + size));
        offset += size;
        if (size < 255) {
            packets.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) packets.push_back(std::move(current));
    return packets;
}

static void parse_comment_header(const uint8_t* data, size_t len, Header& h) {
    if (len < 4) return;
    uint32_t vendor_len = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    data += 4; len -= 4;
    if (len < vendor_len) return;
    h.encoder = std::string(reinterpret_cast<const char*>(data), vendor_len);
    data += vendor_len; len -= vendor_len;
    if (len < 4) return;
    uint32_t comment_count = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    data += 4; len -= 4;
    for (uint32_t i = 0; i < comment_count && len >= 4; i++) {
        uint32_t clen = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        data += 4; len -= 4;
        if (len < clen) break;
        h.comments.emplace_back(reinterpret_cast<const char*>(data), clen);
        data += clen; len -= clen;
    }
}

static Codebook parse_codebook(BitReader& br, bool& ok) {
    Codebook cb;
    auto [sync, sok] = br.read_bits(24);
    if (!sok || sync != 0x564342) { ok = false; return cb; }
    auto [dims, dok] = br.read_bits(16);
    if (!dok) { ok = false; return cb; }
    cb.dimensions = static_cast<int>(dims);
    auto [entries, eok] = br.read_bits(24);
    if (!eok) { ok = false; return cb; }
    cb.entries = static_cast<int>(entries);

    auto [ordered, ook] = br.read_bits(1);
    if (!ook) { ok = false; return cb; }

    if (ordered == 0) {
        auto [sparse, spok] = br.read_bits(1);
        if (!spok) { ok = false; return cb; }
        for (int i = 0; i < static_cast<int>(entries); i++) {
            if (sparse == 1) {
                auto [flag, fok] = br.read_bits(1);
                if (!fok) { ok = false; return cb; }
                if (flag == 1) br.read_bits(5);
            } else {
                br.read_bits(5);
            }
        }
    } else {
        br.read_bits(5);
        int current_entry = 0;
        while (current_entry < static_cast<int>(entries)) {
            int bits_needed = ilog(static_cast<uint32_t>(static_cast<int>(entries) - current_entry));
            auto [num, nok] = br.read_bits(bits_needed);
            if (!nok) { ok = false; return cb; }
            current_entry += static_cast<int>(num);
        }
    }

    auto [lt, ltok] = br.read_bits(4);
    if (!ltok) { ok = false; return cb; }
    cb.lookup_type = static_cast<int>(lt);

    if (lt == 1 || lt == 2) {
        br.read_bits(32); br.read_bits(32);
        auto [vbits, vok] = br.read_bits(4);
        if (!vok) { ok = false; return cb; }
        br.read_bits(1);
        int lv = (lt == 1) ? lookup1_values(static_cast<int>(entries), static_cast<int>(dims))
                           : static_cast<int>(entries) * static_cast<int>(dims);
        for (int i = 0; i < lv; i++) br.read_bits(static_cast<int>(vbits) + 1);
    }

    ok = true;
    return cb;
}

static void skip_floor0_config(BitReader& br) {
    br.read_bits(8); br.read_bits(16); br.read_bits(16);
    br.read_bits(6); br.read_bits(8);
    auto [nb, ok] = br.read_bits(4);
    if (!ok) return;
    for (int i = 0; i < static_cast<int>(nb) + 1; i++) br.read_bits(8);
}

static void skip_floor1_config(BitReader& br) {
    auto [partitions, pok] = br.read_bits(5);
    if (!pok) return;
    int max_class = -1;
    std::vector<int> classes(static_cast<size_t>(partitions));
    for (int i = 0; i < static_cast<int>(partitions); i++) {
        auto [c, cok] = br.read_bits(4);
        if (!cok) return;
        classes[static_cast<size_t>(i)] = static_cast<int>(c);
        if (static_cast<int>(c) > max_class) max_class = static_cast<int>(c);
    }
    std::vector<int> class_dims(static_cast<size_t>(max_class + 1));
    for (int i = 0; i <= max_class; i++) {
        auto [d, dok] = br.read_bits(3);
        if (!dok) return;
        class_dims[static_cast<size_t>(i)] = static_cast<int>(d) + 1;
        auto [sub, sok] = br.read_bits(2);
        if (!sok) return;
        if (sub > 0) br.read_bits(8);
        for (int j = 0; j < (1 << sub); j++) br.read_bits(8);
    }
    br.read_bits(2);
    auto [rb, rbok] = br.read_bits(4);
    if (!rbok) return;
    for (int i = 0; i < static_cast<int>(partitions); i++) {
        for (int j = 0; j < class_dims[static_cast<size_t>(classes[static_cast<size_t>(i)])]; j++)
            br.read_bits(static_cast<int>(rb));
    }
}

static void parse_setup_header(const uint8_t* data, size_t len, Header& h) {
    BitReader br(data, len);
    auto [cb_count_raw, ok] = br.read_bits(8);
    if (!ok) return;
    int cb_count = static_cast<int>(cb_count_raw) + 1;

    for (int i = 0; i < cb_count; i++) {
        bool cbok = false;
        auto cb = parse_codebook(br, cbok);
        if (!cbok) return;
        h.codebooks.push_back(cb);
    }

    auto [td_count, tdok] = br.read_bits(6);
    if (!tdok) return;
    for (int i = 0; i < static_cast<int>(td_count) + 1; i++) br.read_bits(16);

    auto [floor_count, fok] = br.read_bits(6);
    if (!fok) return;
    for (int i = 0; i < static_cast<int>(floor_count) + 1; i++) {
        auto [ft, ftok] = br.read_bits(16);
        if (!ftok) return;
        if (static_cast<int>(ft) > h.floor_type) h.floor_type = static_cast<int>(ft);
        if (ft == 0) skip_floor0_config(br);
        else if (ft == 1) skip_floor1_config(br);
        else return;
    }
}

Header read_header(std::istream& r) {
    std::vector<std::vector<uint8_t>> packets;
    while (packets.size() < 3) {
        auto page = read_ogg_page(r);
        auto pkts = extract_packets(page);
        for (auto& p : pkts) packets.push_back(std::move(p));
    }

    if (packets[0].size() < 30 || packets[0][0] != 1 ||
        std::memcmp(packets[0].data() + 1, "vorbis", 6) != 0)
        throw std::runtime_error("ogg: not a Vorbis identification header");

    Header h;
    h.channels = packets[0][11];
    h.sample_rate = static_cast<int>(packets[0][12]) | (static_cast<int>(packets[0][13]) << 8) |
                    (static_cast<int>(packets[0][14]) << 16) | (static_cast<int>(packets[0][15]) << 24);

    if (packets[1].size() < 7 || packets[1][0] != 3 ||
        std::memcmp(packets[1].data() + 1, "vorbis", 6) != 0)
        throw std::runtime_error("ogg: not a Vorbis comment header");
    parse_comment_header(packets[1].data() + 7, packets[1].size() - 7, h);

    if (packets[2].size() >= 7 && packets[2][0] == 5 &&
        std::memcmp(packets[2].data() + 1, "vorbis", 6) == 0)
        parse_setup_header(packets[2].data() + 7, packets[2].size() - 7, h);

    return h;
}

bool is_pre_one_encoder(const std::string& encoder) {
    std::string lower = encoder;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("beta") != std::string::npos) return true;
    if (lower.find("xiphophorus") != std::string::npos) return true;
    for (const char* d : {"20000508", "20001031", "20010110", "20010225",
                          "20010615", "20010813", "20011007", "20011231", "20020717"}) {
        if (encoder.find(d) != std::string::npos) return true;
    }
    return false;
}

bool lookup1_values_precision_risk(int entries, int dims) {
    if (dims == 0 || entries == 0) return false;
    int float_result = static_cast<int>(std::floor(std::pow(static_cast<double>(entries), 1.0 / dims)));
    int int_result = lookup1_values(entries, dims);
    return float_result != int_result;
}

} // namespace armatools::ogg
