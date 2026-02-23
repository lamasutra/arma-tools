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

static int parse_stage_number(const std::string& class_name) {
    auto lower = to_lower_ascii(class_name);
    if (!lower.starts_with("stage")) return -1;
    auto tail = lower.substr(5);
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
    mat.surface = get_string(root, "surfaceInfo");

    for (const auto& ne : root.entries) {
        auto* ce = std::get_if<config::ClassEntryOwned>(&ne.entry);
        if (!ce || !ce->cls || ce->cls->external || ce->cls->deletion) continue;
        int stage_number = parse_stage_number(ne.name);
        if (stage_number < 0) continue;

        TextureStage st;
        st.stage_number = stage_number;
        st.class_name = ne.name;
        st.texture_path = get_string(*ce->cls, "texture");
        st.uv_source = get_string(*ce->cls, "uvSource");
        mat.stages.push_back(std::move(st));
    }

    std::sort(mat.stages.begin(), mat.stages.end(),
              [](const TextureStage& a, const TextureStage& b) {
                  if (a.stage_number != b.stage_number)
                      return a.stage_number < b.stage_number;
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
    std::istringstream ss(data, std::ios::binary);

    config::Config cfg;
    if (data.size() >= 4 && data[0] == '\0' && data[1] == 'r' && data[2] == 'a' && data[3] == 'P')
        cfg = config::read(ss);
    else
        cfg = config::parse_text(ss);

    return parse(cfg);
}

} // namespace armatools::rvmat
