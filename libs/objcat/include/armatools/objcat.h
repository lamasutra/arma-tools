#pragma once

#include <string>

namespace armatools::objcat {

// category classifies a P3D model path into an object category.
// Modern paths get a category derived from their directory structure.
// OFP-style bare filenames fall back to prefix-based guessing.
std::string category(const std::string& model_path);

} // namespace armatools::objcat
