#pragma once

#include "domain/rvmat_preview_camera_types.h"

class RvmatPreviewCameraController {
public:
    using CameraState = rvmatpreview::CameraState;

    [[nodiscard]] CameraState camera_state() const;
    void set_camera_state(const CameraState& state);

    void orbit_from_drag(float start_azimuth, float start_elevation,
                         double dx, double dy);
    void pan_from_drag(const float start_pivot[3], double dx, double dy);
    void zoom_from_scroll(double dy);

    void build_eye_center(float out_eye[3], float out_center[3]) const;

private:
    float azimuth_ = 0.3f;
    float elevation_ = 0.2f;
    float distance_ = 2.6f;
    float pivot_[3] = {0.0f, 0.0f, 0.0f};
};
