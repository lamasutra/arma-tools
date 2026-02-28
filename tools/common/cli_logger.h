#pragma once

#include <algorithm>
#include <iostream>
#include <utility>
#include <chrono>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

#include "console_unicode.h"

namespace armatools::log {

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
void log_impl(VerbosityLevel min_level, Args&&... args) {
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
void info(Args&&... args) {
    log_impl(VerbosityLevel::Verbose, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(Args&&... args) {
    log_impl(VerbosityLevel::Debug, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(Args&&... args) {
    auto& stream = std::cerr;
    if (supports_utf())
        stream << "⚠️ ";
    else
        stream << "[WARN] ";
    if constexpr (sizeof...(Args) > 0) {
        ((stream << std::forward<Args>(args) << ' '), ...);
    }
    stream << '\n';
}

template <typename... Args>
void error(Args&&... args) {
    auto& stream = std::cerr;
    if (supports_utf())
        stream << "❌ ";
    else
        stream << "[ERROR] ";
    if constexpr (sizeof...(Args) > 0) {
        ((stream << std::forward<Args>(args) << ' '), ...);
    }
    stream << '\n';
}

namespace detail {

inline std::mutex once_mutex;
inline std::unordered_set<uint64_t> once_keys;

inline bool should_log_once(uint64_t key) {
    std::lock_guard<std::mutex> lock(once_mutex);
    return once_keys.insert(key).second;
}

inline std::mutex rate_mutex;
inline std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> rate_timestamps;

inline bool should_log_rate(uint64_t key, uint32_t ms) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rate_mutex);
    auto it = rate_timestamps.find(key);
    if (it == rate_timestamps.end() ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() >= ms) {
        rate_timestamps[key] = now;
        return true;
    }
    return false;
}

// Basic FNV-1a hash function for strings
constexpr uint64_t fnv1a_hash(const char* str) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

// Compile time hash of file+line
constexpr uint64_t make_location_key(const char* file, int line) {
    uint64_t hash = fnv1a_hash(file);
    hash ^= static_cast<uint64_t>(line);
    hash *= 1099511628211ull;
    return hash;
}

} // namespace detail

template <typename... Args> void print(Args&&... args) {
    auto& stream = std::cout;
    if constexpr (sizeof...(Args) > 0) { ((stream << std::forward<Args>(args) << ' '), ...); }
    stream << '\n';
}

template <typename... Args> void log_plain(Args&&... args) {
    auto& stream = std::cerr;
    if constexpr (sizeof...(Args) > 0) { ((stream << std::forward<Args>(args) << ' '), ...); }
    stream << '\n';
}

template <typename... Args> void log_stdout(Args&&... args) {
    auto& stream = std::cout;
    if constexpr (sizeof...(Args) > 0) { ((stream << std::forward<Args>(args) << ' '), ...); }
    stream << '\n';
}

template <typename... Args> void log_raw(Args&&... args) {
    auto& stream = std::cerr;
    if constexpr (sizeof...(Args) > 0) { ((stream << std::forward<Args>(args)), ...); }
}

template <typename... Args> void log_stdout_raw(Args&&... args) {
    auto& stream = std::cout;
    if constexpr (sizeof...(Args) > 0) { ((stream << std::forward<Args>(args)), ...); }
}

// Legacy aliases for gentle refactoring
template <typename... Args> void log_verbose(Args&&... args) { info(std::forward<Args>(args)...); }
template <typename... Args> void log_debug(Args&&... args) { debug(std::forward<Args>(args)...); }
template <typename... Args> void log_warning(Args&&... args) { warn(std::forward<Args>(args)...); }
template <typename... Args> void log_error(Args&&... args) { error(std::forward<Args>(args)...); }

} // namespace armatools::log

namespace armatools::cli {
    using namespace armatools::log;
}

// Core Macros
#define LOGI(...) ::armatools::log::info(__VA_ARGS__)
#define LOGW(...) ::armatools::log::warn(__VA_ARGS__)
#define LOGE(...) ::armatools::log::error(__VA_ARGS__)

#if ARMA_DEBUG
    #define LOGD(...) ::armatools::log::debug(__VA_ARGS__)
    
    #define LOGD_ONCE(key, ...) \
        do { \
            if (::armatools::log::detail::should_log_once(key)) { \
                ::armatools::log::debug(__VA_ARGS__); \
            } \
        } while (false)

    #define LOGW_ONCE(key, ...) \
        do { \
            if (::armatools::log::detail::should_log_once(key)) { \
                ::armatools::log::warn(__VA_ARGS__); \
            } \
        } while (false)
        
    #define LOGE_ONCE(key, ...) \
        do { \
            if (::armatools::log::detail::should_log_once(key)) { \
                ::armatools::log::error(__VA_ARGS__); \
            } \
        } while (false)

    #define LOGD_RATE_LIMIT(ms, ...) \
        do { \
            constexpr uint64_t _loc_key = ::armatools::log::detail::make_location_key(__FILE__, __LINE__); \
            if (::armatools::log::detail::should_log_rate(_loc_key, ms)) { \
                ::armatools::log::debug(__VA_ARGS__); \
            } \
        } while (false)
#else
    #define LOGD(...) do {} while(false)
    #define LOGD_ONCE(key, ...) do {} while(false)
    #define LOGD_RATE_LIMIT(ms, ...) do {} while(false)

    // Warnings and Errors still evaluate/check rate in Release, but use regular methods
    #define LOGW_ONCE(key, ...) \
        do { \
            if (::armatools::log::detail::should_log_once(key)) { \
                ::armatools::log::warn(__VA_ARGS__); \
            } \
        } while (false)
        
    #define LOGE_ONCE(key, ...) \
        do { \
            if (::armatools::log::detail::should_log_once(key)) { \
                ::armatools::log::error(__VA_ARGS__); \
            } \
        } while (false)
#endif
