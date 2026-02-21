#pragma once

#include "config.h"

#include <gtkmm.h>
#include <functional>
#include <map>

class TabConfig : public Gtk::Box {
public:
    TabConfig();
    void set_config(Config* cfg);

    // 7d: Callback invoked after config is saved to disk
    std::function<void()> on_saved;

private:
    Config* cfg_ = nullptr;

    Gtk::Notebook notebook_;
    Gtk::Button save_button_{"Save Configuration"};

    // General tab
    Gtk::ScrolledWindow general_scroll_;
    Gtk::Box general_box_{Gtk::Orientation::VERTICAL, 8};
    struct PathRow {
        Gtk::Box box{Gtk::Orientation::HORIZONTAL, 4};
        Gtk::Label label;
        Gtk::Entry entry;
        Gtk::Button browse{"Browse..."};
    };
    std::vector<std::unique_ptr<PathRow>> path_rows_;
    Gtk::ComboBoxText tool_verbosity_combo_;

    // Asset Browser tab
    Gtk::ScrolledWindow asset_scroll_;
    Gtk::Box asset_box_{Gtk::Orientation::VERTICAL, 8};
    Gtk::CheckButton auto_derap_{"Auto-derap PBO configs"};
    Gtk::CheckButton on_demand_metadata_{"On-demand metadata loading"};

    // Wrp Project tab
    Gtk::ScrolledWindow wrp_scroll_;
    Gtk::Box wrp_box_{Gtk::Orientation::VERTICAL, 8};
    struct WrpRow {
        Gtk::Box box{Gtk::Orientation::HORIZONTAL, 4};
        Gtk::Label label;
        Gtk::Entry entry;
    };
    std::vector<std::unique_ptr<WrpRow>> wrp_rows_;
    Gtk::Box style_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label style_label_{"Style"};
    Gtk::Entry style_entry_;
    Gtk::Button style_browse_{"Browse..."};
    Gtk::CheckButton wrp_extract_p3d_{"Extract P3D & textures to drive"};
    Gtk::CheckButton wrp_empty_layers_{"Empty layers (import objects from txt)"};
    Gtk::CheckButton wrp_use_heightpipe_{"Use heightpipe corrections"};
    Gtk::ComboBoxText hm_scale_combo_;
    Gtk::ComboBoxText heightpipe_preset_combo_;
    Gtk::Entry heightpipe_seed_entry_;

    // Binaries tab
    Gtk::ScrolledWindow binaries_scroll_;
    Gtk::Box binaries_box_{Gtk::Orientation::VERTICAL, 8};
    struct BinaryRow {
        Gtk::Box box{Gtk::Orientation::HORIZONTAL, 4};
        Gtk::Label label;
        Gtk::Entry entry;
        Gtk::Button browse{"Browse..."};
    };
    std::vector<std::unique_ptr<BinaryRow>> binary_rows_;
    Gtk::Box search_fill_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry search_dir_entry_;
    Gtk::Button search_dir_browse_{"Browse..."};
    Gtk::Button search_fill_button_{"Search & Fill"};

    void build_general_tab();
    void build_asset_tab();
    void build_wrp_tab();
    void build_binaries_tab();
    void populate_from_config();
    void save_to_config();
    void on_browse_path(Gtk::Entry& entry, bool directory);
    void on_search_fill();
};
