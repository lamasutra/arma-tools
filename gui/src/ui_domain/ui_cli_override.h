#pragma once

#include <string>
#include <vector>

namespace ui_domain {

struct CliOverrideParseResult {
    bool has_ui_override = false;
    std::string ui_backend;
    std::vector<std::string> warnings;
};

CliOverrideParseResult parse_ui_override_and_strip_args(int* argc, char** argv);

}  // namespace ui_domain
