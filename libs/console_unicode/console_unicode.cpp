#include "console_unicode.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <locale>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <langinfo.h>
#include <locale.h>
#include <strings.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace consoleu {

namespace {

std::string lowercase(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool equals_ignore_case(const char* a, std::string_view b) {
    if (!a) return false;
    std::string_view av{a};
    if (av.size() != b.size()) return false;
    for (size_t i = 0; i < av.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(av[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

Capabilities detect_capabilities() {
    static const Capabilities cached = []() {
        Capabilities caps;

#if defined(_WIN32)
        caps.stdout_is_tty = _isatty(_fileno(stdout));
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        bool has_console = hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode);
        caps.has_native_unicode_console = caps.stdout_is_tty && has_console;
        caps.utf8_configured = GetConsoleOutputCP() == 65001;
        auto* wt = std::getenv("WT_SESSION");
        caps.likely_emoji_ok = caps.has_native_unicode_console &&
                               (caps.utf8_configured || (wt && wt[0]));
        std::ostringstream os;
        os << "CP=" << GetConsoleOutputCP()
           << " console=" << (has_console ? "yes" : "no")
           << " wt=" << (wt && wt[0] ? wt : "unset");
        caps.details = os.str();
#else
        caps.stdout_is_tty = isatty(STDOUT_FILENO);
        setlocale(LC_ALL, "");
        auto codeset = nl_langinfo(CODESET);
        caps.utf8_configured = codeset && equals_ignore_case(codeset, "utf-8");
        auto* term = std::getenv("TERM");
        bool term_ok = term && term[0] && strcasecmp(term, "dumb") != 0;
        caps.likely_emoji_ok = caps.stdout_is_tty && caps.utf8_configured && term_ok;
        std::ostringstream os;
        os << "codeset=" << (codeset ? codeset : "unknown")
           << " term=" << (term ? term : "unset");
        caps.details = os.str();
#endif
        return caps;
    }();
    return cached;
}

void write_stdout_utf8(std::string_view utf8) {
#if defined(_WIN32)
    auto caps = detect_capabilities();
    if (caps.has_native_unicode_console) {
        int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (required > 0) {
            std::wstring wide(required, 0);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                utf8.data(), static_cast<int>(utf8.size()),
                                wide.data(), required);
            DWORD written = 0;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wide.data(),
                          static_cast<DWORD>(wide.size()), &written, nullptr);
            return;
        }
    }
    std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
#else
    std::cout.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
#endif
}

void write_stdout_with_fallback(std::string_view utf8_preferred,
                                std::string_view ascii_fallback,
                                EmojiMode mode) {
    auto caps = detect_capabilities();
    bool prefer = false;
    switch (mode) {
        case EmojiMode::On:
            prefer = true;
            break;
        case EmojiMode::Off:
            prefer = false;
            break;
        case EmojiMode::Auto:
            prefer = caps.likely_emoji_ok;
            break;
    }
    if (prefer) {
        write_stdout_utf8(utf8_preferred);
    } else {
        if (!ascii_fallback.empty()) {
            write_stdout_utf8(ascii_fallback);
        } else {
            write_stdout_utf8(to_ascii_fallback(utf8_preferred));
        }
    }
}

std::string to_ascii_fallback(std::string_view s, char replacement) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(replacement);
        }
    }
    return out;
}

} // namespace consoleu
