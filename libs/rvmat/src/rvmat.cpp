#include "armatools/rvmat.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace armatools::rvmat {

namespace {

static std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool iequals(const std::string& a, const std::string& b) {
    return to_lower_ascii(a) == to_lower_ascii(b);
}

static const config::NamedEntry* find_entry_ci(const config::ConfigClass& cls,
                                               const std::string& name) {
    for (const auto& ne : cls.entries) {
        if (iequals(ne.name, name)) return &ne;
    }
    return nullptr;
}

static std::string get_string(const config::ConfigClass& cls, const std::string& name) {
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return "";
    if (auto* s = std::get_if<config::StringEntry>(&ne->entry)) return s->value;
    return "";
}

static std::string get_value_as_string(const config::ConfigClass& cls, const std::string& name) {
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return "";
    if (auto* s = std::get_if<config::StringEntry>(&ne->entry)) return s->value;
    if (auto* i = std::get_if<config::IntEntry>(&ne->entry)) return std::to_string(i->value);
    if (auto* f = std::get_if<config::FloatEntry>(&ne->entry)) return std::to_string(f->value);
    return "";
}

static float get_number(const config::ConfigClass& cls, const std::string& name) {
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return 0.0f;
    if (auto* f = std::get_if<config::FloatEntry>(&ne->entry)) return f->value;
    if (auto* i = std::get_if<config::IntEntry>(&ne->entry)) return static_cast<float>(i->value);
    return 0.0f;
}

static bool array_elem_to_float(const config::ArrayElement& e, float& out) {
    if (auto* f = std::get_if<config::FloatElement>(&e)) {
        out = f->value;
        return true;
    }
    if (auto* i = std::get_if<config::IntElement>(&e)) {
        out = static_cast<float>(i->value);
        return true;
    }
    return false;
}

static std::array<float, 4> get_rgba(const config::ConfigClass& cls,
                                     const std::string& name) {
    std::array<float, 4> out{0.0f, 0.0f, 0.0f, 0.0f};
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return out;
    auto* arr = std::get_if<config::ArrayEntry>(&ne->entry);
    if (!arr) return out;
    for (size_t i = 0; i < out.size() && i < arr->elements.size(); ++i) {
        float v = 0.0f;
        if (array_elem_to_float(arr->elements[i], v)) out[i] = v;
    }
    return out;
}

static std::vector<std::string> get_string_list(const config::ConfigClass& cls,
                                                const std::string& name) {
    std::vector<std::string> out;
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return out;
    auto* arr = std::get_if<config::ArrayEntry>(&ne->entry);
    if (!arr) return out;
    for (const auto& e : arr->elements) {
        if (auto* s = std::get_if<config::StringElement>(&e)) out.push_back(s->value);
    }
    return out;
}

static std::array<float, 3> get_vec3(const config::ConfigClass& cls,
                                     const std::string& name,
                                     const std::array<float, 3>& fallback) {
    auto* ne = find_entry_ci(cls, name);
    if (!ne) return fallback;
    auto* arr = std::get_if<config::ArrayEntry>(&ne->entry);
    if (!arr) return fallback;
    std::array<float, 3> out = fallback;
    for (size_t i = 0; i < out.size() && i < arr->elements.size(); ++i) {
        float v = 0.0f;
        if (array_elem_to_float(arr->elements[i], v)) out[i] = v;
    }
    return out;
}

static bool parse_uv_transform_from_array(const config::ArrayEntry& arr, UVTransform& out) {
    if (arr.elements.size() < 6) return false;
    auto read = [&](size_t idx, float& v) -> bool {
        if (idx >= arr.elements.size()) return false;
        return array_elem_to_float(arr.elements[idx], v);
    };
    std::array<float, 3> aside{1.0f, 0.0f, 0.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    std::array<float, 3> dir{0.0f, 0.0f, 0.0f};
    std::array<float, 3> pos{0.0f, 0.0f, 0.0f};

    if (arr.elements.size() >= 12) {
        for (size_t i = 0; i < 3; ++i) read(i, aside[i]);
        for (size_t i = 0; i < 3; ++i) read(3 + i, up[i]);
        for (size_t i = 0; i < 3; ++i) read(6 + i, dir[i]);
        for (size_t i = 0; i < 3; ++i) read(9 + i, pos[i]);
    } else if (arr.elements.size() >= 9) {
        for (size_t i = 0; i < 3; ++i) read(i, aside[i]);
        for (size_t i = 0; i < 3; ++i) read(3 + i, up[i]);
        for (size_t i = 0; i < 3; ++i) read(6 + i, pos[i]);
    } else {
        for (size_t i = 0; i < 3; ++i) read(i, aside[i]);
        for (size_t i = 0; i < 3; ++i) read(3 + i, up[i]);
    }
    out.aside = aside;
    out.up = up;
    out.dir = dir;
    out.pos = pos;
    out.valid = true;
    return true;
}

static bool parse_uv_transform_from_class(const config::ConfigClass& cls, UVTransform& out) {
    auto aside = get_vec3(cls, "aside", out.aside);
    auto up = get_vec3(cls, "up", out.up);
    auto dir = get_vec3(cls, "dir", out.dir);
    auto pos = get_vec3(cls, "pos", out.pos);
    out.aside = aside;
    out.up = up;
    out.dir = dir;
    out.pos = pos;
    out.valid = true;
    return true;
}

static int parse_class_number(const std::string& class_name, const std::string& prefix) {
    auto lower = to_lower_ascii(class_name);
    auto lp = to_lower_ascii(prefix);
    if (!lower.starts_with(lp)) return -1;
    auto tail = lower.substr(lp.size());
    if (tail.empty()) return -1;
    for (char c : tail) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    }
    return std::stoi(tail);
}

} // namespace

Material parse(const config::Config& cfg) {
    Material mat;
    const auto& root = cfg.root;

    mat.pixel_shader = get_string(root, "PixelShaderID");
    mat.vertex_shader = get_string(root, "VertexShaderID");
    mat.ambient = get_rgba(root, "ambient");
    mat.diffuse = get_rgba(root, "diffuse");
    mat.forced_diffuse = get_rgba(root, "forcedDiffuse");
    mat.emissive = get_rgba(root, "emmisive"); // BI typo in original format
    mat.specular = get_rgba(root, "specular");
    mat.specular_power = get_number(root, "specularPower");
    mat.render_flags = get_string_list(root, "renderFlags");
    mat.main_light = get_string(root, "mainLight");
    mat.fog_mode = get_string(root, "fogMode");
    mat.surface = get_string(root, "surfaceInfo");

    for (const auto& ne : root.entries) {
        auto* ce = std::get_if<config::ClassEntryOwned>(&ne.entry);
        if (!ce || !ce->cls || ce->cls->external || ce->cls->deletion) continue;

        int stage_number = parse_class_number(ne.name, "stage");
        int texgen_number = parse_class_number(ne.name, "texgen");

        if (stage_number >= 0) {
            TextureStage st;
            st.stage_number = stage_number;
            st.class_name = ne.name;
            st.texture_path = get_string(*ce->cls, "texture");
            st.uv_source = get_string(*ce->cls, "uvSource");
            st.filter = get_string(*ce->cls, "filter");
            st.tex_gen = get_value_as_string(*ce->cls, "texGen");
            if (auto* uv = find_entry_ci(*ce->cls, "uvTransform")) {
                if (auto* arr = std::get_if<config::ArrayEntry>(&uv->entry)) {
                    parse_uv_transform_from_array(*arr, st.uv_transform);
                } else if (auto* uv_cls = std::get_if<config::ClassEntryOwned>(&uv->entry)) {
                    if (uv_cls->cls) parse_uv_transform_from_class(*uv_cls->cls, st.uv_transform);
                }
            }
            mat.stages.push_back(std::move(st));
        } else if (texgen_number >= 0) {
            TexGen tg;
            tg.index = texgen_number;
            tg.class_name = ne.name;
            tg.uv_source = get_string(*ce->cls, "uvSource");
            if (auto* uv = find_entry_ci(*ce->cls, "uvTransform")) {
                if (auto* arr = std::get_if<config::ArrayEntry>(&uv->entry)) {
                    parse_uv_transform_from_array(*arr, tg.uv_transform);
                } else if (auto* uv_cls = std::get_if<config::ClassEntryOwned>(&uv->entry)) {
                    if (uv_cls->cls) parse_uv_transform_from_class(*uv_cls->cls, tg.uv_transform);
                }
            }
            mat.tex_gens.push_back(std::move(tg));
        }
    }

    std::sort(mat.stages.begin(), mat.stages.end(),
              [](const TextureStage& a, const TextureStage& b) {
                  if (a.stage_number != b.stage_number)
                      return a.stage_number < b.stage_number;
                  return a.class_name < b.class_name;
              });

    std::sort(mat.tex_gens.begin(), mat.tex_gens.end(),
              [](const TexGen& a, const TexGen& b) {
                  if (a.index != b.index)
                      return a.index < b.index;
                  return a.class_name < b.class_name;
              });

    return mat;
}

Material parse(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("rvmat: cannot open file");

    std::ostringstream buf;
    buf << f.rdbuf();
    auto data = buf.str();
    return parse_bytes(data);
}

Material parse_bytes(std::string_view data_view) {
    std::string data(data_view);
    std::istringstream ss(data, std::ios::binary);

    config::Config cfg;
    if (data.size() >= 4 && data[0] == '\0' && data[1] == 'r' && data[2] == 'a' && data[3] == 'P')
        cfg = config::read(ss);
    else
        cfg = config::parse_text(ss);

    return parse(cfg);
}

} // namespace armatools::rvmat
