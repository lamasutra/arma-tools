#pragma once

namespace wrpterrain {

struct CameraState {
    float pivot[3] = {0.0f, 0.0f, 0.0f};
    float azimuth = 0.5f;
    float elevation = 0.8f;
    float distance = 500.0f;
};

}  // namespace wrpterrain
