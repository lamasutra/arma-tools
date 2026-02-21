#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace consoleu {

enum class EmojiMode { Auto, On, Off };

struct Capabilities {
    bool stdout_is_tty = false;
    bool utf8_configured = false;
    bool has_native_unicode_console = false;
    bool likely_emoji_ok = false;
    std::string details;
};

Capabilities detect_capabilities();

void write_stdout_utf8(std::string_view utf8);

void write_stdout_with_fallback(std::string_view utf8_preferred,
                                std::string_view ascii_fallback,
                                EmojiMode mode = EmojiMode::Auto);

std::string to_ascii_fallback(std::string_view s, char replacement = '?');

} // namespace consoleu
