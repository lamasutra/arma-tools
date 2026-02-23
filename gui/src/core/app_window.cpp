#include "app_window.h"
#include "dockable_panel.h"
#include "panel_wrapper.h"
#include "pbo_index_service.h"
#include "cli_logger.h"

#include <adwaita.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <vector>

// ---------------------------------------------------------------------------
// Tear-off helpers: find AdwTabView widgets inside the widget tree and
// connect create-window so dragging a tab outside spawns a new window.
// ---------------------------------------------------------------------------

// Forward declarations
static void hook_tab_views_for_tearoff(GtkWidget* dock_or_grid, AppWindow* self);

// Recursively find all AdwTabView widgets under `widget`.
static void find_tab_views(GtkWidget* widget, std::vector<AdwTabView*>& out) {
    if (ADW_IS_TAB_VIEW(widget))
        out.push_back(ADW_TAB_VIEW(widget));
    for (auto* child = gtk_widget_get_first_child(widget);
         child; child = gtk_widget_get_next_sibling(child))
        find_tab_views(child, out);
}

// Guard flag — when true, the close-page handler allows closing all panels
// (including "about").  Set during on_reset_layout().
static bool g_allow_close_all = false;

// Shared create-frame callback for all workspaces (constructor + tear-off).
static PanelFrame* create_frame_with_hooks(gpointer, PanelPosition*, gpointer ud) {
    auto* f = PANEL_FRAME(panel_frame_new());
    auto* tb = PANEL_FRAME_TAB_BAR(panel_frame_tab_bar_new());
    panel_frame_tab_bar_set_autohide(tb, TRUE);
    panel_frame_set_header(f, PANEL_FRAME_HEADER(tb));
    g_signal_connect(GTK_WIDGET(f), "realize",
        G_CALLBACK(+[](GtkWidget* w, gpointer ud2) {
            hook_tab_views_for_tearoff(w, static_cast<AppWindow*>(ud2));
        }), ud);
    return f;
}

// AdwTabView::create-window handler.
// Creates a new PanelDocumentWorkspace, seeds it with a dummy widget so a
// PanelFrame (and its internal AdwTabView) is created, then returns that
// AdwTabView so Adwaita transfers the correct dragged page natively.
static AdwTabView* on_tab_create_window(AdwTabView* source, gpointer user_data) {
    auto* self = static_cast<AppWindow*>(user_data);

    // Block tear-off for non-reorderable panels (e.g. About)
    auto* page = adw_tab_view_get_selected_page(source);
    if (page) {
        auto* child = adw_tab_page_get_child(page);
        if (child && PANEL_IS_WIDGET(child) &&
            !panel_widget_get_reorderable(PANEL_WIDGET(child)))
            return nullptr;
    }

    // Create a new workspace window
    auto* new_ws = PANEL_DOCUMENT_WORKSPACE(panel_document_workspace_new());
    auto* gtkapp = gtk_window_get_application(self->gtk_window());
    gtk_window_set_application(GTK_WINDOW(new_ws), gtkapp);
    gtk_window_set_default_size(GTK_WINDOW(new_ws), 800, 600);
    gtk_window_set_title(GTK_WINDOW(new_ws), "ArmA 3 Tools");

    auto* header = adw_header_bar_new();
    panel_document_workspace_set_titlebar(new_ws, GTK_WIDGET(header));

    panel_workbench_add_workspace(self->workbench(), PANEL_WORKSPACE(new_ws));

    // Connect create-frame on the new dock/workspace
    auto* new_dock = panel_document_workspace_get_dock(new_ws);
    g_signal_connect(new_dock, "create-frame",
        G_CALLBACK(create_frame_with_hooks), self);
    g_signal_connect(new_ws, "create-frame",
        G_CALLBACK(create_frame_with_hooks), self);

    // Seed a dummy PanelWidget so the workspace creates a PanelFrame
    // (which contains the AdwTabView we need to return).
    auto* dummy = PANEL_WIDGET(panel_widget_new());
    panel_widget_set_id(dummy, "__dummy__");
    auto center = panel::make_position(PANEL_AREA_CENTER);
    panel_document_workspace_add_widget(new_ws, dummy, center.get());

    // Find the AdwTabView inside the newly created frame
    std::vector<AdwTabView*> target_views;
    find_tab_views(GTK_WIDGET(new_ws), target_views);

    if (target_views.empty()) {
        panel_workbench_remove_workspace(self->workbench(), PANEL_WORKSPACE(new_ws));
        gtk_window_destroy(GTK_WINDOW(new_ws));
        return nullptr;
    }

    // Remove the dummy — the frame and its AdwTabView survive
    auto* dummy_frame = PANEL_FRAME(
        gtk_widget_get_ancestor(GTK_WIDGET(dummy), PANEL_TYPE_FRAME));
    if (dummy_frame)
        panel_frame_remove(dummy_frame, dummy);

    gtk_window_present(GTK_WINDOW(new_ws));
    hook_tab_views_for_tearoff(GTK_WIDGET(new_ws), self);

    // Return the target AdwTabView — Adwaita natively transfers the
    // correct dragged page (no index confusion).
    return target_views[0];
}

// AdwTabView::close-page handler — reject close for non-closeable panels (About).
static gboolean on_tab_close_page(AdwTabView* tv, AdwTabPage* page, gpointer) {
    if (!g_allow_close_all) {
        auto* child = adw_tab_page_get_child(page);
        if (child && PANEL_IS_WIDGET(child)) {
            const char* id = panel_widget_get_id(PANEL_WIDGET(child));
            if (id && std::string(id) == "about") {
                adw_tab_view_close_page_finish(tv, page, FALSE);
                return GDK_EVENT_STOP;
            }
        }
    }
    adw_tab_view_close_page_finish(tv, page, TRUE);
    return GDK_EVENT_STOP;
}

// Walk a workspace's dock and connect create-window + close-page on every AdwTabView.
static void hook_tab_views_for_tearoff(GtkWidget* dock_or_grid, AppWindow* self) {
    std::vector<AdwTabView*> views;
    find_tab_views(dock_or_grid, views);
    for (auto* tv : views) {
        // Avoid connecting twice — use a GObject data flag
        static const char* key = "tearoff-connected";
        if (g_object_get_data(G_OBJECT(tv), key))
            continue;
        g_object_set_data(G_OBJECT(tv), key, GINT_TO_POINTER(1));
        g_signal_connect(tv, "create-window",
            G_CALLBACK(on_tab_create_window), self);
        g_signal_connect(tv, "close-page",
            G_CALLBACK(on_tab_close_page), nullptr);
    }
}

// ---------------------------------------------------------------------------
// Helper: add a panel to the workspace
// ---------------------------------------------------------------------------
void AppWindow::add_panel(Gtk::Widget& content, const char* id,
                          const char* title, const char* icon_name,
                          PanelArea area) {
    auto* pw = create_dockable_panel({id, title, icon_name, &content});
    panels_[id] = pw;

    auto pos = panel::make_position(area);
    panel_document_workspace_add_widget(workspace_, pw, pos.get());
}

// ---------------------------------------------------------------------------
void AppWindow::pin_panel(const char* id) {
    auto it = panels_.find(id);
    if (it == panels_.end()) return;

    auto* pw = it->second;
    // Prevent dragging/reordering — but do NOT use panel_frame_set_child_pinned
    // because that calls adw_tab_page_set_pinned() which reorders the tab to
    // position 0 and breaks tear-off for other tabs.
    panel_widget_set_reorderable(pw, FALSE);
    // Close prevention is handled by hooking AdwTabView::close-page
    // in hook_tab_views_for_tearoff / hook_tab_views_for_close_guard.
}

// ---------------------------------------------------------------------------
void AppWindow::update_status(const std::string& text) {
    status_label_.set_text(text);
}

// ---------------------------------------------------------------------------
void AppWindow::reload_config() {
    cfg_ = load_config();
    layout_cfg_ = load_layout_config();
    armatools::cli::log_verbose("Configuration reloaded from {}", config_path());
    // if (services_.pbo_index_service)
    //     services_.pbo_index_service->set_db_path(cfg_.a3db_path);
    // } else {
    //     services_.pbo_index_service = std::make_shared<PboIndexService>();
    // }

    if (tab_config_inited_) tab_config_.set_config(&cfg_);
    if (tab_asset_browser_inited_) tab_asset_browser_.set_config(&cfg_);
    if (tab_pbo_inited_) tab_pbo_.set_config(&cfg_);
    if (tab_audio_inited_) tab_audio_.set_config(&cfg_);
    if (tab_ogg_validate_inited_) tab_ogg_validate_.set_config(&cfg_);
    if (tab_conversions_inited_) tab_conversions_.set_config(&cfg_);
    if (tab_obj_replace_inited_) tab_obj_replace_.set_config(&cfg_);
    if (tab_wrp_info_inited_) tab_wrp_info_.set_config(&cfg_);
    if (tab_wrp_project_inited_) tab_wrp_project_.set_config(&cfg_);
    if (tab_p3d_convert_inited_) tab_p3d_convert_.set_config(&cfg_);
    if (tab_p3d_info_inited_) tab_p3d_info_.set_config(&cfg_);
    if (tab_paa_preview_inited_) tab_paa_preview_.set_config(&cfg_);
    if (tab_config_viewer_inited_) tab_config_viewer_.set_config(&cfg_);

    update_status("Configuration reloaded");
}

void AppWindow::init_tabs_lazy() {
    auto hook_lazy = [this](Gtk::Widget& widget, bool& inited,
                            std::function<void()> init_fn) {
        auto maybe_init = [this, &widget, &inited, init_fn]() {
            if (inited) return;
            if (!gtk_widget_get_mapped(widget.gobj())) return;
            if (!gtk_widget_get_child_visible(widget.gobj())) return;
            inited = true;
            init_fn();
        };
        widget.signal_map().connect(maybe_init);
        Glib::signal_timeout().connect(
            [maybe_init, &inited]() -> bool {
                if (inited) return false;
                maybe_init();
                return !inited;
            },
            150);
    };

    hook_lazy(tab_config_, tab_config_inited_,
              [this]() { tab_config_.set_config(&cfg_); });
    hook_lazy(tab_asset_browser_, tab_asset_browser_inited_,
              [this]() { tab_asset_browser_.set_config(&cfg_); });
    hook_lazy(tab_pbo_, tab_pbo_inited_,
              [this]() { tab_pbo_.set_config(&cfg_); });
    hook_lazy(tab_audio_, tab_audio_inited_,
              [this]() { tab_audio_.set_config(&cfg_); });
    hook_lazy(tab_ogg_validate_, tab_ogg_validate_inited_,
              [this]() { tab_ogg_validate_.set_config(&cfg_); });
    hook_lazy(tab_conversions_, tab_conversions_inited_,
              [this]() { tab_conversions_.set_config(&cfg_); });
    hook_lazy(tab_obj_replace_, tab_obj_replace_inited_,
              [this]() { tab_obj_replace_.set_config(&cfg_); });
    hook_lazy(tab_wrp_info_, tab_wrp_info_inited_,
              [this]() { tab_wrp_info_.set_config(&cfg_); });
    hook_lazy(tab_wrp_project_, tab_wrp_project_inited_,
              [this]() { tab_wrp_project_.set_config(&cfg_); });
    hook_lazy(tab_p3d_convert_, tab_p3d_convert_inited_,
              [this]() { tab_p3d_convert_.set_config(&cfg_); });
    hook_lazy(tab_p3d_info_, tab_p3d_info_inited_,
              [this]() { tab_p3d_info_.set_config(&cfg_); });
    hook_lazy(tab_paa_preview_, tab_paa_preview_inited_,
              [this]() { tab_paa_preview_.set_config(&cfg_); });
    hook_lazy(tab_config_viewer_, tab_config_viewer_inited_,
              [this]() { tab_config_viewer_.set_config(&cfg_); });
}

// ---------------------------------------------------------------------------
// Session save/restore
// ---------------------------------------------------------------------------

// Callback to collect panels from a single dock into a PanelSession
static void collect_panels_from_dock(PanelDock* dock, PanelSession* session) {
    panel_dock_foreach_frame(dock, [](PanelFrame* frame, gpointer user_data) {
        auto* sess = static_cast<PanelSession*>(user_data);
        guint n = panel_frame_get_n_pages(frame);
        for (guint i = 0; i < n; i++) {
            auto* pw = panel_frame_get_page(frame, i);
            const char* id = panel_widget_get_id(pw);
            if (!id || id[0] == '\0') continue;

            auto* item = panel_session_item_new();
            panel_session_item_set_id(item, id);
            panel_session_item_set_type_hint(item, panel_widget_get_kind(pw));

            auto* pos = panel_widget_get_position(pw);  // transfer-full
            if (pos) {
                panel_session_item_set_position(item, pos);
                g_object_unref(pos);
            }

            panel_session_append(sess, item);
            g_object_unref(item);
        }
    }, session);
}

void AppWindow::save_layout() {
    auto* session = panel_session_new();

    // Collect panels from all workspaces (main + any torn-off windows)
    panel_workbench_foreach_workspace(workbench_,
        [](PanelWorkspace* ws, gpointer ud) {
            if (PANEL_IS_DOCUMENT_WORKSPACE(ws)) {
                auto* dock = panel_document_workspace_get_dock(
                    PANEL_DOCUMENT_WORKSPACE(ws));
                collect_panels_from_dock(dock, static_cast<PanelSession*>(ud));
            }
        }, session);

    // Serialize to GVariant then to string
    GVariant* variant = panel_session_to_variant(session);
    if (variant) {
        gchar* variant_str = g_variant_print(variant, TRUE);
        layout_cfg_.panels = std::string(variant_str);
        g_free(variant_str);
        g_variant_unref(variant);
        save_config(cfg_);
    }

    g_object_unref(session);
}

void AppWindow::restore_layout() {
    if (layout_cfg_.panels.empty()) {
        apply_default_layout();
        return;
    }

    // Parse the GVariant string
    GError* error = nullptr;
    GVariant* variant = g_variant_parse(nullptr, layout_cfg_.panels.c_str(),
                                         nullptr, nullptr, &error);
    if (!variant) {
        app_log(LogLevel::Warning, std::string("Failed to parse saved layout: ") +
                (error ? error->message : "unknown error"));
        if (error) g_error_free(error);
        apply_default_layout();
        return;
    }

    auto* session = panel_session_new_from_variant(variant, &error);
    g_variant_unref(variant);
    if (!session) {
        app_log(LogLevel::Warning, std::string("Failed to restore session: ") +
                (error ? error->message : "unknown error"));
        if (error) g_error_free(error);
        apply_default_layout();
        return;
    }

    // Track which panels are placed by the session
    std::set<std::string> restored_ids;

    guint n = panel_session_get_n_items(session);
    for (guint i = 0; i < n; i++) {
        auto* item = panel_session_get_item(session, i);
        const char* id = panel_session_item_get_id(item);
        if (!id) continue;

        auto it = panels_.find(id);
        if (it == panels_.end()) continue;

        auto* pos = panel_session_item_get_position(item);  // transfer-none
        if (pos) {
            panel_document_workspace_add_widget(workspace_, it->second, pos);
            restored_ids.insert(id);
        }
    }

    g_object_unref(session);

    // Any panels NOT in the saved session must still be added to avoid
    // orphaned PanelWidgets (which would double-free their gtkmm children).
    auto center = panel::make_position(PANEL_AREA_CENTER);
    for (auto& [id, pw] : panels_) {
        if (restored_ids.count(id) == 0) {
            panel_document_workspace_add_widget(workspace_, pw, center.get());
        }
    }

    // About tab is always pinned
    pin_panel("about");
}

void AppWindow::apply_default_layout() {
    // Add panels as tabs in the center grid
    add_panel(tab_asset_browser_,  "asset-browser",  "Asset Browser",  "system-file-manager-symbolic",  PANEL_AREA_CENTER);
    add_panel(tab_pbo_,            "pbo-browser",    "PBO Browser",    "package-x-generic-symbolic",    PANEL_AREA_CENTER);
    add_panel(tab_p3d_info_,       "p3d-info",       "P3D Info",       "emblem-system-symbolic",        PANEL_AREA_CENTER);
    add_panel(tab_p3d_convert_,    "p3d-convert",    "P3D Convert",    "emblem-synchronizing-symbolic", PANEL_AREA_CENTER);
    add_panel(tab_paa_preview_,    "paa-preview",    "PAA Preview",    "image-x-generic-symbolic",      PANEL_AREA_CENTER);
    add_panel(tab_config_viewer_,  "config-viewer",  "Config Viewer",  "text-x-generic-symbolic",       PANEL_AREA_CENTER);
    add_panel(tab_audio_,          "audio",          "Audio",          "audio-x-generic-symbolic",      PANEL_AREA_CENTER);
    add_panel(tab_ogg_validate_,   "ogg-validate",   "OGG Validate",   "dialog-warning-symbolic",       PANEL_AREA_CENTER);
    add_panel(tab_conversions_,    "conversions",    "Conversions",    "document-save-as-symbolic",     PANEL_AREA_CENTER);
    add_panel(tab_obj_replace_,    "obj-replace",    "Obj Replace",    "edit-find-replace-symbolic",    PANEL_AREA_CENTER);
    add_panel(tab_wrp_info_,       "wrp-info",       "WRP Info",       "x-office-address-book-symbolic", PANEL_AREA_CENTER);
    add_panel(tab_wrp_project_,    "wrp-project",    "WRP Project",    "folder-new-symbolic",           PANEL_AREA_CENTER);
    add_panel(tab_config_,         "config",         "Configuration",  "preferences-system-symbolic",   PANEL_AREA_CENTER);

    // About tab last, pinned (cannot be moved or closed)
    add_panel(tab_about_,          "about",          "About",          "help-about-symbolic",           PANEL_AREA_CENTER);
    pin_panel("about");

    // Log panel in the bottom dock area
    add_panel(log_panel_,          "log",            "Log",            "utilities-terminal-symbolic",   PANEL_AREA_BOTTOM);
}

void AppWindow::on_reset_layout() {
    // Collect live PanelWidgets from ALL workspaces and torn-off windows to close
    struct ResetData {
        std::vector<PanelWidget*> to_close;
        std::vector<PanelDocumentWorkspace*> extra_windows;
    } rd;

    panel_workbench_foreach_workspace(workbench_,
        [](PanelWorkspace* ws, gpointer ud) {
            auto* d = static_cast<ResetData*>(ud);
            if (PANEL_IS_DOCUMENT_WORKSPACE(ws)) {
                auto* dws = PANEL_DOCUMENT_WORKSPACE(ws);
                auto* dock = panel_document_workspace_get_dock(dws);
                panel_dock_foreach_frame(dock, [](PanelFrame* frame, gpointer ud2) {
                    auto* vec = static_cast<std::vector<PanelWidget*>*>(ud2);
                    for (guint i = 0; i < panel_frame_get_n_pages(frame); i++)
                        vec->push_back(panel_frame_get_page(frame, i));
                }, &d->to_close);
                d->extra_windows.push_back(dws);
            }
        }, &rd);

    g_allow_close_all = true;
    for (auto* pw : rd.to_close)
        panel_widget_force_close(pw);
    g_allow_close_all = false;

    panels_.clear();

    // Close torn-off windows (all workspaces except the primary one)
    for (auto* ws : rd.extra_windows) {
        if (ws != workspace_) {
            panel_workbench_remove_workspace(workbench_, PANEL_WORKSPACE(ws));
            gtk_window_destroy(GTK_WINDOW(ws));
        }
    }

    // Clear saved layout
    layout_cfg_.panels.clear();
    save_layout_config(layout_cfg_);

    // Re-apply default layout (creates fresh PanelWidgets)
    apply_default_layout();

    // Re-hook tear-off on new frames
    hook_tab_views_for_tearoff(GTK_WIDGET(workspace_), this);

    // Reveal the bottom area for the log panel
    panel_dock_set_reveal_bottom(dock_, TRUE);

    app_log(LogLevel::Info, "Layout reset to default");
}

static void detach_panels_from_dock(PanelDock* dock) {
    panel_dock_foreach_frame(dock, [](PanelFrame* frame, gpointer) {
        for (guint i = 0; i < panel_frame_get_n_pages(frame); i++) {
            auto* pw = panel_frame_get_page(frame, i);
            panel_widget_set_child(pw, nullptr);
        }
    }, nullptr);
}

void AppWindow::detach_all_panels() {
    // Unparent gtkmm widgets from all workspaces (main + torn-off)
    panel_workbench_foreach_workspace(workbench_,
        [](PanelWorkspace* ws, gpointer) {
            if (PANEL_IS_DOCUMENT_WORKSPACE(ws)) {
                detach_panels_from_dock(
                    panel_document_workspace_get_dock(PANEL_DOCUMENT_WORKSPACE(ws)));
            }
        }, nullptr);
    panels_.clear();
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

AppWindow::AppWindow(GtkApplication* app) {
    cfg_ = load_config();
    layout_cfg_ = load_layout_config();
    services_.pbo_index_service = std::make_shared<PboIndexService>();
    services_.p3d_model_loader_service.reset();
    services_.lod_textures_loader_service.reset();

    // Create the workbench — manages multiple workspace windows for tear-off
    workbench_ = panel_workbench_new();

    // Create the PanelDocumentWorkspace — this is our main window
    workspace_ = PANEL_DOCUMENT_WORKSPACE(panel_document_workspace_new());
    gtk_window_set_application(GTK_WINDOW(workspace_), app);
    gtk_window_set_title(GTK_WINDOW(workspace_), "ArmA Tools");
    gtk_window_set_default_size(GTK_WINDOW(workspace_), 1100, 700);

    // Register workspace with workbench (enables tear-off to new windows)
    panel_workbench_add_workspace(workbench_, PANEL_WORKSPACE(workspace_));

    // Get the built-in dock, grid, statusbar
    dock_ = panel_document_workspace_get_dock(workspace_);
    grid_ = panel_document_workspace_get_grid(workspace_);
    statusbar_ = panel_document_workspace_get_statusbar(workspace_);

    // Connect create-frame signals so libpanel can create new frames
    // when panels are dragged to new positions.
    g_signal_connect(dock_, "create-frame",
        G_CALLBACK(create_frame_with_hooks), this);
    g_signal_connect(workspace_, "create-frame",
        G_CALLBACK(create_frame_with_hooks), this);

    // Set up the titlebar (Adw HeaderBar)
    auto* header = adw_header_bar_new();
    panel_document_workspace_set_titlebar(workspace_, GTK_WIDGET(header));

    // Add a View menu button to the header bar
    auto* menu = g_menu_new();
    // disabled areas
    // auto* dock_section = g_menu_new();
    // g_menu_append(dock_section, "Toggle Start Panel", "win.reveal-start");
    // g_menu_append(dock_section, "Toggle End Panel", "win.reveal-end");
    // g_menu_append(dock_section, "Toggle Top Panel", "win.reveal-top");
    // g_menu_append(dock_section, "Toggle Bottom Panel", "win.reveal-bottom");
    // g_menu_append_section(menu, "Dock Areas", G_MENU_MODEL(dock_section));

    auto* layout_section = g_menu_new();
    g_menu_append(layout_section, "Reset Layout", "win.reset-layout");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(layout_section));

    auto* menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);
    g_object_unref(menu);
    // g_object_unref(dock_section);
    g_object_unref(layout_section);

    // Set up GActions on the window
    auto* action_group = g_simple_action_group_new();

    // Reset Layout action
    auto* reset_action = g_simple_action_new("reset-layout", nullptr);
    g_signal_connect_swapped(reset_action, "activate",
        G_CALLBACK(+[](AppWindow* self, GVariant*) { self->on_reset_layout(); }), this);
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(reset_action));
    g_object_unref(reset_action);

    // Dock area toggle actions
    auto add_toggle_action = [&](const char* name, PanelArea area) {
        auto* action = g_simple_action_new_stateful(
            name, nullptr, g_variant_new_boolean(FALSE));
        struct ToggleData { PanelDock* dock; PanelArea area; };
        auto* data = new ToggleData{dock_, area};
        g_signal_connect_data(action, "activate",
            G_CALLBACK(+[](GSimpleAction* act, GVariant*, gpointer ud) {
                auto* d = static_cast<ToggleData*>(ud);
                gboolean current = panel_dock_get_reveal_area(d->dock, d->area);
                panel_dock_set_reveal_area(d->dock, d->area, !current);
                g_simple_action_set_state(act, g_variant_new_boolean(!current));
            }),
            data,
            +[](gpointer ud, GClosure*) { delete static_cast<ToggleData*>(ud); },
            GConnectFlags(0));
        g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
        g_object_unref(action);
    };

    add_toggle_action("reveal-start", PANEL_AREA_START);
    add_toggle_action("reveal-end", PANEL_AREA_END);
    add_toggle_action("reveal-top", PANEL_AREA_TOP);
    add_toggle_action("reveal-bottom", PANEL_AREA_BOTTOM);

    gtk_widget_insert_action_group(GTK_WIDGET(workspace_), "win", G_ACTION_GROUP(action_group));
    g_object_unref(action_group);

    // Status bar: add our label
    status_label_.set_hexpand(true);
    status_label_.set_halign(Gtk::Align::START);
    panel_statusbar_add_prefix(statusbar_, 0, GTK_WIDGET(status_label_.gobj()));

    // Set up logging
    set_global_log([this](LogLevel level, const std::string& text) {
        log_panel_.log(level, text);
    });
    log_panel_.set_on_toggle_maximize([this](bool maximized) {
        panel_dock_set_reveal_bottom(dock_, TRUE);
        if (maximized) {
            int win_h = gtk_widget_get_height(GTK_WIDGET(workspace_));
            if (win_h <= 0) win_h = 700;
            const int expanded = std::max(220, static_cast<int>(win_h * 0.72));
            panel_dock_set_bottom_height(dock_, expanded);
        } else {
            panel_dock_set_bottom_height(dock_, 200);
        }
    });

    app_log(LogLevel::Info, "Application started");
    app_log(LogLevel::Info, "Configuration loaded from " + config_path());

    tab_asset_browser_.set_pbo_index_service(services_.pbo_index_service);
    tab_pbo_.set_pbo_index_service(services_.pbo_index_service);
    tab_audio_.set_pbo_index_service(services_.pbo_index_service);
    tab_config_viewer_.set_pbo_index_service(services_.pbo_index_service);
    tab_obj_replace_.set_pbo_index_service(services_.pbo_index_service);
    tab_wrp_info_.set_pbo_index_service(services_.pbo_index_service);
    tab_p3d_info_.set_pbo_index_service(services_.pbo_index_service);
    tab_paa_preview_.set_pbo_index_service(services_.pbo_index_service);

    auto rebuild_model_services =
        [this](const std::shared_ptr<armatools::pboindex::DB>& db,
               const std::shared_ptr<armatools::pboindex::Index>& index) {
            const std::string db_path = cfg_.a3db_path;
            services_.p3d_model_loader_service =
                std::make_shared<P3dModelLoaderService>(&cfg_, db, index);
            services_.lod_textures_loader_service =
                std::make_shared<LodTexturesLoaderService>(db_path, &cfg_, db, index);

            tab_asset_browser_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_asset_browser_.set_texture_loader_service(services_.lod_textures_loader_service);
            tab_p3d_info_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_p3d_info_.set_texture_loader_service(services_.lod_textures_loader_service);
            tab_wrp_info_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_wrp_info_.set_texture_loader_service(services_.lod_textures_loader_service);
        };

    rebuild_model_services(nullptr, nullptr);
    services_.pbo_index_service->subscribe(this, [this, rebuild_model_services](
                                                     const PboIndexService::Snapshot& snap) {
        rebuild_model_services(snap.db, snap.index);
    });

    // Delay initial A3DB open slightly so first paint stays responsive.
    Glib::signal_timeout().connect_once([this]() {
        if (services_.pbo_index_service)
            services_.pbo_index_service->set_db_path(cfg_.a3db_path);
    }, 900);

    // Config save callback
    tab_config_.on_saved = [this]() { reload_config(); };
    init_tabs_lazy();

    // Restore layout or apply default
    if (!layout_cfg_.panels.empty()) {
        // First create all panels (unparented) so we can look them up by id
        auto create_pw = [this](Gtk::Widget& w, const char* id, const char* title, const char* icon) {
            auto* pw = create_dockable_panel({id, title, icon, &w});
            panels_[id] = pw;
        };

        create_pw(tab_asset_browser_,  "asset-browser",  "Asset Browser",  "system-file-manager-symbolic");
        create_pw(tab_pbo_,            "pbo-browser",    "PBO Browser",    "package-x-generic-symbolic");
        create_pw(tab_p3d_info_,       "p3d-info",       "P3D Info",       "emblem-system-symbolic");
        create_pw(tab_p3d_convert_,    "p3d-convert",    "P3D Convert",    "emblem-synchronizing-symbolic");
        create_pw(tab_paa_preview_,    "paa-preview",    "PAA Preview",    "image-x-generic-symbolic");
        create_pw(tab_config_viewer_,  "config-viewer",  "Config Viewer",  "text-x-generic-symbolic");
        create_pw(tab_audio_,          "audio",          "Audio",          "audio-x-generic-symbolic");
        create_pw(tab_ogg_validate_,   "ogg-validate",   "OGG Validate",   "dialog-warning-symbolic");
        create_pw(tab_conversions_,    "conversions",    "Conversions",    "document-save-as-symbolic");
        create_pw(tab_obj_replace_,    "obj-replace",    "Obj Replace",    "edit-find-replace-symbolic");
        create_pw(tab_wrp_info_,       "wrp-info",       "WRP Info",       "x-office-address-book-symbolic");
        create_pw(tab_wrp_project_,    "wrp-project",    "WRP Project",    "folder-new-symbolic");
        create_pw(tab_config_,         "config",         "Configuration",  "preferences-system-symbolic");
        create_pw(log_panel_,          "log",            "Log",            "utilities-terminal-symbolic");
        // create_pw(tab_about_,          "about",          "About",          "help-about-symbolic");
        panels_["about"] = create_simple_panel({"about", "About", "help-about-symbolic", &tab_about_});
        restore_layout();
    } else {
        apply_default_layout();
    }

    // Reveal bottom area if log panel is there
    panel_dock_set_reveal_bottom(dock_, TRUE);
    panel_dock_set_bottom_height(dock_, 200);

    // Hook AdwTabView::create-window on all frames for tear-off support.
    // Deferred to after the window is realized (AdwTabViews exist then).
    g_signal_connect(GTK_WIDGET(workspace_), "realize",
        G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            hook_tab_views_for_tearoff(GTK_WIDGET(self->workspace_), self);
        }), this);

    // Save layout and unparent gtkmm widgets on window close.
    // Must unparent here because by the time ~AppWindow runs, GTK has
    // already destroyed the widget tree (dock_, panels_, etc. are dangling).
    g_signal_connect_swapped(workspace_, "close-request",
        G_CALLBACK(+[](AppWindow* self, GtkWindow*) -> gboolean {
            self->save_layout();
            self->detach_all_panels();
            return FALSE;  // Allow close to proceed
        }), this);
}

AppWindow::~AppWindow() {
    if (services_.pbo_index_service)
        services_.pbo_index_service->unsubscribe(this);
    // Everything was already detached in close-request handler.
    // Just clear the map — the PanelWidgets are owned by GTK.
    panels_.clear();
    if (workbench_)
        g_object_unref(workbench_);
}

void AppWindow::present() {
    gtk_window_present(GTK_WINDOW(workspace_));
}

GtkWindow* AppWindow::gtk_window() const {
    return GTK_WINDOW(workspace_);
}
