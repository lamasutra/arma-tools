#pragma once

#include "armatools/config.h"

#include <array>
#include <filesystem>
#include <string_view>
#include <string>
#include <vector>

namespace armatools::rvmat {

struct UVTransform {
    std::array<float, 3> aside{1.0f, 0.0f, 0.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    std::array<float, 3> dir{0.0f, 0.0f, 0.0f};
    std::array<float, 3> pos{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

struct TexGen {
    int index = -1;
    std::string class_name;
    std::string uv_source;
    UVTransform uv_transform;
};

struct TextureStage {
    int stage_number = -1;
    std::string class_name;
    std::string texture_path;
    std::string uv_source;
    std::string filter;
    std::string tex_gen;
    UVTransform uv_transform;
};

struct Material {
    std::string pixel_shader;
    std::string vertex_shader;
    std::array<float, 4> ambient{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> diffuse{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> forced_diffuse{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> emissive{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> specular{0.0f, 0.0f, 0.0f, 0.0f};
    float specular_power = 0.0f;
    std::vector<std::string> render_flags;
    std::string main_light;
    std::string fog_mode;
    std::vector<TextureStage> stages;
    std::vector<TexGen> tex_gens;
    std::string surface;
};

// parse reads an RVMAT file from disk (rapified or text).
Material parse(const std::filesystem::path& path);

// parse_bytes reads an RVMAT from an in-memory buffer (rapified or text).
Material parse_bytes(std::string_view data);

// parse extracts material fields from a parsed config tree.
Material parse(const armatools::config::Config& cfg);

} // namespace armatools::rvmat
