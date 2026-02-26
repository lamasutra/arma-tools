#include "gl_model_camera_controller.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void GlModelCameraController::reset_camera() {
    if (has_default_camera_) {
        azimuth_ = default_azimuth_;
        elevation_ = default_elevation_;
        distance_ = default_distance_;
        if (camera_mode_ == CameraMode::Orbit) {
            pivot_[0] = default_center_[0];
            pivot_[1] = default_center_[1];
            pivot_[2] = default_center_[2];
        } else {
            const float ce = std::cos(elevation_);
            const float se = std::sin(elevation_);
            const float ca = std::cos(azimuth_);
            const float sa = std::sin(azimuth_);
            pivot_[0] = default_center_[0] + distance_ * ce * sa;
            pivot_[1] = default_center_[1] + distance_ * se;
            pivot_[2] = default_center_[2] + distance_ * ce * ca;
        }
        return;
    }

    azimuth_ = 0.4f;
    elevation_ = 0.3f;
    distance_ = 5.0f;
    pivot_[0] = 0.0f;
    pivot_[1] = 0.0f;
    pivot_[2] = 0.0f;
}

void GlModelCameraController::set_camera_from_bounds(float cx, float cy, float cz,
                                                     float radius) {
    default_center_[0] = cx;
    default_center_[1] = cy;
    default_center_[2] = cz;
    has_default_center_ = true;
    default_distance_ = std::max(radius * 2.0f, 0.5f);
    default_azimuth_ = 0.4f;
    default_elevation_ = 0.3f;
    has_default_camera_ = true;

    distance_ = default_distance_;
    azimuth_ = default_azimuth_;
    elevation_ = default_elevation_;
    if (camera_mode_ == CameraMode::Orbit) {
        pivot_[0] = cx;
        pivot_[1] = cy;
        pivot_[2] = cz;
        return;
    }

    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    pivot_[0] = cx + distance_ * ce * sa;
    pivot_[1] = cy + distance_ * se;
    pivot_[2] = cz + distance_ * ce * ca;
}

GlModelCameraController::CameraState GlModelCameraController::camera_state() const {
    CameraState state;
    state.azimuth = azimuth_;
    state.elevation = elevation_;
    state.distance = distance_;
    std::memcpy(state.pivot, pivot_, sizeof(pivot_));
    return state;
}

void GlModelCameraController::set_camera_state(const CameraState& state) {
    azimuth_ = state.azimuth;
    elevation_ = state.elevation;
    distance_ = state.distance;
    std::memcpy(pivot_, state.pivot, sizeof(pivot_));
}

GlModelCameraController::CameraMode GlModelCameraController::camera_mode() const {
    return camera_mode_;
}

bool GlModelCameraController::set_camera_mode(CameraMode mode) {
    if (camera_mode_ == mode) return false;

    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    const float dir[3] = {ce * sa, se, ce * ca};
    float eye[3];
    float target[3];

    if (camera_mode_ == CameraMode::Orbit) {
        target[0] = pivot_[0];
        target[1] = pivot_[1];
        target[2] = pivot_[2];
        eye[0] = pivot_[0] + dir[0] * distance_;
        eye[1] = pivot_[1] + dir[1] * distance_;
        eye[2] = pivot_[2] + dir[2] * distance_;
    } else {
        eye[0] = pivot_[0];
        eye[1] = pivot_[1];
        eye[2] = pivot_[2];
        target[0] = eye[0] - dir[0];
        target[1] = eye[1] - dir[1];
        target[2] = eye[2] - dir[2];
    }

    camera_mode_ = mode;
    if (camera_mode_ == CameraMode::Orbit) {
        const float center[3] = {
            has_default_center_ ? default_center_[0] : target[0],
            has_default_center_ ? default_center_[1] : target[1],
            has_default_center_ ? default_center_[2] : target[2],
        };
        pivot_[0] = center[0];
        pivot_[1] = center[1];
        pivot_[2] = center[2];
        const float dx = eye[0] - center[0];
        const float dy = eye[1] - center[1];
        const float dz = eye[2] - center[2];
        distance_ = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 0.01f);
        azimuth_ = std::atan2(dx, dz);
        elevation_ = std::asin(std::clamp(dy / distance_, -1.0f, 1.0f));
        return true;
    }

    pivot_[0] = eye[0];
    pivot_[1] = eye[1];
    pivot_[2] = eye[2];
    const float dx = eye[0] - target[0];
    const float dy = eye[1] - target[1];
    const float dz = eye[2] - target[2];
    distance_ = std::max(std::sqrt(dx * dx + dy * dy + dz * dz), 0.01f);
    return true;
}

float GlModelCameraController::distance() const {
    return distance_;
}

void GlModelCameraController::orbit_from_drag(float start_azimuth,
                                              float start_elevation,
                                              double dx, double dy) {
    azimuth_ = start_azimuth - static_cast<float>(dx) * 0.004f;
    elevation_ = start_elevation + static_cast<float>(dy) * 0.004f;
    elevation_ = std::clamp(elevation_, -1.5f, 1.5f);
}

void GlModelCameraController::pan_from_drag(const float start_pivot[3],
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

bool GlModelCameraController::scroll_zoom(double dy) {
    if (camera_mode_ != CameraMode::Orbit) return false;
    distance_ *= (dy > 0) ? 1.1f : 0.9f;
    distance_ = std::max(distance_, 0.01f);
    return true;
}

void GlModelCameraController::move_local(float forward, float right, float up) {
    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    const float fx = -ce * sa;
    const float fy = -se;
    const float fz = -ce * ca;
    const float rx = ca;
    const float rz = -sa;

    pivot_[0] += fx * forward + rx * right;
    pivot_[1] += fy * forward + up;
    pivot_[2] += fz * forward + rz * right;
}

void GlModelCameraController::build_eye_center(float out_eye[3],
                                               float out_center[3]) const {
    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);

    if (camera_mode_ == CameraMode::Orbit) {
        out_eye[0] = pivot_[0] + distance_ * ce * sa;
        out_eye[1] = pivot_[1] + distance_ * se;
        out_eye[2] = pivot_[2] + distance_ * ce * ca;
        out_center[0] = pivot_[0];
        out_center[1] = pivot_[1];
        out_center[2] = pivot_[2];
        return;
    }

    out_eye[0] = pivot_[0];
    out_eye[1] = pivot_[1];
    out_eye[2] = pivot_[2];
    out_center[0] = out_eye[0] - ce * sa;
    out_center[1] = out_eye[1] - se;
    out_center[2] = out_eye[2] - ce * ca;
}

float GlModelCameraController::far_plane() const {
    return std::max(distance_ * 10.0f, 100.0f);
}
