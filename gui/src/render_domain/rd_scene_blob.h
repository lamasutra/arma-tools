#pragma once

#include "render_domain/rd_backend_abi.h"

#include <string>

namespace render_domain {

bool validate_scene_blob_v1(const rd_scene_blob_v1& blob, std::string* error_message);
bool validate_camera_blob_v1(const rd_camera_blob_v1& camera, std::string* error_message);
rd_camera_blob_v1 make_camera_blob_v1(const float* view16,
                                      const float* projection16,
                                      const float* position3);
std::string summarize_scene_blob_v1(const rd_scene_blob_v1& blob);
std::string summarize_camera_blob_v1(const rd_camera_blob_v1& camera);

}  // namespace render_domain
