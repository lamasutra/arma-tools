#pragma once

namespace glmodel {

enum class CameraMode {
    Orbit,
    FirstPerson,
};

struct CameraState {
    float azimuth = 0.4f;
    float elevation = 0.3f;
    float distance = 5.0f;
    float pivot[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace glmodel
