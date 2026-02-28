#pragma once

// LogLevel defines the severity of a log message.
// Used by app_log() and the log panel to categorize and filter messages.
enum class LogLevel {
    Debug,    // Verbose internal details; only shown with high verbosity settings.
    Info,     // Normal status messages (e.g., "Configuration loaded").
    Warning,  // Something unexpected happened, but the app can continue.
    Error,    // A serious problem that may prevent a feature from working.
};
