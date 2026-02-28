#include "render_domain/wrp_terrain_widget.h"

#include "render_domain/backend_gles/gl_wrp_terrain_view.h"
#include "render_domain/rd_backend_kind.h"

#include <utility>

namespace render_domain {

struct WrpTerrainWidget::Impl {
    GLWrpTerrainView gles;
};

WrpTerrainWidget::WrpTerrainWidget()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      fallback_label_("Renderer backend does not provide terrain 3D view") {
    const auto kind = active_backend_kind();
    if (kind == BackendKind::Gles) {
        impl_ = std::make_unique<Impl>();
        impl_->gles.set_hexpand(true);
        impl_->gles.set_vexpand(true);
        append(impl_->gles);
        return;
    }

    fallback_box_.set_hexpand(true);
    fallback_box_.set_vexpand(true);
    fallback_box_.set_halign(Gtk::Align::CENTER);
    fallback_box_.set_valign(Gtk::Align::CENTER);
    fallback_label_.set_wrap(true);
    fallback_label_.set_justify(Gtk::Justification::CENTER);

    const auto backend_id = active_backend_id();
    if (backend_id == "null") {
        fallback_label_.set_text("Terrain 3D view disabled (null renderer backend)");
    } else {
        fallback_label_.set_text("Terrain 3D view unavailable for backend: " + backend_id);
    }

    fallback_box_.append(fallback_label_);
    append(fallback_box_);
}

WrpTerrainWidget::~WrpTerrainWidget() = default;

bool WrpTerrainWidget::has_gles() const {
    return impl_ != nullptr;
}

void WrpTerrainWidget::emit_fallback_status() {
    if (has_gles()) return;
    if (on_terrain_stats_) {
        on_terrain_stats_("Renderer backend does not support terrain 3D rendering");
    }
}

void WrpTerrainWidget::clear_world() {
    if (has_gles()) {
        impl_->gles.clear_world();
    } else {
        emit_fallback_status();
    }
}

void WrpTerrainWidget::set_world_data(const armatools::wrp::WorldData& world) {
    if (has_gles()) impl_->gles.set_world_data(world);
}

void WrpTerrainWidget::set_objects(std::vector<armatools::wrp::ObjectRecord> objects) {
    if (has_gles()) impl_->gles.set_objects(std::move(objects));
}

void WrpTerrainWidget::set_wireframe(bool on) {
    if (has_gles()) impl_->gles.set_wireframe(on);
}

void WrpTerrainWidget::set_show_objects(bool on) {
    if (has_gles()) impl_->gles.set_show_objects(on);
}

void WrpTerrainWidget::set_object_max_distance(float distance_m) {
    if (has_gles()) impl_->gles.set_object_max_distance(distance_m);
}

void WrpTerrainWidget::set_object_category_filters(bool buildings,
                                                    bool vegetation,
                                                    bool rocks,
                                                    bool props) {
    if (has_gles()) impl_->gles.set_object_category_filters(buildings, vegetation, rocks, props);
}

void WrpTerrainWidget::set_show_object_bounds(bool on) {
    if (has_gles()) impl_->gles.set_show_object_bounds(on);
}

void WrpTerrainWidget::set_show_water(bool on) {
    if (has_gles()) impl_->gles.set_show_water(on);
}

void WrpTerrainWidget::set_water_level(float level) {
    if (has_gles()) impl_->gles.set_water_level(level);
}

void WrpTerrainWidget::set_color_mode(int mode) {
    if (has_gles()) impl_->gles.set_color_mode(mode);
}

void WrpTerrainWidget::set_satellite_palette(const std::vector<std::array<float, 3>>& palette) {
    if (has_gles()) impl_->gles.set_satellite_palette(palette);
}

void WrpTerrainWidget::set_on_object_picked(std::function<void(size_t)> cb) {
    on_object_picked_ = std::move(cb);
    if (has_gles()) impl_->gles.set_on_object_picked(on_object_picked_);
}

void WrpTerrainWidget::set_on_texture_debug_info(std::function<void(const std::string&)> cb) {
    on_texture_debug_info_ = std::move(cb);
    if (has_gles()) impl_->gles.set_on_texture_debug_info(on_texture_debug_info_);
}

void WrpTerrainWidget::set_on_terrain_stats(std::function<void(const std::string&)> cb) {
    on_terrain_stats_ = std::move(cb);
    if (has_gles()) {
        impl_->gles.set_on_terrain_stats(on_terrain_stats_);
    } else {
        emit_fallback_status();
    }
}

void WrpTerrainWidget::set_on_compass_info(std::function<void(const std::string&)> cb) {
    on_compass_info_ = std::move(cb);
    if (has_gles()) impl_->gles.set_on_compass_info(on_compass_info_);
}

void WrpTerrainWidget::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    if (has_gles()) impl_->gles.set_model_loader_service(service);
}

void WrpTerrainWidget::set_texture_loader_service(
    const std::shared_ptr<TexturesLoaderService>& service) {
    if (has_gles()) impl_->gles.set_texture_loader_service(service);
}

void WrpTerrainWidget::set_show_patch_boundaries(bool on) {
    if (has_gles()) impl_->gles.set_show_patch_boundaries(on);
}

void WrpTerrainWidget::set_show_patch_lod_colors(bool on) {
    if (has_gles()) impl_->gles.set_show_patch_lod_colors(on);
}

void WrpTerrainWidget::set_show_tile_boundaries(bool on) {
    if (has_gles()) impl_->gles.set_show_tile_boundaries(on);
}

void WrpTerrainWidget::set_terrain_far_distance(float distance_m) {
    if (has_gles()) impl_->gles.set_terrain_far_distance(distance_m);
}

void WrpTerrainWidget::set_material_quality_distances(float mid_distance_m, float far_distance_m) {
    if (has_gles()) impl_->gles.set_material_quality_distances(mid_distance_m, far_distance_m);
}

void WrpTerrainWidget::set_seam_debug_mode(int mode) {
    if (has_gles()) impl_->gles.set_seam_debug_mode(mode);
}

void WrpTerrainWidget::set_camera_mode(wrpterrain::CameraMode mode) {
    if (has_gles()) impl_->gles.set_camera_mode(mode);
}

wrpterrain::CameraMode WrpTerrainWidget::camera_mode() const {
    if (has_gles()) return impl_->gles.camera_mode();
    return wrpterrain::CameraMode::Orbit;
}

}  // namespace render_domain
