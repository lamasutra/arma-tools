#include "app_window.h"
#include "app/default_panel_catalog.h"
#include "cli_logger.h"
#include "dockable_panel.h"
#include "panel_wrapper.h"
#include "pbo_index_service.h"
#include "render_domain/rd_runtime_state.h"
#include "ui_domain/ui_runtime_config.h"
#include "ui_domain/ui_backend_registry.h"
#include "ui_domain/ui_runtime_state.h"
#include "ui_domain/ui_event_adapter.h"

#include <adwaita.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <set>
#include <vector>

// ---------------------------------------------------------------------------
// Tear-off helpers: find AdwTabView widgets inside the widget tree and
// connect create-window so dragging a tab outside spawns a new window.
// ---------------------------------------------------------------------------

// Forward declarations
static void hook_tab_views_for_tearoff(GtkWidget* dock_or_grid, AppWindow* self);

static std::uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

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
static PanelArea to_panel_area(DockArea area) {
    switch (area) {
        case DockArea::Center: return PANEL_AREA_CENTER;
        case DockArea::Bottom: return PANEL_AREA_BOTTOM;
        case DockArea::Start: return PANEL_AREA_START;
        case DockArea::End: return PANEL_AREA_END;
        case DockArea::Top: return PANEL_AREA_TOP;
    }
    return PANEL_AREA_CENTER;
}

void AppWindow::add_panel(Gtk::Widget& content, const char* id,
                          const char* title, const char* icon_name,
                          PanelArea area,
                          bool simple_panel) {
    auto* pw = simple_panel
        ? create_simple_panel({id, title, icon_name, &content})
        : create_dockable_panel({id, title, icon_name, &content});
    panels_[id] = pw;

    auto pos = panel::make_position(area);
    panel_document_workspace_add_widget(workspace_, pw, pos.get());
}

Gtk::Widget* AppWindow::panel_content_by_id(std::string_view panel_id) {
    if (panel_id == "asset-browser") return &tab_asset_browser_;
    if (panel_id == "pbo-browser") return &tab_pbo_;
    if (panel_id == "p3d-info") return &tab_p3d_info_;
    if (panel_id == "p3d-convert") return &tab_p3d_convert_;
    if (panel_id == "paa-preview") return &tab_paa_preview_;
    if (panel_id == "config-viewer") return &tab_config_viewer_;
    if (panel_id == "audio") return &tab_audio_;
    if (panel_id == "ogg-validate") return &tab_ogg_validate_;
    if (panel_id == "conversions") return &tab_conversions_;
    if (panel_id == "obj-replace") return &tab_obj_replace_;
    if (panel_id == "wrp-info") return &tab_wrp_info_;
    if (panel_id == "wrp-project") return &tab_wrp_project_;
    if (panel_id == "config") return &tab_config_;
    if (panel_id == "about") return &tab_about_;
    if (panel_id == "log") return &log_panel_;
    return nullptr;
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

bool AppWindow::ensure_imgui_overlay_instance() {
    auto& ui_state = ui_domain::runtime_state_mut();
    if (ui_state.overlay_backend_instance && ui_state.overlay_backend_instance->valid()) {
        return true;
    }
    if (!ui_state.backend_instance || !ui_state.backend_instance->valid()) {
        return false;
    }
    if (ui_state.backend_instance->backend_id() != "gtk") {
        return false;
    }
    if (!ui_state.registry_owner) {
        app_log(LogLevel::Warning, "Cannot create imgui overlay instance: UI registry unavailable");
        return false;
    }

    const auto& renderer_state = render_domain::runtime_state();
    if (!renderer_state.ui_render_bridge ||
        !renderer_state.ui_render_bridge->info().available ||
        !renderer_state.ui_render_bridge->bridge_abi()) {
        app_log(LogLevel::Warning,
                "Cannot create imgui overlay instance: renderer UI bridge unavailable");
        return false;
    }

    ui_backend_create_desc_v1 create_desc{};
    create_desc.struct_size = sizeof(ui_backend_create_desc_v1);
    create_desc.overlay_enabled = 1;
    create_desc.render_bridge =
        const_cast<ui_render_bridge_v1*>(renderer_state.ui_render_bridge->bridge_abi());

    auto overlay_instance = std::make_shared<ui_domain::BackendInstance>();
    std::string create_error;
    if (!ui_state.registry_owner->create_instance("imgui", create_desc,
                                                  *overlay_instance, create_error)) {
        app_log(LogLevel::Warning,
                "Cannot create imgui overlay instance: " + create_error);
        return false;
    }

    ui_state.overlay_backend_instance = std::move(overlay_instance);
    ui_state.overlay_backend_id = "imgui";
    app_log(LogLevel::Info, "Created imgui overlay instance at runtime");
    return true;
}

void AppWindow::toggle_ui_overlay() {
    auto& state = ui_domain::runtime_state_mut();
    std::shared_ptr<ui_domain::BackendInstance> overlay_target;
    if (state.overlay_backend_instance && state.overlay_backend_instance->valid()) {
        overlay_target = state.overlay_backend_instance;
    } else if (state.backend_instance && state.backend_instance->valid() &&
               state.backend_instance->backend_id() == "imgui") {
        overlay_target = state.backend_instance;
    } else if (ensure_imgui_overlay_instance()) {
        overlay_target = state.overlay_backend_instance;
    }

    if (!overlay_target) {
        app_log(LogLevel::Warning, "UI overlay toggle ignored: no active UI backend instance");
        return;
    }

    const bool enabled = overlay_target->overlay_enabled();
    const int status = overlay_target->set_overlay_enabled(!enabled);
    if (status < 0) {
        app_log(LogLevel::Warning,
                "UI overlay toggle failed for backend '" +
                    overlay_target->backend_id() + "' (status " +
                    std::to_string(status) + ")");
        return;
    }

    const bool now_enabled = overlay_target->overlay_enabled();
    update_status(std::string("UI overlay ") + (now_enabled ? "enabled" : "disabled"));
    app_log(LogLevel::Info,
            "UI overlay " + std::string(now_enabled ? "enabled" : "disabled") +
                " for backend '" + overlay_target->backend_id() + "'");
}

bool AppWindow::dispatch_ui_event(const ui_event_v1& event) {
    const auto& state = ui_domain::runtime_state();
    const bool primary_is_imgui = state.backend_instance &&
        state.backend_instance->valid() &&
        state.backend_instance->backend_id() == "imgui";

    auto dispatch = [&](const std::shared_ptr<ui_domain::BackendInstance>& instance) -> int {
        if (!instance || !instance->valid()) return UI_STATUS_OK;
        const int status = instance->handle_event(&event);
        if (status < 0) {
            app_log(LogLevel::Warning,
                    "UI event dispatch failed for backend '" +
                        instance->backend_id() +
                        "' (status " + std::to_string(status) + ")");
        }
        return status;
    };

    // Overlay gets first chance to consume pointer/keyboard events.
    const int overlay_status = dispatch(state.overlay_backend_instance);
    // Companion overlays (e.g. imgui on top of gtk) are informative and should
    // never steal input from native GTK widgets.
    if (overlay_status == UI_STATUS_EVENT_CONSUMED && primary_is_imgui) {
        return true;
    }

    const int primary_status = dispatch(state.backend_instance);
    return primary_status == UI_STATUS_EVENT_CONSUMED;
}

bool AppWindow::on_ui_tick() {
    const auto& state = ui_domain::runtime_state();
    const bool has_primary = state.backend_instance && state.backend_instance->valid();
    const bool has_overlay = state.overlay_backend_instance && state.overlay_backend_instance->valid();
    if (!has_primary && !has_overlay) {
        return true;
    }

    float gtk_scale = static_cast<float>(gtk_widget_get_scale_factor(GTK_WIDGET(workspace_)));
    if (!std::isfinite(gtk_scale) || gtk_scale <= 0.0f) {
        gtk_scale = 1.0f;
    }
    const float effective_scale = gtk_scale * ui_user_scale_;
    if (std::fabs(effective_scale - last_effective_ui_scale_) > 0.001f) {
        ui_event_v1 scale_event =
            ui_domain::event_adapter::make_dpi_scale_event(now_ns(), effective_scale);
        dispatch_ui_event(scale_event);
        last_effective_ui_scale_ = effective_scale;
    }

    auto run_frame = [&](const std::shared_ptr<ui_domain::BackendInstance>& instance) {
        if (!instance || !instance->valid()) return;
        const int begin_status = instance->begin_frame(1.0 / 60.0);
        const int draw_status = instance->draw();
        const int end_status = instance->end_frame();
        if (begin_status < 0 || draw_status < 0 || end_status < 0) {
            app_log(LogLevel::Warning,
                    "UI backend frame error (" + instance->backend_id() +
                        "): begin=" + std::to_string(begin_status) +
                        " draw=" + std::to_string(draw_status) +
                        " end=" + std::to_string(end_status));
        }
    };

    run_frame(state.backend_instance);
    run_frame(state.overlay_backend_instance);
    return true;
}

// ---------------------------------------------------------------------------
void AppWindow::reload_config() {
    cfg_ = load_config();
    layout_cfg_ = load_layout_config();
    armatools::cli::log_verbose("Configuration reloaded from {}", config_path());
    tab_config_presenter_.apply_to_initialized(&cfg_);
    update_status("Configuration reloaded");
}

void AppWindow::register_tab_config_presenter() {
    tab_config_presenter_.register_tab("config", [this](Config* cfg) { tab_config_.set_config(cfg); });
    tab_config_presenter_.register_tab("asset-browser", [this](Config* cfg) { tab_asset_browser_.set_config(cfg); });
    tab_config_presenter_.register_tab("pbo-browser", [this](Config* cfg) { tab_pbo_.set_config(cfg); });
    tab_config_presenter_.register_tab("audio", [this](Config* cfg) { tab_audio_.set_config(cfg); });
    tab_config_presenter_.register_tab("ogg-validate", [this](Config* cfg) { tab_ogg_validate_.set_config(cfg); });
    tab_config_presenter_.register_tab("conversions", [this](Config* cfg) { tab_conversions_.set_config(cfg); });
    tab_config_presenter_.register_tab("obj-replace", [this](Config* cfg) { tab_obj_replace_.set_config(cfg); });
    tab_config_presenter_.register_tab("wrp-info", [this](Config* cfg) { tab_wrp_info_.set_config(cfg); });
    tab_config_presenter_.register_tab("wrp-project", [this](Config* cfg) { tab_wrp_project_.set_config(cfg); });
    tab_config_presenter_.register_tab("p3d-convert", [this](Config* cfg) { tab_p3d_convert_.set_config(cfg); });
    tab_config_presenter_.register_tab("p3d-info", [this](Config* cfg) { tab_p3d_info_.set_config(cfg); });
    tab_config_presenter_.register_tab("paa-preview", [this](Config* cfg) { tab_paa_preview_.set_config(cfg); });
    tab_config_presenter_.register_tab("config-viewer", [this](Config* cfg) { tab_config_viewer_.set_config(cfg); });
}

void AppWindow::init_tabs_lazy() {
    auto hook_lazy = [this](Gtk::Widget& widget, const char* tab_id) {
        const std::string id(tab_id);
        auto maybe_init = [this, &widget, id]() {
            if (tab_config_presenter_.is_initialized(id)) return;
            if (!gtk_widget_get_mapped(widget.gobj())) return;
            if (!gtk_widget_get_child_visible(widget.gobj())) return;
            tab_config_presenter_.ensure_initialized(id, &cfg_);
        };
        widget.signal_map().connect(maybe_init);
        Glib::signal_timeout().connect(
            [this, maybe_init, id]() -> bool {
                if (tab_config_presenter_.is_initialized(id)) return false;
                maybe_init();
                return !tab_config_presenter_.is_initialized(id);
            },
            150);
    };

    hook_lazy(tab_config_, "config");
    hook_lazy(tab_asset_browser_, "asset-browser");
    hook_lazy(tab_pbo_, "pbo-browser");
    hook_lazy(tab_audio_, "audio");
    hook_lazy(tab_ogg_validate_, "ogg-validate");
    hook_lazy(tab_conversions_, "conversions");
    hook_lazy(tab_obj_replace_, "obj-replace");
    hook_lazy(tab_wrp_info_, "wrp-info");
    hook_lazy(tab_wrp_project_, "wrp-project");
    hook_lazy(tab_p3d_convert_, "p3d-convert");
    hook_lazy(tab_p3d_info_, "p3d-info");
    hook_lazy(tab_paa_preview_, "paa-preview");
    hook_lazy(tab_config_viewer_, "config-viewer");
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
    for (const auto& descriptor : default_panel_catalog()) {
        auto* content = panel_content_by_id(descriptor.id);
        if (!content) continue;
        add_panel(*content,
                  descriptor.id,
                  descriptor.title,
                  descriptor.icon_name,
                  to_panel_area(descriptor.area),
                  descriptor.simple_panel);
        if (descriptor.pinned) pin_panel(descriptor.id);
    }
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
    services_.textures_loader_service.reset();

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

    auto* motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(
        motion_controller, "motion",
        G_CALLBACK(+[](GtkEventControllerMotion* controller, double x, double y, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            const uint32_t modifiers = static_cast<uint32_t>(
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller)));
            ui_event_v1 event = ui_domain::event_adapter::make_mouse_move_event(
                now_ns(), modifiers, static_cast<float>(x), static_cast<float>(y));
            self->dispatch_ui_event(event);
        }),
        this);
    gtk_widget_add_controller(GTK_WIDGET(workspace_), GTK_EVENT_CONTROLLER(motion_controller));

    auto* click_gesture = gtk_gesture_click_new();
    g_signal_connect(
        click_gesture, "pressed",
        G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            const uint32_t modifiers = static_cast<uint32_t>(
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)));
            const int32_t button = static_cast<int32_t>(
                gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));
            ui_event_v1 event = ui_domain::event_adapter::make_mouse_button_event(
                now_ns(),
                modifiers,
                button,
                true,
                static_cast<float>(x),
                static_cast<float>(y));
            if (self->dispatch_ui_event(event)) {
                gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
            }
        }),
        this);
    g_signal_connect(
        click_gesture, "released",
        G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            const uint32_t modifiers = static_cast<uint32_t>(
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture)));
            const int32_t button = static_cast<int32_t>(
                gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)));
            ui_event_v1 event = ui_domain::event_adapter::make_mouse_button_event(
                now_ns(),
                modifiers,
                button,
                false,
                static_cast<float>(x),
                static_cast<float>(y));
            if (self->dispatch_ui_event(event)) {
                gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
            }
        }),
        this);
    gtk_widget_add_controller(GTK_WIDGET(workspace_), GTK_EVENT_CONTROLLER(click_gesture));

    auto* scroll_controller = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(
        scroll_controller, "scroll",
        G_CALLBACK(+[](GtkEventControllerScroll* controller, double dx, double dy, gpointer ud) -> gboolean {
            auto* self = static_cast<AppWindow*>(ud);
            const uint32_t modifiers = static_cast<uint32_t>(
                gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller)));
            ui_event_v1 event = ui_domain::event_adapter::make_mouse_wheel_event(
                now_ns(), modifiers, static_cast<float>(dx), static_cast<float>(dy));
            const bool consumed = self->dispatch_ui_event(event);
            return static_cast<gboolean>(consumed ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE);
        }),
        this);
    gtk_widget_add_controller(GTK_WIDGET(workspace_), GTK_EVENT_CONTROLLER(scroll_controller));

    auto* key_controller = gtk_event_controller_key_new();
    g_signal_connect(
        key_controller, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer ud) -> gboolean {
            auto* self = static_cast<AppWindow*>(ud);
            ui_event_v1 key_event = ui_domain::event_adapter::make_key_event(
                now_ns(),
                static_cast<uint32_t>(state),
                static_cast<int32_t>(keyval),
                true);
            const bool key_consumed = self->dispatch_ui_event(key_event);

            if (keyval == GDK_KEY_F1) {
                self->toggle_ui_overlay();
                return static_cast<gboolean>(GDK_EVENT_STOP);
            }
            if (key_consumed) {
                return static_cast<gboolean>(GDK_EVENT_STOP);
            }

            const bool has_text_modifiers =
                (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0;
            if (!has_text_modifiers) {
                const gunichar unicode = gdk_keyval_to_unicode(keyval);
                if (unicode != 0 && !g_unichar_iscntrl(unicode)) {
                    char text_utf8[8] = {};
                    const int written = g_unichar_to_utf8(unicode, text_utf8);
                    if (written > 0) {
                        ui_event_v1 text_event =
                            ui_domain::event_adapter::make_text_input_event(
                                now_ns(),
                                static_cast<uint32_t>(state),
                                text_utf8);
                        if (self->dispatch_ui_event(text_event)) {
                            return static_cast<gboolean>(GDK_EVENT_STOP);
                        }
                    }
                }
            }
            return static_cast<gboolean>(GDK_EVENT_PROPAGATE);
        }),
        this);
    g_signal_connect(
        key_controller, "key-released",
        G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            ui_event_v1 event = ui_domain::event_adapter::make_key_event(
                now_ns(),
                static_cast<uint32_t>(state),
                static_cast<int32_t>(keyval),
                false);
            self->dispatch_ui_event(event);
        }),
        this);
    gtk_widget_add_controller(GTK_WIDGET(workspace_), GTK_EVENT_CONTROLLER(key_controller));
    const auto& ui_state = ui_domain::runtime_state();
    auto ui_cfg = ui_domain::load_runtime_config();
    ui_user_scale_ = (std::isfinite(ui_cfg.scale) && ui_cfg.scale > 0.0f) ? ui_cfg.scale : 1.0f;
    std::string ui_preferred = ui_cfg.preferred.empty() ? "auto" : ui_cfg.preferred;
    const bool known_preferred = ui_preferred == "auto" ||
        std::any_of(ui_state.backends.begin(), ui_state.backends.end(),
                    [&ui_preferred](const ui_domain::BackendRecord& backend) {
                        return backend.id == ui_preferred;
                    });
    if (!known_preferred) ui_preferred = "auto";

    // Add a View menu button to the header bar
    auto* menu = g_menu_new();
    // disabled areas
    // auto* dock_section = g_menu_new();
    // g_menu_append(dock_section, "Toggle Start Panel", "win.reveal-start");
    // g_menu_append(dock_section, "Toggle End Panel", "win.reveal-end");
    // g_menu_append(dock_section, "Toggle Top Panel", "win.reveal-top");
    // g_menu_append(dock_section, "Toggle Bottom Panel", "win.reveal-bottom");
    // g_menu_append_section(menu, "Dock Areas", G_MENU_MODEL(dock_section));

    auto* ui_section = g_menu_new();
    auto append_ui_menu_item = [&](const std::string& id, const std::string& label) {
        auto* item = g_menu_item_new(label.c_str(), nullptr);
        g_menu_item_set_action_and_target_value(
            item, "win.set-ui-backend", g_variant_new_string(id.c_str()));
        g_menu_append_item(ui_section, item);
        g_object_unref(item);
    };

    std::string auto_label = "auto | select highest score available";
    if (ui_state.selection.success) {
        auto_label += " | selected=" + ui_state.selection.selected_backend;
        if (!ui_state.selection.message.empty()) {
            auto_label += " | " + ui_state.selection.message;
        }
    }
    append_ui_menu_item("auto", auto_label);
    for (const auto& backend : ui_state.backends) {
        const std::string label = backend.id +
            " | " + (backend.probe.available ? "available" : "unavailable") +
            " | score=" + std::to_string(backend.probe.score) +
            " | reason=" + (backend.probe.reason.empty() ? std::string("-") : backend.probe.reason);
        append_ui_menu_item(backend.id, label);
    }
    g_menu_append_section(menu, "UI Backend", G_MENU_MODEL(ui_section));

    auto* overlay_section = g_menu_new();
    g_menu_append(overlay_section, "Toggle Overlay (F1)", "win.toggle-ui-overlay");
    g_menu_append(overlay_section, "Persist ImGui Overlay", "win.toggle-imgui-overlay-persist");
    g_menu_append_section(menu, "UI Overlay", G_MENU_MODEL(overlay_section));

    auto* ui_scale_section = g_menu_new();
    auto append_ui_scale_item = [&](double scale, const char* label) {
        auto* item = g_menu_item_new(label, nullptr);
        g_menu_item_set_action_and_target_value(
            item, "win.set-ui-scale", g_variant_new_double(scale));
        g_menu_append_item(ui_scale_section, item);
        g_object_unref(item);
    };
    append_ui_scale_item(0.75, "75%");
    append_ui_scale_item(1.0, "100%");
    append_ui_scale_item(1.25, "125%");
    append_ui_scale_item(1.5, "150%");
    g_menu_append_section(menu, "UI Scale", G_MENU_MODEL(ui_scale_section));

    auto* layout_section = g_menu_new();
    g_menu_append(layout_section, "Reset Layout", "win.reset-layout");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(layout_section));

    auto* menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);
    g_object_unref(menu);
    // g_object_unref(dock_section);
    g_object_unref(ui_section);
    g_object_unref(overlay_section);
    g_object_unref(ui_scale_section);
    g_object_unref(layout_section);

    // Set up GActions on the window
    auto* action_group = g_simple_action_group_new();

    // Reset Layout action
    auto* reset_action = g_simple_action_new("reset-layout", nullptr);
    g_signal_connect_swapped(reset_action, "activate",
        G_CALLBACK(+[](AppWindow* self, GVariant*) { self->on_reset_layout(); }), this);
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(reset_action));
    g_object_unref(reset_action);

    // UI backend preference action (persisted to ui.json; takes effect next launch).
    auto* ui_backend_action = g_simple_action_new_stateful(
        "set-ui-backend",
        G_VARIANT_TYPE_STRING,
        g_variant_new_string(ui_preferred.c_str()));
    g_signal_connect_data(
        ui_backend_action, "activate",
        G_CALLBACK(+[](GSimpleAction* action, GVariant* parameter, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            if (!parameter || !g_variant_is_of_type(parameter, G_VARIANT_TYPE_STRING)) {
                return;
            }

            std::string requested = g_variant_get_string(parameter, nullptr);
            if (requested.empty()) requested = "auto";

            if (requested != "auto") {
                const auto& state = ui_domain::runtime_state();
                auto it = std::find_if(state.backends.begin(), state.backends.end(),
                                       [&requested](const ui_domain::BackendRecord& backend) {
                                           return backend.id == requested;
                                       });
                if (it == state.backends.end()) {
                    self->update_status("Cannot select UI backend '" + requested + "': unknown id");
                    app_log(LogLevel::Warning,
                            "Cannot select UI backend '" + requested + "': unknown id");
                    return;
                }
                if (!it->probe.available) {
                    self->update_status(
                        "Cannot select UI backend '" + requested + "': unavailable");
                    app_log(LogLevel::Warning,
                            "Cannot select UI backend '" + requested + "': unavailable (" +
                                (it->probe.reason.empty() ? std::string("-") : it->probe.reason) + ")");
                    return;
                }
            }

            auto cfg = ui_domain::load_runtime_config();
            cfg.preferred = requested;
            if (!ui_domain::save_runtime_config(cfg)) {
                app_log(LogLevel::Warning,
                        "Failed to persist UI backend preference to " +
                            ui_domain::runtime_config_path().string());
                return;
            }

            g_simple_action_set_state(action, g_variant_new_string(requested.c_str()));
            self->update_status("UI backend preference saved: " + requested);
            app_log(LogLevel::Info,
                    "UI backend preference set to '" + requested +
                        "' (restart required)");
        }),
        this, nullptr, GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(ui_backend_action));
    g_object_unref(ui_backend_action);

    // Runtime overlay toggle action (non-persistent, equivalent to F1).
    auto* toggle_overlay_action = g_simple_action_new("toggle-ui-overlay", nullptr);
    g_signal_connect_data(
        toggle_overlay_action, "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            self->toggle_ui_overlay();
        }),
        this, nullptr, GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(toggle_overlay_action));
    g_object_unref(toggle_overlay_action);

    // Persistent overlay preference.
    auto* persist_overlay_action = g_simple_action_new_stateful(
        "toggle-imgui-overlay-persist",
        nullptr,
        g_variant_new_boolean(ui_cfg.imgui_overlay_enabled));
    g_signal_connect_data(
        persist_overlay_action, "activate",
        G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);

            GVariant* state_value = g_action_get_state(G_ACTION(action));
            const bool current = state_value && g_variant_get_boolean(state_value) != 0;
            if (state_value) g_variant_unref(state_value);
            const bool enabled = !current;

            auto cfg = ui_domain::load_runtime_config();
            cfg.imgui_overlay_enabled = enabled;
            if (!ui_domain::save_runtime_config(cfg)) {
                app_log(LogLevel::Warning,
                        "Failed to persist ImGui overlay preference to " +
                            ui_domain::runtime_config_path().string());
                return;
            }

            g_simple_action_set_state(action, g_variant_new_boolean(enabled));

            auto& state = ui_domain::runtime_state_mut();
            std::shared_ptr<ui_domain::BackendInstance> target;
            if (state.overlay_backend_instance && state.overlay_backend_instance->valid()) {
                target = state.overlay_backend_instance;
            } else if (state.backend_instance && state.backend_instance->valid() &&
                       state.backend_instance->backend_id() == "imgui") {
                target = state.backend_instance;
            } else if (enabled && self->ensure_imgui_overlay_instance()) {
                target = state.overlay_backend_instance;
            }

            if (target) {
                const int status = target->set_overlay_enabled(enabled);
                if (status < 0) {
                    app_log(LogLevel::Warning,
                            "Failed to apply runtime ImGui overlay state for backend '" +
                                target->backend_id() + "' (status " + std::to_string(status) + ")");
                }
            } else if (enabled) {
                app_log(LogLevel::Info,
                        "ImGui overlay preference enabled, but no active imgui overlay instance");
            }

            self->update_status(
                std::string("ImGui overlay default ") + (enabled ? "enabled" : "disabled"));
            app_log(LogLevel::Info,
                    "ImGui overlay preference set to " +
                        std::string(enabled ? "enabled" : "disabled"));
        }),
        this, nullptr, GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(persist_overlay_action));
    g_object_unref(persist_overlay_action);

    // Persistent UI scale preference (applied live through the existing DPI event tick path).
    auto* ui_scale_action = g_simple_action_new_stateful(
        "set-ui-scale",
        G_VARIANT_TYPE_DOUBLE,
        g_variant_new_double(static_cast<double>(ui_user_scale_)));
    g_signal_connect_data(
        ui_scale_action, "activate",
        G_CALLBACK(+[](GSimpleAction* action, GVariant* parameter, gpointer ud) {
            auto* self = static_cast<AppWindow*>(ud);
            if (!parameter || !g_variant_is_of_type(parameter, G_VARIANT_TYPE_DOUBLE)) {
                return;
            }

            const double requested_raw = g_variant_get_double(parameter);
            if (!std::isfinite(requested_raw) || requested_raw <= 0.0) {
                self->update_status("UI scale change ignored: invalid value");
                app_log(LogLevel::Warning, "Ignoring invalid UI scale request");
                return;
            }
            const float requested = static_cast<float>(requested_raw);

            auto cfg = ui_domain::load_runtime_config();
            cfg.scale = requested;
            if (!ui_domain::save_runtime_config(cfg)) {
                self->update_status("Failed to persist UI scale preference");
                app_log(LogLevel::Warning,
                        "Failed to persist UI scale preference to " +
                            ui_domain::runtime_config_path().string());
                return;
            }

            self->ui_user_scale_ = requested;
            self->last_effective_ui_scale_ = 0.0f;
            g_simple_action_set_state(action, g_variant_new_double(requested_raw));

            const int pct = static_cast<int>(std::lround(requested_raw * 100.0));
            self->update_status("UI scale set to " + std::to_string(pct) + "%");
            app_log(LogLevel::Info,
                    "UI scale preference set to " + std::to_string(pct) + "%");
        }),
        this, nullptr, GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(ui_scale_action));
    g_object_unref(ui_scale_action);

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
    const auto& renderer_state = render_domain::runtime_state();
    if (renderer_state.selection.success) {
        app_log(LogLevel::Info,
                "Renderer selected: " + renderer_state.selection.selected_backend +
                " (" + renderer_state.selection.message + ")");
    } else if (!renderer_state.selection.message.empty()) {
        app_log(LogLevel::Warning,
                "Renderer selection failed: " + renderer_state.selection.message);
    }
    for (const auto& backend : renderer_state.backends) {
        app_log(LogLevel::Info,
                "Renderer backend " + backend.id +
                " | available=" + (backend.probe.available ? "yes" : "no") +
                " | score=" + std::to_string(backend.probe.score) +
                " | source=" + backend.source +
                " | reason=" + (backend.probe.reason.empty()
                                    ? std::string("-")
                                    : backend.probe.reason));
    }
    if (ui_state.selection.success) {
        app_log(LogLevel::Info,
                "UI backend selected: " + ui_state.selection.selected_backend +
                " (" + ui_state.selection.message + ")");
    } else if (!ui_state.selection.message.empty()) {
        app_log(LogLevel::Warning,
                "UI backend selection failed: " + ui_state.selection.message);
    }
    for (const auto& backend : ui_state.backends) {
        app_log(LogLevel::Info,
                "UI backend " + backend.id +
                " | available=" + (backend.probe.available ? "yes" : "no") +
                " | score=" + std::to_string(backend.probe.score) +
                " | source=" + backend.source +
                " | reason=" + (backend.probe.reason.empty()
                                    ? std::string("-")
                                    : backend.probe.reason));
    }
    if (ui_state.selection.success) {
        update_status(
            "UI backend: " + ui_state.selection.selected_backend + " | " +
            (ui_state.selection.message.empty() ? std::string("selected")
                                                : ui_state.selection.message));
    } else if (!ui_state.selection.message.empty()) {
        update_status("UI backend selection failed: " + ui_state.selection.message);
    }
    register_tab_config_presenter();

    tab_asset_browser_.set_pbo_index_service(services_.pbo_index_service);
    tab_pbo_.set_pbo_index_service(services_.pbo_index_service);
    tab_audio_.set_pbo_index_service(services_.pbo_index_service);
    tab_config_viewer_.set_pbo_index_service(services_.pbo_index_service);
    tab_obj_replace_.set_pbo_index_service(services_.pbo_index_service);
    tab_wrp_info_.set_pbo_index_service(services_.pbo_index_service);
    tab_p3d_info_.set_pbo_index_service(services_.pbo_index_service);
    tab_paa_preview_.set_pbo_index_service(services_.pbo_index_service);
    tab_wrp_info_.set_on_open_p3d_info([this](const std::string& model_path) {
        if (model_path.empty()) return;
        tab_config_presenter_.ensure_initialized("p3d-info", &cfg_);
        tab_p3d_info_.open_model_path(model_path);
        auto it = panels_.find("p3d-info");
        if (it != panels_.end() && it->second)
            panel_widget_raise(it->second);
    });

    auto rebuild_model_services =
        [this](const std::shared_ptr<armatools::pboindex::DB>& db,
               const std::shared_ptr<armatools::pboindex::Index>& index) {
            const std::string db_path = cfg_.a3db_path;
            services_.p3d_model_loader_service =
                std::make_shared<P3dModelLoaderService>(&cfg_, db, index);
                services_.textures_loader_service =
                std::make_shared<TexturesLoaderService>(db_path, &cfg_, db, index);

            tab_asset_browser_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_asset_browser_.set_texture_loader_service(services_.textures_loader_service);
            tab_p3d_info_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_p3d_info_.set_texture_loader_service(services_.textures_loader_service);
            tab_wrp_info_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_wrp_info_.set_texture_loader_service(services_.textures_loader_service);
            tab_obj_replace_.set_model_loader_service(services_.p3d_model_loader_service);
            tab_obj_replace_.set_texture_loader_service(services_.textures_loader_service);
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
        // First create all panels (unparented) so restore can place by id.
        for (const auto& descriptor : default_panel_catalog()) {
            auto* content = panel_content_by_id(descriptor.id);
            if (!content) continue;
            panels_[descriptor.id] = descriptor.simple_panel
                ? create_simple_panel({descriptor.id, descriptor.title, descriptor.icon_name, content})
                : create_dockable_panel({descriptor.id, descriptor.title, descriptor.icon_name, content});
        }
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

    ui_tick_connection_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &AppWindow::on_ui_tick),
        16);
}

AppWindow::~AppWindow() {
    if (ui_tick_connection_.connected()) {
        ui_tick_connection_.disconnect();
    }
    set_global_log({});
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
