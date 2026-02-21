#pragma once

#include "config.h"
#include "pbo_index_service.h"

#include <gtkmm.h>
#include <armatools/pboindex.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

class TabPbo : public Gtk::Paned {
public:
    TabPbo();
    ~TabPbo() override;
    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;

    // Left panel: file list
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};
    Gtk::Button search_button_{"Search"};
    Gtk::Spinner search_spinner_;
    Gtk::Label search_count_label_;

    // PBO search results (index mode)
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<std::string> search_results_paths_;
    bool pbo_mode_ = false;

    // PBO entry list (file mode)
    Gtk::ScrolledWindow list_scroll_;
    Gtk::ListBox entry_list_;

    // Right panel: info + extract
    Gtk::Box right_box_{Gtk::Orientation::VERTICAL, 8};
    Gtk::Label pbo_info_label_;
    Gtk::ScrolledWindow info_scroll_;
    Gtk::TextView info_view_;
    Gtk::Box extract_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry extract_dir_entry_;
    Gtk::Button extract_browse_{"Browse..."};
    Gtk::Button extract_button_{"Extract All"};
    Gtk::Button extract_selected_button_{"Extract Selected"};
    Gtk::Label status_label_;

    // Background thread
    std::thread worker_;

    void on_browse();
    void load_pbo(const std::string& path);
    void on_entry_selected(Gtk::ListBoxRow* row);
    void on_extract();
    void on_extract_selected();
    void on_extract_browse();

    void on_pbo_mode_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
};
