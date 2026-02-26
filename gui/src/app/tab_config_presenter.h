#pragma once

#include "config.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

class TabConfigPresenter {
public:
    using ApplyConfigFn = std::function<void(Config*)>;

    void register_tab(std::string id, ApplyConfigFn apply_config);
    bool ensure_initialized(std::string_view id, Config* cfg);
    [[nodiscard]] bool is_initialized(std::string_view id) const;
    void apply_to_initialized(Config* cfg) const;
    void reset();

private:
    struct TabEntry {
        ApplyConfigFn apply_config;
        bool initialized = false;
    };

    std::unordered_map<std::string, TabEntry> tabs_;
};
