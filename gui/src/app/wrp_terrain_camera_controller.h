#pragma once

#include "domain/wrp_terrain_camera_types.h"

class WrpTerrainCameraController {
public:
    using CameraMode = wrpterrain::CameraMode;
    using CameraState = wrpterrain::CameraState;

    [[nodiscard]] CameraState camera_state() const;
    void set_camera_state(const CameraState& state);

    [[nodiscard]] CameraMode camera_mode() const;
    [[nodiscard]] bool set_camera_mode(CameraMode mode);

    void set_world_defaults(float world_size_x, float world_size_z,
                            float min_elevation, float max_elevation);

    void orbit_from_drag(float start_azimuth, float start_elevation,
                         double dx, double dy);
    void pan_from_drag(const float start_pivot[3], double dx, double dy);
    void zoom_from_scroll(double dy);
    void move_local(float forward, float right, float vertical);

    [[nodiscard]] float distance() const;
    [[nodiscard]] const float* pivot() const;

    void build_eye_center(float out_eye[3], float out_center[3]) const;

private:
    float pivot_[3] = {0.0f, 0.0f, 0.0f};
    float azimuth_ = 0.5f;
    float elevation_ = 0.8f;
    float distance_ = 500.0f;
    CameraMode camera_mode_ = CameraMode::Orbit;
    float default_center_[3] = {0.0f, 0.0f, 0.0f};
    bool has_default_center_ = false;
};
