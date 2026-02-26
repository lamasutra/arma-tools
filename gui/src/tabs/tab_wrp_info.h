#pragma once

#include "config.h"
#include "gl_wrp_terrain_view.h"
#include "model_view_panel.h"
#include "pbo_index_service.h"
#include "lod_textures_loader.h"

#include <armatools/wrp.h>

#include <gtkmm.h>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <array>

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
    void set_on_open_p3d_info(std::function<void(const std::string&)> cb);

    struct ClassEntry {
        std::string category;
        std::string model_name;
        int count = 0;
    };
    struct ClassListSnapshot {
        struct CategoryGroup {
            std::string name;
            std::vector<ClassEntry> entries;
        };
        std::vector<CategoryGroup> groups;
        int total_objects = 0;
    };

private:
    Config* cfg_ = nullptr;

    // PboIndex (for 3D preview)
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;
    std::shared_ptr<LodTexturesLoaderService> texture_loader_service_;

    // Left panel: file list
    Gtk::Box list_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box filter_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label source_label_{"Source:"};
    Gtk::ComboBoxText source_combo_;
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

    // Page 4: Terrain 3D
    Gtk::Box terrain3d_box_{Gtk::Orientation::VERTICAL};
    Gtk::Box terrain3d_toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label terrain3d_mode_label_{"Mode:"};
    Gtk::ComboBoxText terrain3d_mode_combo_;
    Gtk::CheckButton terrain3d_wireframe_btn_{"Wireframe"};
    Gtk::CheckButton terrain3d_objects_btn_{"Objects"};
    Gtk::CheckButton terrain3d_patch_bounds_btn_{"Patch Bounds"};
    Gtk::CheckButton terrain3d_lod_tint_btn_{"LOD Colors"};
    Gtk::CheckButton terrain3d_tile_bounds_btn_{"Tile Grid"};
    Gtk::Label terrain3d_far_label_{"Far:"};
    Gtk::Scale terrain3d_far_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label terrain3d_mid_label_{"Mat Mid:"};
    Gtk::Scale terrain3d_mid_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label terrain3d_far_mat_label_{"Mat Far:"};
    Gtk::Scale terrain3d_far_mat_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label terrain3d_status_label_;
    std::string terrain3d_base_status_;
    Gtk::Overlay terrain3d_overlay_;
    GLWrpTerrainView terrain3d_view_;
    Gtk::Label terrain3d_debug_overlay_;
    bool allow_texture_mode_ = true;
    bool allow_satellite_mode_ = true;

    // Cached WRP data
    std::unique_ptr<armatools::wrp::WorldData> world_data_;
    std::string loaded_wrp_path_;

    std::vector<ClassEntry> class_entries_;

    std::string scan_dir_;
    struct WrpFileEntry {
        std::string display;
        std::string full_path;
        std::string pbo_path;
        std::string entry_name;
        std::string source;
        bool from_pbo = false;
    };
    std::vector<WrpFileEntry> wrp_files_;
    std::vector<WrpFileEntry> filtered_files_;
    std::string current_source_;
    bool source_combo_updating_ = false;
    WrpFileEntry loaded_wrp_entry_;
    bool loaded_wrp_entry_valid_ = false;
    std::jthread worker_;
    std::jthread objects_worker_;
    std::jthread satellite_worker_;
    std::jthread scan_thread_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> objects_loading_{false};
    std::atomic<bool> satellite_loading_{false};
    bool objects_loaded_ = false;
    bool satellite_loaded_ = false;
    std::vector<std::array<float, 3>> satellite_palette_;
    std::atomic<unsigned> load_generation_{0};
    std::atomic<unsigned> scan_generation_{0};

    void on_scan();
    void on_folder_browse();
    void on_filter_changed();
    void on_source_changed();
    void refresh_source_combo();
    void on_file_selected(Gtk::ListBoxRow* row);
    void scan_wrp_files(const std::string& dir);
    void load_wrp(const WrpFileEntry& entry);
    void update_file_list();
    Glib::RefPtr<Gdk::Texture> render_heightmap(const std::vector<float>& elevations, int grid_x, int grid_y);

    // New methods
    void on_class_selected(Gtk::ListBoxRow* row);
    void on_hm_export();
    void populate_class_list(const ClassListSnapshot& snapshot);
    void on_class_activated(Gtk::ListBoxRow* row);
    void load_p3d_preview(const std::string& model_path);
    void ensure_objects_loaded();
    void ensure_satellite_palette_loaded();
    void update_terrain3d_mode_options(bool allow_texture, bool allow_satellite);
    std::function<void(const std::string&)> on_open_p3d_info_;
};
