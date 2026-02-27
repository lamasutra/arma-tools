#pragma once

#include "render_domain/rd_backend_abi.h"

#include <armatools/p3d.h>

#include <cstdint>
#include <string>
#include <vector>

namespace render_domain {

struct SceneBlobBuildOutput {
    rd_scene_blob_v1 blob{};
    std::vector<uint8_t> data;
    std::vector<std::string> material_texture_keys;
};

bool build_scene_blob_v1_from_lods(const std::vector<armatools::p3d::LOD>& lods,
                                   SceneBlobBuildOutput* out,
                                   std::string* error_message);

}  // namespace render_domain
