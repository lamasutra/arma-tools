#pragma once

#include <armatools/paa.h>

#include <optional>
#include <string>

namespace procedural_texture {

// Generates a preview image for common RV procedural texture expressions.
// Returns std::nullopt when expression is not procedural or unsupported.
std::optional<armatools::paa::Image> generate(const std::string& expression,
                                              const std::string& semantic_hint = {});

} // namespace procedural_texture

