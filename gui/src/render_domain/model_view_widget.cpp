#include "render_domain/model_view_widget.h"
#include "cli_logger.h"

#include "log_panel.h"
#include "cli_logger.h"
#include "render_domain/backend_gles/gl_model_view.h"
#include "cli_logger.h"
#include "render_domain/rd_backend_kind.h"
#include "cli_logger.h"
#include "render_domain/rd_scene_blob_builder.h"
#include "cli_logger.h"

#include <utility>
#include "cli_logger.h"

namespace render_domain {

struct ModelViewWidget::Impl {
    GLModelView gles;
};

namespace {

GLModelView::HighlightMode to_gles_highlight_mode(ModelViewWidget::HighlightMode mode) {
    return mode == ModelViewWidget::HighlightMode::Lines
        ? GLModelView::HighlightMode::Lines
        : GLModelView::HighlightMode::Points;
}

GLModelView::MaterialParams to_gles_material_params(const ModelViewWidget::MaterialParams& params) {
    GLModelView::MaterialParams out;
    out.ambient[0] = params.ambient[0];
    out.ambient[1] = params.ambient[1];
    out.ambient[2] = params.ambient[2];
    out.diffuse[0] = params.diffuse[0];
    out.diffuse[1] = params.diffuse[1];
    out.diffuse[2] = params.diffuse[2];
    out.emissive[0] = params.emissive[0];
    out.emissive[1] = params.emissive[1];
    out.emissive[2] = params.emissive[2];
    out.specular[0] = params.specular[0];
    out.specular[1] = params.specular[1];
    out.specular[2] = params.specular[2];
    out.specular_power = params.specular_power;
    out.shader_mode = params.shader_mode;
    return out;
}

rd_scene_blob_v1 make_empty_scene_blob() {
    rd_scene_blob_v1 blob{};
    blob.struct_size = sizeof(rd_scene_blob_v1);
    blob.version = RD_SCENE_BLOB_VERSION;
    blob.flags = RD_SCENE_BLOB_FLAG_INDEX32;
    blob.positions_offset = 0;
    blob.indices_offset = 0;
    blob.meshes_offset = 0;
    blob.materials_offset = 0;
    blob.textures_offset = 0;
    return blob;
}

}  // namespace

ModelViewWidget::ModelViewWidget()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      fallback_label_("Renderer backend does not provide model view") {
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
    fallback_box_.append(fallback_label_);
    append(fallback_box_);

    const auto backend_id = active_backend_id();
    if (backend_id == "null") {
        fallback_label_.set_text("Model view disabled (null renderer backend)");
    } else {
        fallback_label_.set_text("Model view unavailable for backend: " + backend_id);
    }
}

ModelViewWidget::~ModelViewWidget() = default;

bool ModelViewWidget::has_gles() const {
    return impl_ != nullptr;
}

void ModelViewWidget::set_lod(const armatools::p3d::LOD& lod) {
    set_lods(std::vector<armatools::p3d::LOD>{lod});
}

void ModelViewWidget::set_lods(const std::vector<armatools::p3d::LOD>& lods) {
    if (!has_gles()) return;

    SceneBlobBuildOutput scene;
    std::string error;
    if (!build_scene_blob_v1_from_lods(lods, &scene, &error)) {
        LOGE("ModelViewWidget: scene blob build failed: " + error);
        impl_->gles.set_scene_blob(make_empty_scene_blob());
        return;
    }

    impl_->gles.set_scene_blob(scene.blob, scene.material_texture_keys);
}

void ModelViewWidget::set_scene_blob(const rd_scene_blob_v1& blob,
                                     const std::vector<std::string>& material_texture_keys) {
    if (has_gles()) impl_->gles.set_scene_blob(blob, material_texture_keys);
}

void ModelViewWidget::set_texture(const std::string& key,
                                  int width,
                                  int height,
                                  const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_texture(key, width, height, rgba_data);
}

void ModelViewWidget::set_normal_map(const std::string& key,
                                     int width,
                                     int height,
                                     const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_normal_map(key, width, height, rgba_data);
}

void ModelViewWidget::set_specular_map(const std::string& key,
                                       int width,
                                       int height,
                                       const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_specular_map(key, width, height, rgba_data);
}

void ModelViewWidget::set_material_params(const std::string& key,
                                          const MaterialParams& params) {
    if (has_gles()) impl_->gles.set_material_params(key, to_gles_material_params(params));
}

void ModelViewWidget::reset_camera() {
    if (has_gles()) impl_->gles.reset_camera();
}

void ModelViewWidget::set_camera_from_bounds(float cx, float cy, float cz, float radius) {
    if (has_gles()) impl_->gles.set_camera_from_bounds(cx, cy, cz, radius);
}

void ModelViewWidget::set_wireframe(bool on) {
    if (has_gles()) impl_->gles.set_wireframe(on);
}

void ModelViewWidget::set_textured(bool on) {
    if (has_gles()) impl_->gles.set_textured(on);
}

Glib::RefPtr<Gdk::Pixbuf> ModelViewWidget::snapshot() const {
    if (has_gles()) return impl_->gles.snapshot();
    return {};
}

void ModelViewWidget::set_show_grid(bool on) {
    if (has_gles()) impl_->gles.set_show_grid(on);
}

void ModelViewWidget::set_background_color(float r, float g, float b) {
    if (has_gles()) impl_->gles.set_background_color(r, g, b);
}

void ModelViewWidget::set_camera_mode(CameraMode mode) {
    if (has_gles()) impl_->gles.set_camera_mode(mode);
}

ModelViewWidget::CameraMode ModelViewWidget::camera_mode() const {
    if (has_gles()) return impl_->gles.camera_mode();
    return CameraMode::Orbit;
}

void ModelViewWidget::set_highlight_geometry(const std::vector<float>& positions,
                                             HighlightMode mode) {
    if (has_gles()) impl_->gles.set_highlight_geometry(positions, to_gles_highlight_mode(mode));
}

ModelViewWidget::CameraState ModelViewWidget::get_camera_state() const {
    if (has_gles()) return impl_->gles.get_camera_state();
    return {};
}

void ModelViewWidget::set_camera_state(const CameraState& state) {
    if (has_gles()) impl_->gles.set_camera_state(state);
}

sigc::signal<void()>& ModelViewWidget::signal_camera_changed() {
    if (has_gles()) return impl_->gles.signal_camera_changed();
    return fallback_camera_changed_;
}

bool ModelViewWidget::get_realized() const {
    if (has_gles()) return impl_->gles.get_realized();
    return Gtk::Widget::get_realized();
}

Glib::SignalProxy<void()> ModelViewWidget::signal_realize() {
    if (has_gles()) return impl_->gles.signal_realize();
    return Gtk::Widget::signal_realize();
}

}  // namespace render_domain
