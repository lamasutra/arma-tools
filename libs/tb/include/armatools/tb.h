#pragma once

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace armatools::tb {

// p3d_base_name extracts filename without path or extension from a P3D path.
std::string p3d_base_name(const std::string& s);

// sdbm_hash computes the SDBM hash of a string.
// Terrain Builder uses this for template hashes and library IDs.
uint32_t sdbm_hash(const std::string& s);

// xml_esc escapes special XML characters in text content.
std::string xml_esc(const std::string& s);

struct CategoryStyle {
    std::string shape = "rectangle";
    int32_t fill = -65536;
    int32_t outline = -16777216;
};

inline CategoryStyle default_style() { return {}; }

struct ModelMeta {
    std::array<float, 3> bbox_min;
    std::array<float, 3> bbox_max;
    std::array<float, 3> bbox_center;
    float bb_radius = -1.0f;
    float bb_hscale = 1.0f;
    float height = 0.0f;
};

// write_tml writes a Terrain Builder template library (.tml) to w.
// If name_overrides is non-null, it maps full model path to display name
// (used for deduplication and case correction).
void write_tml(std::ostream& w, const std::string& library_name,
               const std::vector<std::string>& models,
               const std::unordered_map<std::string, ModelMeta>* meta,
               const CategoryStyle& style,
               const std::unordered_map<std::string, std::string>* name_overrides = nullptr);

} // namespace armatools::tb
