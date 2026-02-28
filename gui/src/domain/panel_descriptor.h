#pragma once

// DockArea specifies which region of the main window a panel is placed in.
// libpanel (PanelDock) divides the window into these five zones.
enum class DockArea {
    Center,  // The main tab area where most tool panels live.
    Bottom,  // The bottom strip — typically used for the log panel.
    Start,   // Left side panel area.
    End,     // Right side panel area.
    Top,     // Top strip above the main tab area.
};

// Describes one panel entry in the default catalog.
// The application uses this to construct and place all panels on startup.
struct PanelDescriptor {
    const char* id = "";          // Unique string ID for this panel (e.g. "asset-browser").
    const char* title = "";       // Human-readable tab title shown in the tab bar.
    const char* icon_name = "";   // GTK/Adwaita icon name for the tab icon.
    DockArea area = DockArea::Center; // Where to place the panel in the dock layout.
    bool pinned = false;          // If true, the tab cannot be reordered or dragged out.
    bool simple_panel = false;    // If true, creates a non-dockable plain panel (no tab bar).
};
