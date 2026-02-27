#pragma once

#include <string>
#include <vector>

namespace render_domain {

struct CliOverrideParseResult {
    bool has_renderer_override = false;
    std::string renderer_backend;
    std::vector<std::string> warnings;
};

CliOverrideParseResult parse_renderer_override_and_strip_args(int* argc, char** argv);

}  // namespace render_domain
