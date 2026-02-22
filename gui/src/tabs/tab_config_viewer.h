#pragma once

#include "config.h"
#include "pbo_index_service.h"

#include <gtkmm.h>
#include <armatools/config.h>
#include <armatools/pboindex.h>

#include <memory>
#include <string>
#include <vector>

class TabConfigViewer : public Gtk::Paned {
public:
    TabConfigViewer();
    ~TabConfigViewer() override;

    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;

    // --- Left panel ---
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};

    // Path / browse row
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};

    // PBO mode UI
    Gtk::Box switch_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Button search_button_{"Search"};
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<armatools::pboindex::FindResult> search_results_data_;
    bool pbo_mode_ = false;

    // Info label
    Gtk::Label info_label_;

    // Tree view for config hierarchy
    class TreeColumns : public Gtk::TreeModelColumnRecord {
    public:
        TreeColumns() { add(col_name); add(col_value); add(col_type); }
        Gtk::TreeModelColumn<Glib::ustring> col_name;
        Gtk::TreeModelColumn<Glib::ustring> col_value;
        Gtk::TreeModelColumn<Glib::ustring> col_type;
    };
    TreeColumns tree_columns_;
    Glib::RefPtr<Gtk::TreeStore> tree_store_;
    Gtk::ScrolledWindow tree_scroll_;
    Gtk::TreeView tree_view_;

    // --- Right panel ---
    Gtk::Box right_box_{Gtk::Orientation::VERTICAL, 4};

    // Text view with syntax highlighting
    Gtk::ScrolledWindow text_scroll_;
    Gtk::TextView text_view_;

    // Text buffer tags for syntax highlighting
    Glib::RefPtr<Gtk::TextBuffer::Tag> tag_keyword_;
    Glib::RefPtr<Gtk::TextBuffer::Tag> tag_string_;
    Glib::RefPtr<Gtk::TextBuffer::Tag> tag_number_;
    Glib::RefPtr<Gtk::TextBuffer::Tag> tag_comment_;
    Glib::RefPtr<Gtk::TextBuffer::Tag> tag_search_match_;

    // Search bar at bottom of right panel
    Gtk::Box search_bar_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::SearchEntry text_search_entry_;
    Gtk::Label match_count_label_;

    // Kept alive for tree population
    armatools::config::Config current_cfg_;
    bool has_config_ = false;

    void on_browse();
    void load_file(const std::string& path);

    // PBO mode
    void on_pbo_mode_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
    void load_from_pbo(const armatools::pboindex::FindResult& r);
    void load_config_data(std::istream& stream, const std::string& ext,
                          const std::string& display_name);

    // Tree view population
    void populate_tree(const armatools::config::ConfigClass& cls,
                       Gtk::TreeModel::Row* parent = nullptr);

    // Syntax highlighting
    void setup_tags();
    void apply_highlighting();

    // Text search
    void on_text_search_changed();
};
