#include "render_domain/rvmat_preview_widget.h"

#include "render_domain/backend_gles/gl_rvmat_preview.h"
#include "render_domain/rd_backend_kind.h"

namespace render_domain {

struct RvmatPreviewWidget::Impl {
    GLRvmatPreview gles;
};

namespace {

GLRvmatPreview::Shape to_gles_shape(RvmatPreviewWidget::Shape shape) {
    return shape == RvmatPreviewWidget::Shape::Tile
        ? GLRvmatPreview::Shape::Tile
        : GLRvmatPreview::Shape::Sphere;
}

GLRvmatPreview::ViewMode to_gles_view_mode(RvmatPreviewWidget::ViewMode mode) {
    switch (mode) {
    case RvmatPreviewWidget::ViewMode::Albedo:
        return GLRvmatPreview::ViewMode::Albedo;
    case RvmatPreviewWidget::ViewMode::Normal:
        return GLRvmatPreview::ViewMode::Normal;
    case RvmatPreviewWidget::ViewMode::Specular:
        return GLRvmatPreview::ViewMode::Specular;
    case RvmatPreviewWidget::ViewMode::AO:
        return GLRvmatPreview::ViewMode::AO;
    case RvmatPreviewWidget::ViewMode::Final:
    default:
        return GLRvmatPreview::ViewMode::Final;
    }
}

GLRvmatPreview::UVSource to_gles_uv_source(RvmatPreviewWidget::UVSource source) {
    return source == RvmatPreviewWidget::UVSource::Tex1
        ? GLRvmatPreview::UVSource::Tex1
        : GLRvmatPreview::UVSource::Tex0;
}

GLRvmatPreview::MaterialParams to_gles_material(const RvmatPreviewWidget::MaterialParams& in) {
    GLRvmatPreview::MaterialParams out;
    out.ambient[0] = in.ambient[0];
    out.ambient[1] = in.ambient[1];
    out.ambient[2] = in.ambient[2];
    out.diffuse[0] = in.diffuse[0];
    out.diffuse[1] = in.diffuse[1];
    out.diffuse[2] = in.diffuse[2];
    out.emissive[0] = in.emissive[0];
    out.emissive[1] = in.emissive[1];
    out.emissive[2] = in.emissive[2];
    out.specular[0] = in.specular[0];
    out.specular[1] = in.specular[1];
    out.specular[2] = in.specular[2];
    out.specular_power = in.specular_power;
    return out;
}

}  // namespace

RvmatPreviewWidget::RvmatPreviewWidget()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      fallback_label_("Renderer backend does not provide rvmat preview") {
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
        fallback_label_.set_text("RVMAT preview disabled (null renderer backend)");
    } else {
        fallback_label_.set_text("RVMAT preview unavailable for backend: " + backend_id);
    }
    fallback_box_.append(fallback_label_);
    append(fallback_box_);
}

RvmatPreviewWidget::~RvmatPreviewWidget() = default;

bool RvmatPreviewWidget::has_gles() const {
    return impl_ != nullptr;
}

void RvmatPreviewWidget::clear_material() {
    if (has_gles()) impl_->gles.clear_material();
}

void RvmatPreviewWidget::set_material_params(const MaterialParams& params) {
    if (has_gles()) impl_->gles.set_material_params(to_gles_material(params));
}

void RvmatPreviewWidget::set_diffuse_texture(int width, int height, const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_diffuse_texture(width, height, rgba_data);
}

void RvmatPreviewWidget::set_normal_texture(int width, int height, const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_normal_texture(width, height, rgba_data);
}

void RvmatPreviewWidget::set_specular_texture(int width, int height, const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_specular_texture(width, height, rgba_data);
}

void RvmatPreviewWidget::set_ao_texture(int width, int height, const uint8_t* rgba_data) {
    if (has_gles()) impl_->gles.set_ao_texture(width, height, rgba_data);
}

void RvmatPreviewWidget::set_diffuse_uv_matrix(const std::array<float, 9>& m) {
    if (has_gles()) impl_->gles.set_diffuse_uv_matrix(m);
}

void RvmatPreviewWidget::set_normal_uv_matrix(const std::array<float, 9>& m) {
    if (has_gles()) impl_->gles.set_normal_uv_matrix(m);
}

void RvmatPreviewWidget::set_specular_uv_matrix(const std::array<float, 9>& m) {
    if (has_gles()) impl_->gles.set_specular_uv_matrix(m);
}

void RvmatPreviewWidget::set_ao_uv_matrix(const std::array<float, 9>& m) {
    if (has_gles()) impl_->gles.set_ao_uv_matrix(m);
}

void RvmatPreviewWidget::set_diffuse_uv_source(UVSource source) {
    if (has_gles()) impl_->gles.set_diffuse_uv_source(to_gles_uv_source(source));
}

void RvmatPreviewWidget::set_normal_uv_source(UVSource source) {
    if (has_gles()) impl_->gles.set_normal_uv_source(to_gles_uv_source(source));
}

void RvmatPreviewWidget::set_specular_uv_source(UVSource source) {
    if (has_gles()) impl_->gles.set_specular_uv_source(to_gles_uv_source(source));
}

void RvmatPreviewWidget::set_ao_uv_source(UVSource source) {
    if (has_gles()) impl_->gles.set_ao_uv_source(to_gles_uv_source(source));
}

void RvmatPreviewWidget::set_shape(Shape shape) {
    if (has_gles()) impl_->gles.set_shape(to_gles_shape(shape));
}

void RvmatPreviewWidget::set_view_mode(ViewMode mode) {
    if (has_gles()) impl_->gles.set_view_mode(to_gles_view_mode(mode));
}

}  // namespace render_domain
