#pragma once

#include <gtkmm.h>

#include <array>
#include <cstdint>
#include <memory>

namespace render_domain {

// RvmatPreviewWidget is the GTK wrapper for rendering Arma 3 surface materials.
//
// Unlike the ModelViewWidget (which renders complex P3D geometry), this widget
// is specialized for rendering single materials (.rvmat files) either onto a
// 3D Sphere or a flat 2D Tile.
//
// It supports multiple viewing modes (Final, Albedo, Normal, Specular, AO)
// which are extremely useful for debugging PBR (Physically Based Rendering) assets.
class RvmatPreviewWidget : public Gtk::Box {
public:
    struct MaterialParams {
        float ambient[3]{0.18f, 0.18f, 0.18f};
        float diffuse[3]{1.0f, 1.0f, 1.0f};
        float emissive[3]{0.0f, 0.0f, 0.0f};
        float specular[3]{0.08f, 0.08f, 0.08f};
        float specular_power = 32.0f;
    };

    enum class UVSource { Tex0 = 0, Tex1 = 1 };
    enum class Shape { Sphere, Tile };
    enum class ViewMode { Final = 0, Albedo = 1, Normal = 2, Specular = 3, AO = 4 };

    RvmatPreviewWidget();
    ~RvmatPreviewWidget() override;

    void clear_material();
    void set_material_params(const MaterialParams& params);
    void set_diffuse_texture(int width, int height, const uint8_t* rgba_data);
    void set_normal_texture(int width, int height, const uint8_t* rgba_data);
    void set_specular_texture(int width, int height, const uint8_t* rgba_data);
    void set_ao_texture(int width, int height, const uint8_t* rgba_data);
    void set_diffuse_uv_matrix(const std::array<float, 9>& m);
    void set_normal_uv_matrix(const std::array<float, 9>& m);
    void set_specular_uv_matrix(const std::array<float, 9>& m);
    void set_ao_uv_matrix(const std::array<float, 9>& m);
    void set_diffuse_uv_source(UVSource source);
    void set_normal_uv_source(UVSource source);
    void set_specular_uv_source(UVSource source);
    void set_ao_uv_source(UVSource source);
    void set_shape(Shape shape);
    void set_view_mode(ViewMode mode);

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    Gtk::Box fallback_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label fallback_label_;

    bool has_gles() const;
};

}  // namespace render_domain
