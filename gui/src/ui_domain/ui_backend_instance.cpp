#include "ui_domain/ui_backend_instance.h"

#include <utility>

namespace ui_domain {

BackendInstance::~BackendInstance() {
    reset();
}

BackendInstance::BackendInstance(BackendInstance&& other) noexcept {
    backend_id_ = std::move(other.backend_id_);
    instance_ = other.instance_;
    valid_ = other.valid_;
    other.instance_ = {};
    other.valid_ = false;
}

BackendInstance& BackendInstance::operator=(BackendInstance&& other) noexcept {
    if (this == &other) return *this;
    reset();
    backend_id_ = std::move(other.backend_id_);
    instance_ = other.instance_;
    valid_ = other.valid_;
    other.instance_ = {};
    other.valid_ = false;
    return *this;
}

BackendInstance BackendInstance::from_raw(std::string backend_id,
                                          ui_backend_instance_v1 raw_instance) {
    BackendInstance instance;
    instance.backend_id_ = std::move(backend_id);
    instance.instance_ = raw_instance;
    instance.valid_ = true;
    return instance;
}

int BackendInstance::resize(uint32_t width, uint32_t height) {
    if (!valid_ || !instance_.resize) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.resize(instance_.userdata, width, height);
}

int BackendInstance::handle_event(const ui_event_v1* event) {
    if (!valid_ || !instance_.handle_event) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.handle_event(instance_.userdata, event);
}

int BackendInstance::begin_frame(double delta_seconds) {
    if (!valid_ || !instance_.begin_frame) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.begin_frame(instance_.userdata, delta_seconds);
}

int BackendInstance::draw() {
    if (!valid_ || !instance_.draw) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.draw(instance_.userdata);
}

int BackendInstance::end_frame() {
    if (!valid_ || !instance_.end_frame) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.end_frame(instance_.userdata);
}

int BackendInstance::set_overlay_enabled(bool enabled) {
    if (!valid_ || !instance_.set_overlay_enabled) return UI_STATUS_NOT_IMPLEMENTED;
    return instance_.set_overlay_enabled(instance_.userdata, enabled ? 1 : 0);
}

bool BackendInstance::overlay_enabled() const {
    if (!valid_ || !instance_.get_overlay_enabled) return false;
    return instance_.get_overlay_enabled(instance_.userdata) != 0;
}

void BackendInstance::reset() {
    if (valid_ && instance_.destroy) {
        instance_.destroy(instance_.userdata);
    }
    instance_ = {};
    valid_ = false;
}

}  // namespace ui_domain
