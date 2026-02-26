#pragma once

#include "app/tab_config_presenter.h"
#include "config.h"
#include "log_panel.h"
#include "tab_about.h"
#include "tab_asset_browser.h"
#include "tab_audio.h"
#include "tab_config.h"
#include "tab_config_viewer.h"
#include "tab_conversions.h"
#include "tab_ogg_validate.h"
#include "tab_p3d_convert.h"
#include "tab_p3d_info.h"
#include "tab_paa_preview.h"
#include "tab_pbo.h"
#include "tab_obj_replace.h"
#include "tab_wrp_info.h"
#include "tab_wrp_project.h"
#include "pbo_index_service.h"
#include "p3d_model_loader.h"
#include "textures_loader.h"

#include <libpanel.h>
#include <gtkmm.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>

struct Services {
    std::shared_ptr<PboIndexService> pbo_index_service;
    std::shared_ptr<P3dModelLoaderService> p3d_model_loader_service;
    std::shared_ptr<TexturesLoaderService> textures_loader_service;
};

// AppWindow owns the PanelDocumentWorkspace (a GtkWindow) and all tab widgets.
// It is NOT a Gtk::Window subclass — the actual window is the workspace.
class AppWindow {
public:
    explicit AppWindow(GtkApplication* app);
    ~AppWindow();

    // Present the workspace window
    void present();

    // Get the underlying GtkWindow for the application
    GtkWindow* gtk_window() const;

    // Access workbench (needed by tear-off callback)
    PanelWorkbench* workbench() const { return workbench_; }

    // Update the status bar text (callable from tabs)
    void update_status(const std::string& text);

private:
    Config cfg_;
    LayoutConfig layout_cfg_;
    Services services_;
    TabConfigPresenter tab_config_presenter_;

    // The libpanel workbench manages multiple workspace windows (tear-off)
    PanelWorkbench* workbench_ = nullptr;

    // The primary workspace — this IS the main GtkWindow
    PanelDocumentWorkspace* workspace_ = nullptr;
    PanelDock* dock_ = nullptr;
    PanelGrid* grid_ = nullptr;
    PanelStatusbar* statusbar_ = nullptr;

    // Status label (added to PanelStatusbar)
    Gtk::Label status_label_{"Ready"};

    // Log panel
    LogPanel log_panel_;

    // Tab widgets — these are gtkmm objects whose GtkWidget* are set as
    // children of PanelWidgets via create_dockable_panel().
    TabAbout tab_about_;
    TabAssetBrowser tab_asset_browser_;
    TabPbo tab_pbo_;
    TabP3dInfo tab_p3d_info_;
    TabP3dConvert tab_p3d_convert_;
    TabPaaPreview tab_paa_preview_;
    TabConfigViewer tab_config_viewer_;
    TabAudio tab_audio_;
    TabOggValidate tab_ogg_validate_;
    TabConversions tab_conversions_;
    TabObjReplace tab_obj_replace_;
    TabWrpInfo tab_wrp_info_;
    TabWrpProject tab_wrp_project_;
    TabConfig tab_config_;

    // Map from panel id -> PanelWidget* (not owned, owned by widget tree)
    std::map<std::string, PanelWidget*> panels_;

    // Add a panel to the workspace at the given position
    void add_panel(Gtk::Widget& content, const char* id,
                   const char* title, const char* icon_name,
                   PanelArea area,
                   bool simple_panel = false);

    // Lookup helper for panel catalog descriptors.
    Gtk::Widget* panel_content_by_id(std::string_view panel_id);

    // Reload config from disk and re-apply to all tabs
    void reload_config();
    void register_tab_config_presenter();
    void init_tabs_lazy();

    // Session save/restore
    void save_layout();
    void restore_layout();
    void apply_default_layout();

    // "Reset Layout" action handler
    void on_reset_layout();

    // Pin a panel so it cannot be moved, reordered, or closed
    void pin_panel(const char* id);

    // Unparent gtkmm widgets from PanelWidgets so they survive GTK teardown
    void detach_all_panels();
};
