#pragma once

#include <string>
#include <string_view>

namespace infra::gl {

[[nodiscard]] std::string load_resource_text(std::string_view resource_path);

}  // namespace infra::gl
