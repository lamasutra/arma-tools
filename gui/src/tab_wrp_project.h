#pragma once

#include "config.h"

#include <armatools/heightpipe.h>
#include <armatools/wrp.h>
#include <gtkmm.h>
#include <thread>
#include <atomic>

class TabWrpProject : public Gtk::Paned {
public:
    TabWrpProject();
    ~TabWrpProject() override;
    void set_config(Config* cfg);

private:
    Config* cfg_ = nullptr;

    // Left panel: WRP browser + options
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};

    // WRP file browser
    Gtk::Box filter_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry filter_entry_;
    Gtk::Button scan_button_{"Scan"};
    Gtk::Button folder_button_{"Folder..."};
    Gtk::ScrolledWindow list_scroll_;
    Gtk::ListBox file_list_;

    std::string scan_dir_;
    std::vector<std::string> wrp_files_;
    std::vector<std::string> filtered_files_;

    // Output
    Gtk::Box output_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label output_label_{"Output Dir:"};
    Gtk::Entry output_entry_;
    Gtk::Button output_browse_{"Browse..."};

    // Options grid
    Gtk::Grid options_grid_;
    Gtk::Entry offset_x_entry_;
    Gtk::Entry offset_z_entry_;
    Gtk::ComboBoxText hm_scale_combo_;
    Gtk::CheckButton use_heightpipe_check_{"Use heightpipe corrections"};
    Gtk::ComboBoxText heightpipe_preset_combo_;
    Gtk::Entry heightpipe_seed_entry_;
    Gtk::Entry split_entry_;
    Gtk::Entry style_entry_;
    Gtk::Entry replace_entry_;
    Gtk::Button replace_browse_{"Browse..."};
    Gtk::CheckButton extract_p3d_check_{"Extract P3D & textures to drive"};
    Gtk::CheckButton empty_layers_check_{"Empty layers (import objects from txt)"};

    // Action
    Gtk::Box action_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button generate_button_{"Generate Project"};
    Gtk::Button save_defaults_button_{"Save as Defaults"};
    Gtk::Label status_label_;

    // Right panel: heightmap preview + log
    Gtk::Box right_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label hm_info_label_;
    Gtk::ScrolledWindow hm_scroll_;
    Gtk::Picture hm_picture_;
    Gtk::ScrolledWindow log_scroll_;
    Gtk::TextView log_view_;

    // Worker threads
    std::thread worker_;
    std::thread hm_worker_;
    std::thread scan_thread_;
    std::atomic<bool> hm_loading_{false};
    std::atomic<unsigned> scan_generation_{0};
    std::string hm_loaded_path_;
    std::string selected_wrp_path_;

    void on_scan();
    void on_folder_browse();
    void on_filter_changed();
    void on_file_selected(Gtk::ListBoxRow* row);
    void scan_wrp_files(const std::string& dir);
    void update_file_list();

    void on_output_browse();
    void on_replace_browse();
    void on_generate();
    void on_save_defaults();
    void populate_defaults();
    void load_heightmap(const std::string& path);
    static bool apply_heightpipe_to_project(
        const std::string& wrp_path,
        const std::string& output_dir,
        int scale,
        double offset_x,
        double offset_z,
        armatools::heightpipe::CorrectionPreset preset,
        uint32_t seed,
        std::string& log_text);
    Glib::RefPtr<Gdk::Texture> render_heightmap(
        const std::vector<float>& elevations, int grid_x, int grid_y);
};
