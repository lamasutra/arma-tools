#pragma once

#include <gtkmm.h>
#include <functional>
#include <string>
#include <vector>

enum class LogLevel { Debug, Info, Warning, Error };

// Global log function — set by AppWindow, callable from any tab.
using LogFunc = std::function<void(LogLevel, const std::string&)>;
void set_global_log(LogFunc func);
void app_log(LogLevel level, const std::string& text);

class LogPanel : public Gtk::Box {
public:
    LogPanel();

    void log(LogLevel level, const std::string& text);
    void clear();

    using ToggleMaxFunc = std::function<void(bool maximized)>;
    void set_on_toggle_maximize(ToggleMaxFunc func);

private:
    // A single stored log entry
    struct LogEntry {
        LogLevel level;
        std::string text; // fully formatted line including timestamp and prefix
    };

    // All log entries kept in memory for filtering/export
    std::vector<LogEntry> entries_;

    // --- Toolbar widgets ---
    Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label title_{"Log"};

    // Level filter toggle buttons
    Gtk::ToggleButton filter_debug_{"DBG"};
    Gtk::ToggleButton filter_info_{"INF"};
    Gtk::ToggleButton filter_warning_{"WRN"};
    Gtk::ToggleButton filter_error_{"ERR"};

    // Copy / Save / Clear
    Gtk::Button copy_button_;
    Gtk::Button save_button_;
    Gtk::Button clear_button_;

    // Maximize / Restore
    Gtk::Button maximize_button_;
    Gtk::Button restore_button_;

    // Search
    Gtk::SearchEntry search_entry_;

    // --- Text view ---
    Gtk::ScrolledWindow scroll_;
    Gtk::TextView text_view_;

    // --- Text tags ---
    Glib::RefPtr<Gtk::TextTag> tag_debug_;
    Glib::RefPtr<Gtk::TextTag> tag_info_;
    Glib::RefPtr<Gtk::TextTag> tag_warning_;
    Glib::RefPtr<Gtk::TextTag> tag_error_;
    Glib::RefPtr<Gtk::TextTag> tag_highlight_;

    // --- Filter state ---
    bool show_debug_   = true;
    bool show_info_    = true;
    bool show_warning_ = true;
    bool show_error_   = true;

    // --- Maximize state ---
    bool maximized_ = false;
    ToggleMaxFunc on_toggle_maximize_;

    // --- Internal helpers ---
    void rebuild_view();
    void apply_highlight();
    bool is_level_visible(LogLevel level) const;

    void on_filter_toggled();
    void on_copy_all();
    void on_save_log();
    void on_search_changed();
    void on_maximize();
    void on_restore();
};
