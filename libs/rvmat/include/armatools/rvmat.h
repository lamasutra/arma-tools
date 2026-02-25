#pragma once

#include "armatools/config.h"

#include <array>
#include <filesystem>
#include <string_view>
#include <string>
#include <vector>

namespace armatools::rvmat {

struct TextureStage {
    int stage_number = -1;
    std::string class_name;
    std::string texture_path;
    std::string uv_source;
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
    std::vector<TextureStage> stages;
    std::string surface;
};

// parse reads an RVMAT file from disk (rapified or text).
Material parse(const std::filesystem::path& path);

// parse_bytes reads an RVMAT from an in-memory buffer (rapified or text).
Material parse_bytes(std::string_view data);

// parse extracts material fields from a parsed config tree.
Material parse(const armatools::config::Config& cfg);

} // namespace armatools::rvmat
