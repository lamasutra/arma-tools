#include "rvmat_preview_camera_controller.h"

#include <algorithm>
#include <cmath>
#include <cstring>

RvmatPreviewCameraController::CameraState
RvmatPreviewCameraController::camera_state() const {
    CameraState state;
    state.azimuth = azimuth_;
    state.elevation = elevation_;
    state.distance = distance_;
    std::memcpy(state.pivot, pivot_, sizeof(pivot_));
    return state;
}

void RvmatPreviewCameraController::set_camera_state(const CameraState& state) {
    azimuth_ = state.azimuth;
    elevation_ = state.elevation;
    distance_ = state.distance;
    std::memcpy(pivot_, state.pivot, sizeof(pivot_));
}

void RvmatPreviewCameraController::orbit_from_drag(float start_azimuth,
                                                   float start_elevation,
                                                   double dx, double dy) {
    azimuth_ = start_azimuth - static_cast<float>(dx) * 0.004f;
    elevation_ = start_elevation + static_cast<float>(dy) * 0.004f;
    elevation_ = std::clamp(elevation_, -1.5f, 1.5f);
}

void RvmatPreviewCameraController::pan_from_drag(const float start_pivot[3],
                                                 double dx, double dy) {
    const float scale = distance_ * 0.002f;
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    const float rx = ca;
    const float rz = -sa;
    const float uy = 1.0f;
    pivot_[0] = start_pivot[0] - static_cast<float>(dx) * scale * rx;
    pivot_[1] = start_pivot[1] + static_cast<float>(dy) * scale * uy;
    pivot_[2] = start_pivot[2] - static_cast<float>(dx) * scale * rz;
}

void RvmatPreviewCameraController::zoom_from_scroll(double dy) {
    distance_ *= (dy > 0) ? 1.1f : 0.9f;
    distance_ = std::max(distance_, 0.25f);
}

void RvmatPreviewCameraController::build_eye_center(float out_eye[3],
                                                    float out_center[3]) const {
    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    out_eye[0] = pivot_[0] + distance_ * ce * sa;
    out_eye[1] = pivot_[1] + distance_ * se;
    out_eye[2] = pivot_[2] + distance_ * ce * ca;
    out_center[0] = pivot_[0];
    out_center[1] = pivot_[1];
    out_center[2] = pivot_[2];
}
