#pragma once

#include <gtkmm.h>
#include "pbo_index_service.h"
#include "model_view_panel.h"

#include <armatools/pboindex.h>

#include <string>
#include <memory>
#include <vector>

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
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<TexturesLoaderService>& service);
    void open_model_path(const std::string& model_path);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;

    // Left panel: LOD list
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Box source_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label source_label_{"Source:"};
    Gtk::ComboBoxText source_combo_;
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};

    // Right panel: model view
    ModelViewPanel model_panel_;

    // Texture section (in left panel, below LOD list)
    Gtk::Label texture_header_;
    Gtk::ScrolledWindow texture_scroll_;
    Gtk::Box texture_list_{Gtk::Orientation::VERTICAL, 2};

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

    // PboIndex
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;
    std::shared_ptr<P3dModelLoaderService> model_loader_service_;

    // PBO mode UI
    Gtk::Box switch_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Button search_button_{"Search"};
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<armatools::pboindex::FindResult> search_results_data_;
    bool pbo_mode_ = false;
    bool source_combo_updating_ = false;
    std::string current_source_;

    void on_browse();
    void load_file(const std::string& path);
    void on_model_lod_changed(const armatools::p3d::LOD& lod, int idx);
    void update_texture_list(const armatools::p3d::LOD& lod);
    void on_texture_clicked(const std::string& texture_path);

    void on_pbo_mode_changed();
    void refresh_source_combo();
    void on_source_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
    void load_from_pbo(const armatools::pboindex::FindResult& r);
};
