#include "armatools/lzo.h"
#include "armatools/binutil.h"

#include <format>
#include <stdexcept>

namespace armatools::lzo {

static constexpr size_t m2_max_offset = 0x0800;

// PeekReader wraps an istream to support single-byte peek and lookahead.
class PeekReader {
public:
    explicit PeekReader(std::istream& r) : r_(r) {}

    uint8_t read_byte() {
        if (buffed_ > 0) {
            uint8_t b = buf_[0];
            for (int i = 1; i < buffed_; i++)
                buf_[i - 1] = buf_[i];
            buffed_--;
            return b;
        }
        char c;
        if (!r_.read(&c, 1))
            throw std::runtime_error("lzo: unexpected end of stream");
        return static_cast<uint8_t>(c);
    }

    uint8_t peek_byte_at(int offset) {
        int need = offset + 1;
        while (buffed_ < need) {
            char c;
            if (!r_.read(&c, 1))
                throw std::runtime_error("lzo: unexpected end of stream (peek)");
            buf_[buffed_++] = static_cast<uint8_t>(c);
        }
        return buf_[offset];
    }

private:
    std::istream& r_;
    uint8_t buf_[8]{};
    int buffed_ = 0;
};

class LzoDecoder {
public:
    LzoDecoder(std::istream& r, size_t expected_size)
        : pr_(r), out_(expected_size), size_(expected_size) {}

    std::vector<uint8_t> run();

private:
    PeekReader pr_;
    std::vector<uint8_t> out_;
    size_t op_ = 0;
    size_t size_;
    uint8_t hist_[2]{};
    int hist_idx_ = 0;

    uint8_t read() {
        uint8_t b = pr_.read_byte();
        hist_[hist_idx_ & 1] = b;
        hist_idx_++;
        return b;
    }

    uint8_t ip_minus_2() const { return hist_[hist_idx_ & 1]; }
    uint8_t peek() { return pr_.peek_byte_at(0); }
    uint8_t peek_at(int offset) { return pr_.peek_byte_at(offset); }
    size_t remaining() const { return size_ - op_; }

    void copy_literals(size_t n) {
        if (remaining() < n)
            throw std::runtime_error(
                std::format("lzo: output overrun copying {} literals (remaining={})",
                            n, remaining()));
        for (size_t i = 0; i < n; i++)
            out_[op_++] = read();
    }

    void copy_back(size_t m_pos, size_t length) {
        if (m_pos >= op_)
            throw std::runtime_error(
                std::format("lzo: lookbehind overrun (mPos={}, op={})", m_pos, op_));
        if (remaining() < length)
            throw std::runtime_error(
                std::format("lzo: output overrun in copyBack (need={}, remaining={})",
                            length, remaining()));
        for (size_t i = 0; i < length; i++) {
            out_[op_] = out_[m_pos];
            op_++;
            m_pos++;
        }
    }

    size_t read_run_length(size_t step) {
        size_t t = 0;
        while (peek() == 0) {
            read();
            t += step;
        }
        return t + read();
    }

    // Returns next tag for match loop
    size_t do_match_done();
    size_t do_b3_inline();
    size_t do_m2(size_t t);
    size_t do_m3(size_t t);

    // Returns {done, next_t}
    struct M4Result { bool done; size_t next_t; };
    M4Result do_m4(size_t t);

    size_t do_short_m1(size_t t);
    std::vector<uint8_t> handle_b5_and_loop(size_t t);
    std::vector<uint8_t> match_loop(size_t t);
};

size_t LzoDecoder::do_match_done() {
    size_t t = ip_minus_2() & 3;
    if (t == 0) return do_b3_inline();

    copy_literals(t);
    return read();
}

size_t LzoDecoder::do_b3_inline() {
    size_t t = read();
    if (t >= 16) return t;

    if (t == 0) t = 15 + read_run_length(255);
    copy_literals(t + 3);

    size_t tag = read();
    if (tag >= 16) return tag;

    // B_5: M1 short match after first literal run
    size_t m_pos = op_ - (1 + m2_max_offset);
    m_pos -= tag >> 2;
    m_pos -= static_cast<size_t>(read()) << 2;
    copy_back(m_pos, 3);
    return do_match_done();
}

size_t LzoDecoder::do_m2(size_t t) {
    size_t m_pos = op_ - 1;
    m_pos -= (t >> 2) & 7;
    m_pos -= static_cast<size_t>(read()) << 3;
    size_t match_len = ((t >> 5) - 1) + 2;
    copy_back(m_pos, match_len);
    return do_match_done();
}

size_t LzoDecoder::do_m3(size_t t) {
    t &= 31;
    if (t == 0) t = 31 + read_run_length(255);

    uint8_t b0 = peek_at(0);
    uint8_t b1 = peek_at(1);
    size_t m_pos = op_ - 1 - ((b0 >> 2) + (static_cast<size_t>(b1) << 6));
    read();
    read();
    copy_back(m_pos, t + 2);
    return do_match_done();
}

LzoDecoder::M4Result LzoDecoder::do_m4(size_t t) {
    size_t m_pos = op_;
    m_pos -= (t & 8) << 11;

    t &= 7;
    if (t == 0) t = 7 + read_run_length(255);

    uint8_t b0 = peek_at(0);
    uint8_t b1 = peek_at(1);
    m_pos -= (b0 >> 2) + (static_cast<size_t>(b1) << 6);
    read();
    read();

    if (m_pos == op_) {
        if (op_ != size_)
            throw std::runtime_error(
                std::format("lzo: output underrun (op={}, expected={})", op_, size_));
        return {true, 0};
    }
    m_pos -= 0x4000;
    copy_back(m_pos, t + 2);
    return {false, do_match_done()};
}

size_t LzoDecoder::do_short_m1(size_t t) {
    size_t m_pos = op_ - 1;
    m_pos -= t >> 2;
    m_pos -= static_cast<size_t>(read()) << 2;
    copy_back(m_pos, 2);
    return do_match_done();
}

std::vector<uint8_t> LzoDecoder::handle_b5_and_loop(size_t t) {
    size_t m_pos = op_ - (1 + m2_max_offset);
    m_pos -= t >> 2;
    m_pos -= static_cast<size_t>(read()) << 2;
    copy_back(m_pos, 3);
    return match_loop(do_match_done());
}

std::vector<uint8_t> LzoDecoder::match_loop(size_t t) {
    for (;;) {
        if (t >= 64) {
            t = do_m2(t);
        } else if (t >= 32) {
            t = do_m3(t);
        } else if (t >= 16) {
            auto [done, nt] = do_m4(t);
            if (done) return std::move(out_);
            t = nt;
        } else {
            t = do_short_m1(t);
        }
    }
}

std::vector<uint8_t> LzoDecoder::run() {
    uint8_t first = peek();

    if (first > 17) {
        size_t t = read() - 17;
        if (t < 4) {
            copy_literals(t);
            size_t tag = read();
            return match_loop(tag);
        }
        copy_literals(t);
        size_t tag = read();
        if (tag >= 16) return match_loop(tag);
        return handle_b5_and_loop(tag);
    }

    size_t t = read();
    if (t >= 16) return match_loop(t);

    if (t == 0) t = 15 + read_run_length(255);
    copy_literals(t + 3);

    size_t tag = read();
    if (tag >= 16) return match_loop(tag);
    return handle_b5_and_loop(tag);
}

std::vector<uint8_t> decompress(std::istream& r, size_t expected_size) {
    LzoDecoder decoder(r, expected_size);
    return decoder.run();
}

std::vector<uint8_t> decompress_or_raw(std::istream& r, size_t expected_size) {
    if (expected_size < 1024)
        return binutil::read_bytes(r, expected_size);
    return decompress(r, expected_size);
}

} // namespace armatools::lzo
