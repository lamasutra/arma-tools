#include "load_resource_text.h"

#include <gio/gio.h>

#include <string>

namespace infra::gl {

std::string load_resource_text(std::string_view resource_path) {
    GError* error = nullptr;
    GBytes* bytes = g_resources_lookup_data(
        std::string(resource_path).c_str(),
        G_RESOURCE_LOOKUP_FLAGS_NONE,
        &error);
    if (!bytes) {
        if (error) g_error_free(error);
        return {};
    }

    gsize size = 0;
    const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    std::string text;
    if (data && size > 0) {
        text.assign(data, size);
    }
    g_bytes_unref(bytes);
    return text;
}

}  // namespace infra::gl
