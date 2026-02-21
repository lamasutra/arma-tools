#pragma once

// Thin C++ helpers for libpanel's C/GObject API.
// Not full bindings — just RAII for objects we create/destroy manually.

#include <libpanel.h>
#include <utility>

namespace panel {

// RAII guard for GObject pointers we own (unrefs on destruction).
// Does NOT ref on construction — takes ownership of a new/floating ref.
template <typename T>
class OwnedRef {
public:
    OwnedRef() = default;
    explicit OwnedRef(T* ptr) : ptr_(ptr) {}
    ~OwnedRef() { if (ptr_) g_object_unref(ptr_); }

    OwnedRef(const OwnedRef&) = delete;
    OwnedRef& operator=(const OwnedRef&) = delete;
    OwnedRef(OwnedRef&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    OwnedRef& operator=(OwnedRef&& o) noexcept {
        if (this != &o) { if (ptr_) g_object_unref(ptr_); ptr_ = std::exchange(o.ptr_, nullptr); }
        return *this;
    }

    T* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    T* release() { return std::exchange(ptr_, nullptr); }

private:
    T* ptr_ = nullptr;
};

// Convenience: create a PanelPosition, set area, return owned ref.
inline OwnedRef<PanelPosition> make_position(PanelArea area) {
    auto* pos = panel_position_new();
    panel_position_set_area(pos, area);
    return OwnedRef<PanelPosition>(pos);
}

inline OwnedRef<PanelPosition> make_position(PanelArea area, guint column, guint row, guint depth) {
    auto* pos = panel_position_new();
    panel_position_set_area(pos, area);
    panel_position_set_column(pos, column);
    panel_position_set_row(pos, row);
    panel_position_set_depth(pos, depth);
    return OwnedRef<PanelPosition>(pos);
}

} // namespace panel
