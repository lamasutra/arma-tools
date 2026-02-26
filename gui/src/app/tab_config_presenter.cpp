#include "tab_config_presenter.h"

#include <utility>

void TabConfigPresenter::register_tab(std::string id, ApplyConfigFn apply_config) {
    if (id.empty() || !apply_config) return;
    tabs_[std::move(id)] = TabEntry{std::move(apply_config), false};
}

bool TabConfigPresenter::ensure_initialized(std::string_view id, Config* cfg) {
    const auto it = tabs_.find(std::string(id));
    if (it == tabs_.end()) return false;
    if (it->second.initialized) return false;
    it->second.initialized = true;
    it->second.apply_config(cfg);
    return true;
}

bool TabConfigPresenter::is_initialized(std::string_view id) const {
    const auto it = tabs_.find(std::string(id));
    return it != tabs_.end() && it->second.initialized;
}

void TabConfigPresenter::apply_to_initialized(Config* cfg) const {
    for (const auto& [_, tab] : tabs_) {
        if (!tab.initialized) continue;
        tab.apply_config(cfg);
    }
}

void TabConfigPresenter::reset() {
    for (auto& [_, tab] : tabs_) {
        tab.initialized = false;
    }
}
