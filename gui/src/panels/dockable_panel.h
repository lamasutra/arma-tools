#pragma once

// Factory to wrap a gtkmm widget as a libpanel PanelWidget.
// The PanelWidget takes the GtkWidget* from the gtkmm object as its child.
// The caller must ensure the gtkmm widget outlives the PanelWidget.

#include <libpanel.h>
#include <gtkmm.h>

struct DockablePanelInfo {
    const char* id;
    const char* title;
    const char* icon_name;
    Gtk::Widget* content;  // The existing gtkmm tab widget
};

// Creates a PanelWidget* wrapping the gtkmm widget.
// The returned PanelWidget has a floating ref (consumed when added to a container).
inline PanelWidget* create_dockable_panel(const DockablePanelInfo& info) {
    auto* pw = PANEL_WIDGET(panel_widget_new());
    panel_widget_set_id(pw, info.id);
    panel_widget_set_title(pw, info.title);
    panel_widget_set_icon_name(pw, info.icon_name);
    panel_widget_set_reorderable(pw, TRUE);
    panel_widget_set_can_maximize(pw, TRUE);
    panel_widget_set_kind(pw, PANEL_WIDGET_KIND_UTILITY);

    // Bridge gtkmm widget to C GtkWidget* for libpanel
    panel_widget_set_child(pw, GTK_WIDGET(info.content->gobj()));

    return pw;
}

inline PanelWidget* create_simple_panel(const DockablePanelInfo& info) {
    auto* pw = PANEL_WIDGET(panel_widget_new());
    panel_widget_set_id(pw, info.id);
    panel_widget_set_title(pw, info.title);
    panel_widget_set_icon_name(pw, info.icon_name);
    panel_widget_set_reorderable(pw, FALSE);
    panel_widget_set_can_maximize(pw, FALSE);
    panel_widget_set_kind(pw, PANEL_WIDGET_KIND_UTILITY);

    // Bridge gtkmm widget to C GtkWidget* for libpanel
    panel_widget_set_child(pw, GTK_WIDGET(info.content->gobj()));

    return pw;
}
