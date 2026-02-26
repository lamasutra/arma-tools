#include "log_panel_presenter.h"

#include <utility>

void LogPanelPresenter::append(LogLevel level, std::string line) {
    entries_.push_back(Entry{level, std::move(line)});
}

void LogPanelPresenter::clear() {
    entries_.clear();
}

void LogPanelPresenter::set_level_visible(LogLevel level, bool visible) {
    switch (level) {
        case LogLevel::Debug:
            show_debug_ = visible;
            break;
        case LogLevel::Info:
            show_info_ = visible;
            break;
        case LogLevel::Warning:
            show_warning_ = visible;
            break;
        case LogLevel::Error:
            show_error_ = visible;
            break;
    }
}

bool LogPanelPresenter::is_level_visible(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:
            return show_debug_;
        case LogLevel::Info:
            return show_info_;
        case LogLevel::Warning:
            return show_warning_;
        case LogLevel::Error:
            return show_error_;
    }
    return true;
}

void LogPanelPresenter::set_search_query(std::string query) {
    search_query_ = std::move(query);
}

const std::string& LogPanelPresenter::search_query() const {
    return search_query_;
}

std::vector<const LogPanelPresenter::Entry*> LogPanelPresenter::visible_entries() const {
    std::vector<const Entry*> visible;
    visible.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (!is_level_visible(entry.level)) continue;
        visible.push_back(&entry);
    }
    return visible;
}

std::string LogPanelPresenter::all_text() const {
    std::string text;
    for (const auto& entry : entries_) {
        text += entry.line;
    }
    return text;
}

bool LogPanelPresenter::set_maximized(bool maximized) {
    if (maximized_ == maximized) return false;
    maximized_ = maximized;
    return true;
}

bool LogPanelPresenter::maximized() const {
    return maximized_;
}
