#pragma once

#include "domain/gl_model_camera_types.h"

class GlModelCameraController {
public:
    using CameraMode = glmodel::CameraMode;
    using CameraState = glmodel::CameraState;

    void reset_camera();
    void set_camera_from_bounds(float cx, float cy, float cz, float radius);

    [[nodiscard]] CameraState camera_state() const;
    void set_camera_state(const CameraState& state);

    [[nodiscard]] CameraMode camera_mode() const;
    [[nodiscard]] bool set_camera_mode(CameraMode mode);

    [[nodiscard]] float distance() const;
    void orbit_from_drag(float start_azimuth, float start_elevation,
                         double dx, double dy);
    void pan_from_drag(const float start_pivot[3], double dx, double dy);
    [[nodiscard]] bool scroll_zoom(double dy);
    void move_local(float forward, float right, float up);

    void build_eye_center(float out_eye[3], float out_center[3]) const;
    [[nodiscard]] float far_plane() const;

private:
    float azimuth_ = 0.4f;
    float elevation_ = 0.3f;
    float distance_ = 5.0f;
    float pivot_[3] = {0.0f, 0.0f, 0.0f};
    CameraMode camera_mode_ = CameraMode::Orbit;

    float default_center_[3] = {0.0f, 0.0f, 0.0f};
    bool has_default_center_ = false;
    float default_azimuth_ = 0.4f;
    float default_elevation_ = 0.3f;
    float default_distance_ = 5.0f;
    bool has_default_camera_ = false;
};
