#include "ui_domain/ui_cli_override.h"

#include <cctype>

namespace ui_domain {

namespace {

std::string normalize_backend_name(std::string backend) {
    for (char& ch : backend) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return backend;
}

}  // namespace

CliOverrideParseResult parse_ui_override_and_strip_args(int* argc, char** argv) {
    CliOverrideParseResult result;
    if (!argc || !argv || *argc <= 0) {
        return result;
    }

    int write_index = 1;
    for (int read_index = 1; read_index < *argc; ++read_index) {
        const std::string arg = argv[read_index] ? argv[read_index] : "";
        if (arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v") {
            argv[write_index++] = argv[read_index];
            continue;
        }

        if (arg.rfind("--ui=", 0) == 0) {
            const std::string value = normalize_backend_name(arg.substr(5));
            if (value.empty()) {
                result.warnings.emplace_back("Ignoring empty --ui override");
            } else {
                result.has_ui_override = true;
                result.ui_backend = value;
            }
            continue;
        }

        if (arg == "--ui") {
            if (read_index + 1 >= *argc) {
                result.warnings.emplace_back("Missing value for --ui option");
                continue;
            }
            const std::string value = normalize_backend_name(
                argv[read_index + 1] ? argv[read_index + 1] : "");
            if (value.empty()) {
                result.warnings.emplace_back("Ignoring empty --ui override");
            } else {
                result.has_ui_override = true;
                result.ui_backend = value;
            }
            ++read_index;
            continue;
        }

        argv[write_index++] = argv[read_index];
    }

    *argc = write_index;
    argv[write_index] = nullptr;
    return result;
}

}  // namespace ui_domain
