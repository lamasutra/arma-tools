#pragma once

#include <string>
#include <vector>

// modelview namespace contains types used by the 3D model view panel and its
// highlighting / selection system. These types are shared between the panel
// widget and the render backend.
namespace modelview {

// Defines how highlighted geometry is drawn on top of the 3D model.
enum class HighlightMode {
    Points, // Draw highlighted positions as individual points (e.g., vertices).
    Lines,  // Draw highlighted geometry as line segments (e.g., edges of a face).
};

// Represents a set of geometry positions to highlight in the 3D viewport.
// For example, when the user clicks a named selection in P3D Info, the
// corresponding vertices/edges are highlighted using this struct.
struct HighlightGeometry {
    HighlightMode mode = HighlightMode::Points; // How the positions should be drawn.
    std::vector<float> positions; // Flat list of XYZ triplets: [x0,y0,z0, x1,y1,z1, ...].
    std::string debug_message;    // Optional label shown in the viewport for debugging.
};

// Represents one named selection from a P3D model (e.g., "memory", "geometry").
// Named selections group vertices or faces by a logical name in the .p3d format.
struct NamedSelectionItem {
    std::string name;   // Internal identifier used by the model format.
    std::string label;  // Friendly display label shown in the UI list.
};

}  // namespace modelview
