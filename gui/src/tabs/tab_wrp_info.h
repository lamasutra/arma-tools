#pragma once

#include "config.h"
#include "model_view_panel.h"
#include "pbo_index_service.h"

#include <armatools/wrp.h>

#include <gtkmm.h>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace armatools::pboindex {
class Index;
class DB;
}

class TabWrpInfo : public Gtk::Paned {
public:
    TabWrpInfo();
    ~TabWrpInfo() override;
    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<LodTexturesLoaderService>& service);

private:
    Config* cfg_ = nullptr;

    // PboIndex (for 3D preview)
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;

    // Left panel: file list
    Gtk::Box list_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box filter_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry filter_entry_;
    Gtk::Button scan_button_{"Scan"};
    Gtk::Button folder_button_{"Folder..."};
    Gtk::ScrolledWindow list_scroll_;
    Gtk::ListBox file_list_;

    // Right panel: notebook with pages
    Gtk::Notebook right_notebook_;

    // Page 1: Info (existing)
    Gtk::ScrolledWindow info_scroll_;
    Gtk::TextView info_view_;

    // Page 2: Objects
    Gtk::Paned objects_paned_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box class_top_box_{Gtk::Orientation::VERTICAL};
    Gtk::Label class_status_label_;
    Gtk::ScrolledWindow class_scroll_;
    Gtk::ListBox class_list_;
    ModelViewPanel model_panel_;

    // Page 3: Heightmap
    Gtk::Box hm_box_{Gtk::Orientation::VERTICAL};
    Gtk::Box hm_toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label hm_scale_label_{"Scale:"};
    Gtk::ComboBoxText hm_scale_combo_;
    Gtk::Button hm_export_button_{"Export..."};
    Gtk::ScrolledWindow hm_scroll_;
    Gtk::Picture hm_picture_;

    // Cached WRP data
    std::unique_ptr<armatools::wrp::WorldData> world_data_;
    std::string loaded_wrp_path_;

    // Class list data
    struct ClassEntry {
        std::string category;
        std::string model_name;
        int count = 0;
    };
    std::vector<ClassEntry> class_entries_;

    std::string scan_dir_;
    std::vector<std::string> wrp_files_;
    std::vector<std::string> filtered_files_;
    std::thread worker_;
    std::thread scan_thread_;
    std::atomic<bool> loading_{false};
    std::atomic<unsigned> scan_generation_{0};

    void on_scan();
    void on_folder_browse();
    void on_filter_changed();
    void on_file_selected(Gtk::ListBoxRow* row);
    void scan_wrp_files(const std::string& dir);
    void load_wrp(const std::string& path);
    void update_file_list();
    Glib::RefPtr<Gdk::Texture> render_heightmap(const std::vector<float>& elevations, int grid_x, int grid_y);

    // New methods
    void on_class_selected(Gtk::ListBoxRow* row);
    void on_hm_export();
    void populate_class_list();
    void load_p3d_preview(const std::string& model_path);
};
