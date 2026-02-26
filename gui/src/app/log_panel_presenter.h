#pragma once

#include "domain/log_level.h"

#include <string>
#include <vector>

class LogPanelPresenter {
public:
    struct Entry {
        LogLevel level = LogLevel::Info;
        std::string line;
    };

    void append(LogLevel level, std::string line);
    void clear();

    void set_level_visible(LogLevel level, bool visible);
    [[nodiscard]] bool is_level_visible(LogLevel level) const;

    void set_search_query(std::string query);
    [[nodiscard]] const std::string& search_query() const;

    [[nodiscard]] std::vector<const Entry*> visible_entries() const;
    [[nodiscard]] std::string all_text() const;

    [[nodiscard]] bool set_maximized(bool maximized);
    [[nodiscard]] bool maximized() const;

private:
    std::vector<Entry> entries_;
    bool show_debug_ = true;
    bool show_info_ = true;
    bool show_warning_ = true;
    bool show_error_ = true;
    std::string search_query_;
    bool maximized_ = false;
};
