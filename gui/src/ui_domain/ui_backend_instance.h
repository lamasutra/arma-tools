#pragma once

#include "ui_domain/ui_backend_abi.h"

#include <string>

namespace ui_domain {

class BackendInstance {
public:
    BackendInstance() = default;
    ~BackendInstance();

    BackendInstance(const BackendInstance&) = delete;
    BackendInstance& operator=(const BackendInstance&) = delete;

    BackendInstance(BackendInstance&& other) noexcept;
    BackendInstance& operator=(BackendInstance&& other) noexcept;

    static BackendInstance from_raw(std::string backend_id,
                                    ui_backend_instance_v1 raw_instance);

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const std::string& backend_id() const { return backend_id_; }

    int resize(uint32_t width, uint32_t height);
    int handle_event(const ui_event_v1* event);
    int begin_frame(double delta_seconds);
    int draw();
    int end_frame();
    int set_overlay_enabled(bool enabled);
    bool overlay_enabled() const;

private:
    std::string backend_id_;
    ui_backend_instance_v1 instance_{};
    bool valid_ = false;

    void reset();
};

}  // namespace ui_domain
