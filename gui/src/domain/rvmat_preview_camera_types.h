#pragma once

// rvmatpreview namespace contains types for the RVMAT surface material preview camera.
// RVMAT files describe how an Arma 3 surface looks (texture, reflections, etc.).
namespace rvmatpreview {

// Stores the orbit camera state used in the RVMAT preview panel.
// Works similarly to the glmodel::CameraState but with defaults suited for
// previewing a small surface tile (shorter default distance than the 3D model viewer).
struct CameraState {
    float azimuth = 0.3f;           // Horizontal rotation angle (radians).
    float elevation = 0.2f;         // Vertical tilt angle (radians); slight downward angle by default.
    float distance = 2.6f;          // Distance from the camera to the pivot point (world units).
    float pivot[3] = {0.0f, 0.0f, 0.0f}; // 3D point the camera orbits around.
};

}  // namespace rvmatpreview
