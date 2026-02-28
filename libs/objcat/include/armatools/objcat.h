#pragma once

#include <string>

namespace armatools::objcat {

enum class ObjectType {
    Unknown,
    Road
};

// get_object_type classifies a P3D model path into a specific object type.
ObjectType get_object_type(const std::string& model_path);
const char* to_string(ObjectType type);

// category classifies a P3D model path into an object category.
// Modern paths get a category derived from their directory structure.
// OFP-style bare filenames fall back to prefix-based guessing.
std::string category(const std::string& model_path);

} // namespace armatools::objcat
