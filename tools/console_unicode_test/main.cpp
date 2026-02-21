#include "console_unicode.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace consoleu;

static void print_report(const Capabilities& caps) {
    std::cout << "{\n";
    std::cout << "  \"stdout_is_tty\": " << std::boolalpha << caps.stdout_is_tty << ",\n";
    std::cout << "  \"utf8_configured\": " << caps.utf8_configured << ",\n";
    std::cout << "  \"has_native_unicode_console\": " << caps.has_native_unicode_console << ",\n";
    std::cout << "  \"likely_emoji_ok\": " << caps.likely_emoji_ok << ",\n";
    std::cout << "  \"details\": \"" << caps.details << "\"\n";
    std::cout << "}\n";
}

int main(int argc, char* argv[]) {
    bool probe = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--probe" || arg == "-p") {
            probe = true;
        }
    }

    auto caps = detect_capabilities();
    print_report(caps);

    if (probe) {
        write_stdout_utf8("emoji_probe: 😀 😁 😂 👍🏽 ❤️ 🧪 🧠 🌍\n");
        if (!caps.likely_emoji_ok) {
            write_stdout_with_fallback("fallback_probe: 😀 😁 😂 👍🏽 ❤️\n",
                                       "fallback_probe: :-)\n", EmojiMode::Auto);
        }
    }

    return caps.likely_emoji_ok ? 0 : 1;
}
