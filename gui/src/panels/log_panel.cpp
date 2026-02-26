#include "log_panel.h"

#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {
LogFunc g_log_func;
std::mutex g_log_mutex;
std::deque<std::pair<LogLevel, std::string>> g_log_queue;
bool g_log_flush_scheduled = false;
} // namespace

namespace {
bool flush_log_queue_idle() {
    std::deque<std::pair<LogLevel, std::string>> batch;
    LogFunc sink;
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        sink = g_log_func;
        static constexpr size_t kBatchSize = 256;
        size_t count = 0;
        while (!g_log_queue.empty() && count < kBatchSize) {
            batch.push_back(std::move(g_log_queue.front()));
            g_log_queue.pop_front();
            ++count;
        }
        if (g_log_queue.empty()) {
            g_log_flush_scheduled = false;
        }
    }

    if (sink) {
        for (const auto& item : batch) {
            sink(item.first, item.second);
        }
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_flush_scheduled;
}
} // namespace

void set_global_log(LogFunc func) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_func = std::move(func);
}

void app_log(LogLevel level, const std::string& text) {
    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_queue.emplace_back(level, text);
        if (!g_log_flush_scheduled) {
            g_log_flush_scheduled = true;
            schedule = true;
        }
    }
    if (schedule) {
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer) -> gboolean {
                return flush_log_queue_idle() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            },
            nullptr,
            nullptr);
    }
}

namespace {

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    return ss.str();
}

const char* level_prefix(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:   return "[DBG] ";
    case LogLevel::Info:    return "[INF] ";
    case LogLevel::Warning: return "[WRN] ";
    case LogLevel::Error:   return "[ERR] ";
    }
    return "";
}

} // namespace

LogPanel::LogPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
    // --- Toolbar ---
    toolbar_.set_margin_start(4);
    toolbar_.set_margin_end(4);
    toolbar_.set_margin_top(2);
    toolbar_.set_margin_bottom(2);

    title_.set_halign(Gtk::Align::START);
    toolbar_.append(title_);

    // Level filter toggles — all active by default
    filter_debug_.set_active(true);
    filter_debug_.set_tooltip_text("DBG");
    filter_debug_.set_label("");
    filter_debug_.set_icon_name("utilities-terminal-symbolic");
    filter_debug_.set_has_frame(false);
    filter_debug_.add_css_class("log-filter-debug");
    filter_debug_.add_css_class("log-filter-toggle");
    filter_debug_.set_size_request(26, 26);
    toolbar_.append(filter_debug_);

    filter_info_.set_active(true);
    filter_info_.set_tooltip_text("INF");
    filter_info_.set_label("");
    filter_info_.set_icon_name("dialog-information-symbolic");
    filter_info_.set_has_frame(false);
    filter_info_.add_css_class("log-filter-info");
    filter_info_.add_css_class("log-filter-toggle");
    filter_info_.set_size_request(26, 26);
    toolbar_.append(filter_info_);

    filter_warning_.set_active(true);
    filter_warning_.set_tooltip_text("WRN");
    filter_warning_.set_label("");
    filter_warning_.set_icon_name("dialog-warning-symbolic");
    filter_warning_.set_has_frame(false);
    filter_warning_.add_css_class("log-filter-warning");
    filter_warning_.add_css_class("log-filter-toggle");
    filter_warning_.set_size_request(26, 26);
    toolbar_.append(filter_warning_);

    filter_error_.set_active(true);
    filter_error_.set_tooltip_text("ERR");
    filter_error_.set_label("");
    filter_error_.set_icon_name("dialog-error-symbolic");
    filter_error_.set_has_frame(false);
    filter_error_.add_css_class("log-filter-error");
    filter_error_.add_css_class("log-filter-toggle");
    filter_error_.set_size_request(26, 26);
    toolbar_.append(filter_error_);

    // Separator space
    auto* spacer = Gtk::make_managed<Gtk::Label>();
    spacer->set_hexpand(true);
    toolbar_.append(*spacer);

    // Search entry
    search_entry_.set_placeholder_text("Search log...");
    search_entry_.set_tooltip_text("Search in log");
    search_entry_.set_hexpand(false);
    search_entry_.set_size_request(180, -1);
    toolbar_.append(search_entry_);

    // Copy All button
    copy_button_.set_icon_name("edit-copy-symbolic");
    copy_button_.set_tooltip_text("Copy all log text to clipboard");
    copy_button_.set_has_frame(false);
    toolbar_.append(copy_button_);

    // Save Log button
    save_button_.set_icon_name("document-save-symbolic");
    save_button_.set_tooltip_text("Save log to file");
    save_button_.set_has_frame(false);
    toolbar_.append(save_button_);

    // Clear button
    clear_button_.set_icon_name("edit-clear-all-symbolic");
    clear_button_.set_tooltip_text("Clear log");
    clear_button_.set_has_frame(false);
    toolbar_.append(clear_button_);

    // Maximize / Restore
    maximize_button_.set_icon_name("view-fullscreen-symbolic");
    maximize_button_.set_tooltip_text("Maximize log panel");
    maximize_button_.set_has_frame(false);
    toolbar_.append(maximize_button_);

    restore_button_.set_icon_name("view-restore-symbolic");
    restore_button_.set_tooltip_text("Restore log panel");
    restore_button_.set_has_frame(false);
    restore_button_.set_visible(false);
    toolbar_.append(restore_button_);

    Gtk::Box::append(toolbar_);

    // --- Text view ---
    text_view_.set_editable(false);
    text_view_.set_monospace(true);
    text_view_.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    scroll_.set_vexpand(true);
    scroll_.set_child(text_view_);
    Gtk::Box::append(scroll_);

    // --- Text tags for log levels ---
    auto buf = text_view_.get_buffer();
    tag_debug_ = buf->create_tag("debug");
    tag_debug_->property_foreground() = "#888888";
    tag_info_ = buf->create_tag("info");
    tag_info_->property_foreground() = "#2196F3";
    tag_warning_ = buf->create_tag("warning");
    tag_warning_->property_foreground() = "#FF9800";
    tag_error_ = buf->create_tag("error");
    tag_error_->property_foreground() = "#F44336";

    // Highlight tag for search matches
    tag_highlight_ = buf->create_tag("highlight");
    tag_highlight_->property_background() = "#FFFF00";
    tag_highlight_->property_foreground() = "#000000";

    // --- Signals ---
    clear_button_.signal_clicked().connect(sigc::mem_fun(*this, &LogPanel::clear));
    maximize_button_.signal_clicked().connect(sigc::mem_fun(*this, &LogPanel::on_maximize));
    restore_button_.signal_clicked().connect(sigc::mem_fun(*this, &LogPanel::on_restore));
    copy_button_.signal_clicked().connect(sigc::mem_fun(*this, &LogPanel::on_copy_all));
    save_button_.signal_clicked().connect(sigc::mem_fun(*this, &LogPanel::on_save_log));

    filter_debug_.signal_toggled().connect(sigc::mem_fun(*this, &LogPanel::on_filter_toggled));
    filter_info_.signal_toggled().connect(sigc::mem_fun(*this, &LogPanel::on_filter_toggled));
    filter_warning_.signal_toggled().connect(sigc::mem_fun(*this, &LogPanel::on_filter_toggled));
    filter_error_.signal_toggled().connect(sigc::mem_fun(*this, &LogPanel::on_filter_toggled));

    search_entry_.signal_search_changed().connect(sigc::mem_fun(*this, &LogPanel::on_search_changed));
}

void LogPanel::log(LogLevel level, const std::string& text) {
    auto line = timestamp() + " " + level_prefix(level) + text + "\n";
    entries_.push_back({level, line});

    // If this level is currently visible, append directly instead of full rebuild
    if (is_level_visible(level)) {
        Glib::RefPtr<Gtk::TextTag> tag;
        switch (level) {
        case LogLevel::Debug:   tag = tag_debug_;   break;
        case LogLevel::Info:    tag = tag_info_;     break;
        case LogLevel::Warning: tag = tag_warning_;  break;
        case LogLevel::Error:   tag = tag_error_;    break;
        }

        auto buf = text_view_.get_buffer();
        buf->insert_with_tag(buf->end(), line, tag);

        // Re-apply highlight if there is an active search term
        auto search_text = search_entry_.get_text();
        if (!search_text.empty()) {
            apply_highlight();
        }

        auto mark = buf->create_mark(buf->end());
        text_view_.scroll_to(mark);
    }
}

void LogPanel::clear() {
    entries_.clear();
    text_view_.get_buffer()->set_text("");
}

void LogPanel::set_on_toggle_maximize(ToggleMaxFunc func) {
    on_toggle_maximize_ = std::move(func);
}

bool LogPanel::is_level_visible(LogLevel level) const {
    switch (level) {
    case LogLevel::Debug:   return show_debug_;
    case LogLevel::Info:    return show_info_;
    case LogLevel::Warning: return show_warning_;
    case LogLevel::Error:   return show_error_;
    }
    return true;
}

void LogPanel::rebuild_view() {
    auto buf = text_view_.get_buffer();
    buf->set_text("");

    for (const auto& entry : entries_) {
        if (!is_level_visible(entry.level))
            continue;

        Glib::RefPtr<Gtk::TextTag> tag;
        switch (entry.level) {
        case LogLevel::Debug:   tag = tag_debug_;   break;
        case LogLevel::Info:    tag = tag_info_;     break;
        case LogLevel::Warning: tag = tag_warning_;  break;
        case LogLevel::Error:   tag = tag_error_;    break;
        }

        buf->insert_with_tag(buf->end(), entry.text, tag);
    }

    // Re-apply search highlighting after rebuild
    apply_highlight();

    // Scroll to end
    if (buf->get_char_count() > 0) {
        auto mark = buf->create_mark(buf->end());
        text_view_.scroll_to(mark);
    }
}

void LogPanel::apply_highlight() {
    auto buf = text_view_.get_buffer();
    // Remove all existing highlight tags
    buf->remove_tag(tag_highlight_, buf->begin(), buf->end());

    auto search_text = search_entry_.get_text();
    if (search_text.empty())
        return;

    Glib::ustring needle = search_text;
    Glib::ustring haystack = buf->get_text();

    // Case-insensitive search
    Glib::ustring needle_lower = needle.lowercase();
    Glib::ustring haystack_lower = haystack.lowercase();

    Glib::ustring::size_type pos = 0;
    while ((pos = haystack_lower.find(needle_lower, pos)) != Glib::ustring::npos) {
        auto start = buf->get_iter_at_offset(static_cast<int>(pos));
        auto end = buf->get_iter_at_offset(static_cast<int>(pos + needle.size()));
        buf->apply_tag(tag_highlight_, start, end);
        pos += needle.size();
    }
}

void LogPanel::on_filter_toggled() {
    show_debug_   = filter_debug_.get_active();
    show_info_    = filter_info_.get_active();
    show_warning_ = filter_warning_.get_active();
    show_error_   = filter_error_.get_active();
    rebuild_view();
}

void LogPanel::on_copy_all() {
    auto clipboard = get_clipboard();
    // Build text from all entries (unfiltered — copy everything)
    std::string all_text;
    for (const auto& entry : entries_) {
        all_text += entry.text;
    }
    clipboard->set_text(all_text);
}

void LogPanel::on_save_log() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Save Log");
    dialog->set_initial_name("arma_tools.log");

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter_log = Gtk::FileFilter::create();
    filter_log->set_name("Log files");
    filter_log->add_pattern("*.log");
    filters->append(filter_log);
    auto filter_txt = Gtk::FileFilter::create();
    filter_txt->set_name("Text files");
    filter_txt->add_pattern("*.txt");
    filters->append(filter_txt);
    auto filter_all = Gtk::FileFilter::create();
    filter_all->set_name("All files");
    filter_all->add_pattern("*");
    filters->append(filter_all);
    dialog->set_filters(filters);

    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(*win, [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->save_finish(result);
            if (file) {
                std::string path = file->get_path();
                std::ofstream ofs(path);
                if (ofs.is_open()) {
                    for (const auto& entry : entries_) {
                        ofs << entry.text;
                    }
                }
            }
        } catch (const Gtk::DialogError&) {
            // User cancelled — nothing to do
        }
    });
}

void LogPanel::on_search_changed() {
    apply_highlight();
}

void LogPanel::on_maximize() {
    maximized_ = true;
    maximize_button_.set_visible(false);
    restore_button_.set_visible(true);
    if (on_toggle_maximize_) on_toggle_maximize_(true);
}

void LogPanel::on_restore() {
    maximized_ = false;
    maximize_button_.set_visible(true);
    restore_button_.set_visible(false);
    if (on_toggle_maximize_) on_toggle_maximize_(false);
}
