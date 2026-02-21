#pragma once

#include "config.h"
#include "model_view_panel.h"
#include "pbo_index_service.h"

#include <gtkmm.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace armatools::pboindex {
class Index;
class DB;
}

struct ObjReplEntry {
    std::string old_model;
    std::string new_model;  // "unmatched" if not mapped; "a;b;c" for multi-match
    int count = 0;          // instance count from WRP (0 if no WRP loaded)

    bool is_matched() const;
    bool is_multi_match() const; // true if new_model contains ";"
};

class TabObjReplace : public Gtk::Box {
public:
    TabObjReplace();
    ~TabObjReplace() override;

    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;

    // PboIndex (for P3D preview)
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;

    // Toolbar rows
    Gtk::Box toolbar_{Gtk::Orientation::VERTICAL, 4};

    Gtk::Box repl_row_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label repl_label_{"Replacements:"};
    Gtk::Entry repl_entry_;
    Gtk::Button repl_browse_{"Browse"};
    Gtk::Button repl_load_{"Load"};

    Gtk::Box wrp_row_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label wrp_label_{"WRP:"};
    Gtk::Entry wrp_entry_;
    Gtk::Button wrp_browse_{"Browse"};
    Gtk::Button wrp_load_{"Load WRP"};

    Gtk::Box filter_row_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label filter_label_{"Filter:"};
    Gtk::Entry filter_entry_;
    Gtk::Button set_unmatched_button_{"Set Unmatched To..."};
    Gtk::Button auto_match_button_{"Auto-Match"};
    Gtk::Button save_button_{"Save"};
    Gtk::Button save_as_button_{"Save As"};

    // Main content: table + preview
    Gtk::Paned main_paned_{Gtk::Orientation::VERTICAL};

    // Table (ColumnView)
    Gtk::Box table_box_{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow table_scroll_;
    Gtk::ColumnView table_view_;
    Glib::RefPtr<Gio::ListStore<Gtk::StringObject>> table_model_;
    GtkCustomFilter* table_filter_c_ = nullptr;  // C API (no gtkmm wrapper in 4.10)
    Glib::RefPtr<Gtk::FilterListModel> filter_model_;
    Glib::RefPtr<Gtk::SortListModel> sort_model_;
    Glib::RefPtr<Gtk::SingleSelection> table_selection_;

    // Preview
    Gtk::Box preview_box_{Gtk::Orientation::VERTICAL};
    Gtk::Label status_label_;
    Gtk::Box preview_toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::ToggleButton sync_button_{"Sync"};
    Gtk::CheckButton auto_extract_textures_check_{"Auto-extract textures"};
    Gtk::Paned preview_paned_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box left_preview_box_{Gtk::Orientation::VERTICAL};
    Gtk::Label left_label_{"Old Model"};
    ModelViewPanel left_model_panel_;
    Gtk::Box right_preview_box_{Gtk::Orientation::VERTICAL};
    Gtk::Label right_label_{"New Model"};
    ModelViewPanel right_model_panel_;

    // Camera sync
    sigc::connection left_cam_conn_;
    sigc::connection right_cam_conn_;
    void on_sync_toggled();
    void sync_camera_left_to_right();
    void sync_camera_right_to_left();

    // Data
    std::vector<ObjReplEntry> entries_;
    bool dirty_ = false;
    std::string current_file_;

    // Background loading
    std::thread worker_;
    std::thread auto_extract_thread_;
    std::atomic<bool> loading_{false};
    std::atomic<bool> auto_extract_busy_{false};
    std::mutex auto_extract_mutex_;
    std::set<std::string> auto_extract_pending_textures_;
    std::string auto_extract_pending_drive_root_;

    // Actions
    void on_repl_browse();
    void on_repl_load();
    void on_wrp_browse();
    void on_wrp_load();
    void on_filter_changed();
    void on_save();
    void on_save_as();
    void on_selection_changed();
    void on_table_activate(guint position);

    // Unsaved changes confirmation
    void check_unsaved_changes(std::function<void()> proceed_callback);

    // Batch operations
    void on_set_unmatched_to();
    void on_auto_match();

    // Replacement file I/O
    void load_replacement_file(const std::string& path);
    void save_replacement_file(const std::string& path);

    // WRP loading
    void load_wrp_file(const std::string& path);

    // Table management
    void rebuild_model();
    void refresh_all();
    void update_status_label();

    // Preview
    void show_preview(const std::string& old_model, const std::string& new_model);
    void load_p3d_into_panel(ModelViewPanel& panel, Gtk::Label& label,
                             const std::string& model_path);
    void enqueue_auto_extract_textures(const std::set<std::string>& textures);
    void start_auto_extract_worker(std::set<std::string> textures,
                                   std::string drive_root);

    // Edit dialog
    void show_edit_dialog(size_t entry_index);

    // Helpers
    size_t entry_index_at(guint position) const;
};
