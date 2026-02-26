#include "wrp_terrain_camera_controller.h"

#include <algorithm>
#include <cmath>
#include <cstring>

WrpTerrainCameraController::CameraState WrpTerrainCameraController::camera_state() const {
    CameraState state;
    std::memcpy(state.pivot, pivot_, sizeof(pivot_));
    state.azimuth = azimuth_;
    state.elevation = elevation_;
    state.distance = distance_;
    return state;
}

void WrpTerrainCameraController::set_camera_state(const CameraState& state) {
    std::memcpy(pivot_, state.pivot, sizeof(pivot_));
    azimuth_ = state.azimuth;
    elevation_ = state.elevation;
    distance_ = state.distance;
}

void WrpTerrainCameraController::set_world_defaults(float world_size_x, float world_size_z,
                                                    float min_elevation, float max_elevation) {
    pivot_[0] = world_size_x * 0.5f;
    pivot_[2] = world_size_z * 0.5f;
    pivot_[1] = (min_elevation + max_elevation) * 0.5f;
    const float radius = std::max(world_size_x, world_size_z) * 0.75f;
    distance_ = std::clamp(std::max(radius, 100.0f), 100.0f, 200000.0f);
    azimuth_ = 0.65f;
    elevation_ = 0.85f;
}

void WrpTerrainCameraController::orbit_from_drag(float start_azimuth,
                                                 float start_elevation,
                                                 double dx, double dy) {
    azimuth_ = start_azimuth - static_cast<float>(dx) * 0.008f;
    elevation_ = std::clamp(start_elevation - static_cast<float>(dy) * 0.008f,
                            -1.57f, 1.57f);
}

void WrpTerrainCameraController::pan_from_drag(const float start_pivot[3],
                                               double dx, double dy) {
    const float scale = std::max(0.1f, distance_ * 0.002f);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    const float rx = ca;
    const float rz = -sa;
    pivot_[0] = start_pivot[0] - static_cast<float>(dx) * scale * rx;
    pivot_[2] = start_pivot[2] - static_cast<float>(dx) * scale * rz;
    pivot_[1] = start_pivot[1] + static_cast<float>(dy) * scale;
}

void WrpTerrainCameraController::zoom_from_scroll(double dy) {
    distance_ *= (dy > 0.0) ? 0.9f : 1.1f;
    distance_ = std::clamp(distance_, 5.0f, 250000.0f);
}

void WrpTerrainCameraController::move_local(float forward, float right, float vertical) {
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    const float fx = sa;
    const float fz = ca;
    const float rx = ca;
    const float rz = -sa;
    pivot_[0] += fx * forward + rx * right;
    pivot_[2] += fz * forward + rz * right;
    pivot_[1] += vertical;
}

float WrpTerrainCameraController::distance() const {
    return distance_;
}

const float* WrpTerrainCameraController::pivot() const {
    return pivot_;
}

void WrpTerrainCameraController::build_eye_center(float out_eye[3], float out_center[3]) const {
    out_eye[0] = pivot_[0];
    out_eye[1] = pivot_[1] + distance_;
    out_eye[2] = pivot_[2];

    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    out_center[0] = out_eye[0] + ce * sa;
    out_center[1] = out_eye[1] + se;
    out_center[2] = out_eye[2] + ce * ca;
}
