#pragma once

namespace rvmatpreview {

struct CameraState {
    float azimuth = 0.3f;
    float elevation = 0.2f;
    float distance = 2.6f;
    float pivot[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace rvmatpreview
