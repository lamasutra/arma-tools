#pragma once

// glmodel namespace contains types related to the 3D model viewer camera.
// These are shared between the camera controller and the rendering widget.
namespace glmodel {

// Controls how the camera moves in the 3D viewport.
enum class CameraMode {
    Orbit,       // Camera orbits around a pivot point (rotate by dragging).
    FirstPerson, // Camera moves like a first-person view (WASD-style movement).
};

// Stores the current state of the orbit camera.
// Angles are in radians; distance is in world units.
struct CameraState {
    float azimuth = 0.4f;          // Horizontal rotation angle around the Y axis.
    float elevation = 0.3f;        // Vertical tilt angle (pitch) above the horizon.
    float distance = 5.0f;         // Distance from the pivot point to the camera.
    float pivot[3] = {0.0f, 0.0f, 0.0f}; // 3D world-space point the camera orbits around.
};

}  // namespace glmodel
