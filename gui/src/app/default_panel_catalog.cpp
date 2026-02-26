#include "default_panel_catalog.h"

const std::vector<PanelDescriptor>& default_panel_catalog() {
    static const std::vector<PanelDescriptor> k_catalog = {
        {"asset-browser", "Asset Browser", "system-file-manager-symbolic", DockArea::Center, false, false},
        {"pbo-browser", "PBO Browser", "package-x-generic-symbolic", DockArea::Center, false, false},
        {"p3d-info", "P3D Info", "emblem-system-symbolic", DockArea::Center, false, false},
        {"p3d-convert", "P3D Convert", "emblem-synchronizing-symbolic", DockArea::Center, false, false},
        {"paa-preview", "PAA Preview", "image-x-generic-symbolic", DockArea::Center, false, false},
        {"config-viewer", "Config Viewer", "text-x-generic-symbolic", DockArea::Center, false, false},
        {"audio", "Audio", "audio-x-generic-symbolic", DockArea::Center, false, false},
        {"ogg-validate", "OGG Validate", "dialog-warning-symbolic", DockArea::Center, false, false},
        {"conversions", "Conversions", "document-save-as-symbolic", DockArea::Center, false, false},
        {"obj-replace", "Obj Replace", "edit-find-replace-symbolic", DockArea::Center, false, false},
        {"wrp-info", "WRP Info", "x-office-address-book-symbolic", DockArea::Center, false, false},
        {"wrp-project", "WRP Project", "folder-new-symbolic", DockArea::Center, false, false},
        {"config", "Configuration", "preferences-system-symbolic", DockArea::Center, false, false},
        {"about", "About", "help-about-symbolic", DockArea::Center, true, true},
        {"log", "Log", "utilities-terminal-symbolic", DockArea::Bottom, false, false},
    };

    return k_catalog;
}
