#include "procedural_texture.h"

#include <armatools/armapath.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace procedural_texture {
namespace {

static std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static std::vector<std::string> split_csv(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

static bool parse_int(const std::string& s, int& out) {
    auto t = trim(s);
    if (t.empty()) return false;
    int v = 0;
    auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc{} || ptr != t.data() + t.size()) return false;
    out = v;
    return true;
}

static bool parse_float(const std::string& s, float& out) {
    auto t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtof(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

static uint8_t to_u8(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(v * 255.0f));
}

static armatools::paa::Image make_solid(int w, int h, const std::array<uint8_t, 4>& c) {
    armatools::paa::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
    for (size_t i = 0; i < img.pixels.size(); i += 4) {
        img.pixels[i + 0] = c[0];
        img.pixels[i + 1] = c[1];
        img.pixels[i + 2] = c[2];
        img.pixels[i + 3] = c[3];
    }
    return img;
}

static armatools::paa::Image make_checker(int w, int h,
                                          const std::array<uint8_t, 4>& a,
                                          const std::array<uint8_t, 4>& b) {
    armatools::paa::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
    const int tile = std::max(2, std::min(w, h) / 8);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool sel = ((x / tile) + (y / tile)) % 2 == 0;
            const auto& c = sel ? a : b;
            size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4U;
            img.pixels[off + 0] = c[0];
            img.pixels[off + 1] = c[1];
            img.pixels[off + 2] = c[2];
            img.pixels[off + 3] = c[3];
        }
    }
    return img;
}

static uint32_t fnv1a(std::string_view s) {
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

static armatools::paa::Image make_noise(int w, int h, std::string_view seed) {
    armatools::paa::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
    const uint32_t base = fnv1a(seed);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t v = base ^ (static_cast<uint32_t>(x) * 374761393u)
                              ^ (static_cast<uint32_t>(y) * 668265263u);
            v ^= (v >> 13);
            v *= 1274126177u;
            uint8_t g = static_cast<uint8_t>((v >> 24) & 0xFF);
            size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4U;
            img.pixels[off + 0] = g;
            img.pixels[off + 1] = g;
            img.pixels[off + 2] = g;
            img.pixels[off + 3] = 255;
        }
    }
    return img;
}

static armatools::paa::Image make_fresnel(int w, int h,
                                          const std::array<uint8_t, 4>& base,
                                          float bias, float power, float scale) {
    armatools::paa::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4U);
    const float inv_w = w > 1 ? 1.0f / static_cast<float>(w - 1) : 0.0f;
    const float inv_h = h > 1 ? 1.0f / static_cast<float>(h - 1) : 0.0f;
    const float p = std::max(0.01f, power);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float nx = static_cast<float>(x) * inv_w * 2.0f - 1.0f;
            const float ny = static_cast<float>(y) * inv_h * 2.0f - 1.0f;
            const float r = std::sqrt(nx * nx + ny * ny);
            const float edge = std::pow(std::clamp(r, 0.0f, 1.0f), p);
            const float fr = std::clamp(bias + edge * scale, 0.0f, 1.0f);
            size_t off = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4U;
            img.pixels[off + 0] = to_u8((static_cast<float>(base[0]) / 255.0f) * fr);
            img.pixels[off + 1] = to_u8((static_cast<float>(base[1]) / 255.0f) * fr);
            img.pixels[off + 2] = to_u8((static_cast<float>(base[2]) / 255.0f) * fr);
            img.pixels[off + 3] = base[3];
        }
    }
    return img;
}

static std::optional<std::vector<std::string>> parse_args(const std::string& expr,
                                                          std::string_view op) {
    const std::string needle = std::string(op) + "(";
    auto pos = expr.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    auto end = expr.find(')', pos + needle.size());
    if (end == std::string::npos || end <= pos + needle.size()) return std::nullopt;
    return split_csv(std::string_view(expr).substr(pos + needle.size(),
                                                   end - (pos + needle.size())));
}

static void apply_alpha(armatools::paa::Image& img, float alpha_mul) {
    const uint8_t mul = to_u8(alpha_mul);
    for (size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
        uint16_t a = static_cast<uint16_t>(img.pixels[i + 3]) * static_cast<uint16_t>(mul);
        img.pixels[i + 3] = static_cast<uint8_t>(a / 255U);
    }
}

} // namespace

std::optional<armatools::paa::Image> generate(const std::string& expression,
                                              const std::string& semantic_hint) {
    if (!armatools::armapath::is_procedural_texture(expression))
        return std::nullopt;

    std::string expr = armatools::armapath::to_slash_lower(expression);
    std::string hint = armatools::armapath::to_slash_lower(semantic_hint);

    int w = 64;
    int h = 64;
    auto close = expr.find(')');
    if (close != std::string::npos && close > 2) {
        auto parts = split_csv(std::string_view(expr).substr(2, close - 2));
        if (parts.size() >= 3) {
            int pw = 0, ph = 0;
            if (parse_int(parts[1], pw)) w = std::clamp(pw, 1, 1024);
            if (parse_int(parts[2], ph)) h = std::clamp(ph, 1, 1024);
        }
    }

    std::array<uint8_t, 4> base = {255, 255, 255, 255};
    auto col_pos = expr.find("color(");
    if (col_pos != std::string::npos) {
        auto end = expr.find(')', col_pos);
        if (end != std::string::npos && end > col_pos + 6) {
            auto cols = split_csv(std::string_view(expr).substr(col_pos + 6, end - (col_pos + 6)));
            float c[4] = {1.f, 1.f, 1.f, 1.f};
            for (size_t i = 0; i < cols.size() && i < 4; ++i) parse_float(cols[i], c[i]);
            base = {to_u8(c[0]), to_u8(c[1]), to_u8(c[2]), to_u8(c[3])};
        }
    }

    if (hint.find("normal") != std::string::npos || expr.find("normal") != std::string::npos) {
        if (auto args = parse_args(expr, "normal")) {
            float nx = 0.0f, ny = 0.0f, nz = 1.0f;
            if (args->size() > 0) parse_float((*args)[0], nx);
            if (args->size() > 1) parse_float((*args)[1], ny);
            if (args->size() > 2) parse_float((*args)[2], nz);
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len; ny /= len; nz /= len;
            } else {
                nx = 0.0f; ny = 0.0f; nz = 1.0f;
            }
            return make_solid(w, h, {
                to_u8(nx * 0.5f + 0.5f),
                to_u8(ny * 0.5f + 0.5f),
                to_u8(nz * 0.5f + 0.5f),
                255});
        }
        return make_solid(w, h, {128, 128, 255, 255});
    }
    if (expr.find("fresnel(") != std::string::npos) {
        float bias = 0.04f;
        float power = 5.0f;
        float scale = 1.0f;
        if (auto args = parse_args(expr, "fresnel")) {
            if (args->size() > 0) parse_float((*args)[0], bias);
            if (args->size() > 1) parse_float((*args)[1], power);
            if (args->size() > 2) parse_float((*args)[2], scale);
        }
        auto img = make_fresnel(w, h, base, bias, power, scale);
        if (auto aa = parse_args(expr, "alpha")) {
            float a = 1.0f;
            if (!aa->empty()) parse_float((*aa)[0], a);
            apply_alpha(img, std::clamp(a, 0.0f, 1.0f));
        }
        return img;
    }
    if (expr.find("checker") != std::string::npos) {
        std::array<uint8_t, 4> inv = {
            static_cast<uint8_t>(255 - base[0]),
            static_cast<uint8_t>(255 - base[1]),
            static_cast<uint8_t>(255 - base[2]),
            base[3]};
        return make_checker(w, h, base, inv);
    }
    if (expr.find("noise") != std::string::npos || expr.find("random") != std::string::npos) {
        auto img = make_noise(w, h, expr);
        if (auto aa = parse_args(expr, "alpha")) {
            float a = 1.0f;
            if (!aa->empty()) parse_float((*aa)[0], a);
            apply_alpha(img, std::clamp(a, 0.0f, 1.0f));
        }
        return img;
    }
    if (col_pos != std::string::npos) {
        auto img = make_solid(w, h, base);
        if (auto aa = parse_args(expr, "alpha")) {
            float a = 1.0f;
            if (!aa->empty()) parse_float((*aa)[0], a);
            apply_alpha(img, std::clamp(a, 0.0f, 1.0f));
        }
        return img;
    }
    // Generic fallback for unsupported procedural forms.
    return make_checker(w, h, {180, 50, 180, 255}, {80, 20, 80, 255});
}

} // namespace procedural_texture
