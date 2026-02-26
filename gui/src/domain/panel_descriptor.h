#pragma once

enum class DockArea {
    Center,
    Bottom,
    Start,
    End,
    Top,
};

struct PanelDescriptor {
    const char* id = "";
    const char* title = "";
    const char* icon_name = "";
    DockArea area = DockArea::Center;
    bool pinned = false;
    bool simple_panel = false;
};
