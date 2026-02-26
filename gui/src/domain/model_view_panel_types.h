#pragma once

#include <string>
#include <vector>

namespace modelview {

enum class HighlightMode {
    Points,
    Lines,
};

struct HighlightGeometry {
    HighlightMode mode = HighlightMode::Points;
    std::vector<float> positions;
    std::string debug_message;
};

struct NamedSelectionItem {
    std::string name;
    std::string label;
};

}  // namespace modelview
