#pragma once

// wrpterrain namespace contains camera types for the WRP terrain viewer panel.
// WRP is the Arma 3 world/terrain file format containing heightmap and object data.
namespace wrpterrain {

// Controls how the camera moves across the terrain.
enum class CameraMode {
    Orbit,       // Camera orbits around a pivot point on the terrain surface.
    FirstPerson, // Camera flies freely through the terrain scene (WASD-style).
};

// Stores the current camera state for the terrain viewer.
// Uses a large default distance (500 units) because WRP terrains are huge.
struct CameraState {
    float pivot[3] = {0.0f, 0.0f, 0.0f}; // 3D world-space point the camera orbits around.
    float azimuth = 0.5f;                  // Horizontal rotation angle (radians).
    float elevation = 0.8f;                // Vertical tilt angle (radians); high angle for top-down view.
    float distance = 500.0f;               // Distance from pivot to camera — large for terrain scale.
};

}  // namespace wrpterrain
