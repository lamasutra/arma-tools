#pragma once

#include <algorithm>
#include <iostream>
#include <utility>

#include "console_unicode.h"

namespace armatools::cli {

enum class VerbosityLevel : int { Quiet = 0, Verbose = 1, Debug = 2 };

inline VerbosityLevel current_level = VerbosityLevel::Quiet;

inline void set_verbosity(int level) {
    level = std::clamp(level, 0, 2);
    current_level = static_cast<VerbosityLevel>(level);
}

inline VerbosityLevel verbosity_level() { return current_level; }

inline bool verbose_enabled() { return current_level >= VerbosityLevel::Verbose; }
inline bool debug_enabled() { return current_level >= VerbosityLevel::Debug; }

constexpr const char* level_name(VerbosityLevel level) {
    switch (level) {
        case VerbosityLevel::Quiet: return "QUIET";
        case VerbosityLevel::Verbose: return "VERBOSE";
        case VerbosityLevel::Debug: return "DEBUG";
    }
    return "VERBOSE";
}

constexpr const char* level_emoji(VerbosityLevel level) {
    switch (level) {
        case VerbosityLevel::Quiet: return "🔇";
        case VerbosityLevel::Verbose: return "🔈";
        case VerbosityLevel::Debug: return "🐞";
    }
    return "🔈";
}

inline bool supports_utf() {
    static const bool value = []() {
        auto caps = consoleu::detect_capabilities();
        return caps.has_native_unicode_console || caps.utf8_configured;
    }();
    return value;
}

template <typename... Args>
void log(VerbosityLevel min_level, Args&&... args) {
    if (current_level < min_level) return;
    auto& stream = std::cerr;
    if (supports_utf())
        stream << '[' << level_emoji(min_level) << "] ";
    else
        stream << '[' << level_name(min_level) << "] ";
    if constexpr (sizeof...(Args) > 0) {
        ((stream << std::forward<Args>(args) << ' '), ...);
    }
    stream << '\n';
}

template <typename... Args>
void log_verbose(Args&&... args) {
    log(VerbosityLevel::Verbose, std::forward<Args>(args)...);
}

template <typename... Args>
void log_debug(Args&&... args) {
    log(VerbosityLevel::Debug, std::forward<Args>(args)...);
}

} // namespace armatools::cli
