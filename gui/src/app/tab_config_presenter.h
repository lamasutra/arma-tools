#pragma once

#include "config.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

// TabConfigPresenter manages lazy initialization of tab panels with the app Config.
//
// Problem it solves:
//   Many tabs do expensive work on initialization (e.g. scanning the filesystem).
//   We don't want all tabs to initialize at startup — only when they're first
//   shown to the user.
//
// How it works:
//   1. Each tab registers itself with register_tab(), providing a lambda that
//      applies a Config* to the tab (e.g. sets search paths, default values).
//   2. When a panel becomes visible, AppWindow calls ensure_initialized().
//   3. The presenter records that the tab is now initialized so it won't run again.
//   4. When config is reloaded (user saves Config tab), apply_to_initialized()
//      re-applies the new Config only to already-initialized tabs.
class TabConfigPresenter {
public:
    // The callback type: receives a pointer to the current Config.
    using ApplyConfigFn = std::function<void(Config*)>;

    // Register a tab. `id` should match the panel ID (e.g. "asset-browser").
    void register_tab(std::string id, ApplyConfigFn apply_config);

    // If the tab has not been initialized yet, call its ApplyConfigFn now.
    // Returns true if initialization happened, false if already initialized.
    bool ensure_initialized(std::string_view id, Config* cfg);

    // Returns true if the tab has already been initialized.
    [[nodiscard]] bool is_initialized(std::string_view id) const;

    // Re-apply the Config to all tabs that have already been initialized.
    // Called after the user saves new settings in the Config tab.
    void apply_to_initialized(Config* cfg) const;

    // Reset all initialization state (e.g. after a full config reload).
    void reset();

private:
    struct TabEntry {
        ApplyConfigFn apply_config; // Callback to pass Config to the tab.
        bool initialized = false;   // True once ensure_initialized() has been called.
    };

    std::unordered_map<std::string, TabEntry> tabs_;
};

