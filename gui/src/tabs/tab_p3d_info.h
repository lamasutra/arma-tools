#pragma once

#include <gtkmm.h>
#include "pbo_index_service.h"
#include "model_view_panel.h"

#include <armatools/pboindex.h>

#include <string>
#include <unordered_set>
#include <memory>
#include <vector>
#include <thread>

struct Config;

namespace armatools::p3d {
struct LOD;
}

class TabP3dInfo : public Gtk::Paned {
public:
    TabP3dInfo();
    ~TabP3dInfo() override;

    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;

    // Left panel: LOD list
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};
    Gtk::Label model_info_label_;
    Gtk::Label lod_header_;
    Gtk::ScrolledWindow lod_scroll_;
    Gtk::ListBox lod_list_;

    // Toolbar (tab-specific controls only)
    Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::CheckButton auto_extract_check_{"Auto-extract"};

    // Right panel: model view
    ModelViewPanel model_panel_;

    // Texture section (in left panel, below LOD list)
    Gtk::Label texture_header_;
    Gtk::ScrolledWindow texture_scroll_;
    Gtk::Box texture_list_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box extract_row_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button extract_button_;
    Gtk::Spinner extract_spinner_;
    Gtk::Label extract_status_;

    // Floating texture preview
    std::unique_ptr<Gtk::Window> texture_preview_window_;
    Gtk::Picture texture_preview_picture_;

    // Detail text
    Gtk::ScrolledWindow detail_scroll_;
    Gtk::TextView detail_view_;

    // Parsed data (kept alive for LOD selection)
    struct ModelData;
    std::shared_ptr<ModelData> model_;
    std::string model_path_;

    // Missing textures for current LOD
    std::vector<std::string> missing_textures_;

    // Extraction thread
    std::thread extract_thread_;

    // PboIndex
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;

    // PBO mode UI
    Gtk::Box switch_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Button search_button_{"Search"};
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<armatools::pboindex::FindResult> search_results_data_;
    bool pbo_mode_ = false;

    void on_browse();
    void load_file(const std::string& path);
    void on_lod_selected(Gtk::ListBoxRow* row);
    void update_texture_list(const armatools::p3d::LOD& lod);
    void on_texture_clicked(const std::string& texture_path);
    void extract_missing_textures();
    bool resolve_texture_on_disk(const std::string& texture) const;

    void on_pbo_mode_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
    void load_from_pbo(const armatools::pboindex::FindResult& r);
};
