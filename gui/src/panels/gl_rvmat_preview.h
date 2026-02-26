#pragma once

#include "app/rvmat_preview_camera_controller.h"
#include "domain/rvmat_preview_camera_types.h"

#include <gtkmm.h>

#include <array>
#include <cstdint>
#include <vector>

class GLRvmatPreview : public Gtk::GLArea {
public:
    struct MaterialParams {
        float ambient[3]{0.18f, 0.18f, 0.18f};
        float diffuse[3]{1.0f, 1.0f, 1.0f};
        float emissive[3]{0.0f, 0.0f, 0.0f};
        float specular[3]{0.08f, 0.08f, 0.08f};
        float specular_power = 32.0f;
    };

    GLRvmatPreview();
    ~GLRvmatPreview() override;

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
    enum class UVSource { Tex0 = 0, Tex1 = 1 };
    void set_diffuse_uv_source(UVSource source);
    void set_normal_uv_source(UVSource source);
    void set_specular_uv_source(UVSource source);
    void set_ao_uv_source(UVSource source);
    enum class Shape { Sphere, Tile };
    void set_shape(Shape shape);
    enum class ViewMode { Final = 0, Albedo = 1, Normal = 2, Specular = 3, AO = 4 };
    void set_view_mode(ViewMode mode);

private:
    struct Vertex {
        float p[3];
        float n[3];
        float uv[2];
        float uv1[2];
        float t[3];
    };

    uint32_t prog_ = 0;
    int loc_mvp_ = -1;
    int loc_model_ = -1;
    int loc_normal_mat_ = -1;
    int loc_light_dir_ = -1;
    int loc_cam_pos_ = -1;
    int loc_tex_diff_ = -1;
    int loc_tex_nrm_ = -1;
    int loc_tex_spec_ = -1;
    int loc_tex_ao_ = -1;
    int loc_has_diff_ = -1;
    int loc_has_nrm_ = -1;
    int loc_has_spec_ = -1;
    int loc_has_ao_ = -1;
    int loc_mat_ambient_ = -1;
    int loc_mat_diffuse_ = -1;
    int loc_mat_emissive_ = -1;
    int loc_mat_specular_ = -1;
    int loc_mat_spec_power_ = -1;
    int loc_uv_diff_ = -1;
    int loc_uv_nrm_ = -1;
    int loc_uv_spec_ = -1;
    int loc_uv_ao_ = -1;
    int loc_uv_src_diff_ = -1;
    int loc_uv_src_nrm_ = -1;
    int loc_uv_src_spec_ = -1;
    int loc_uv_src_ao_ = -1;
    int loc_view_mode_ = -1;
    int loc_diffuse_srgb_ = -1;

    uint32_t vao_sphere_ = 0;
    uint32_t vbo_sphere_ = 0;
    uint32_t ebo_sphere_ = 0;
    int index_count_sphere_ = 0;
    uint32_t vao_tile_ = 0;
    uint32_t vbo_tile_ = 0;
    uint32_t ebo_tile_ = 0;
    int index_count_tile_ = 0;

    uint32_t tex_diff_ = 0;
    uint32_t tex_nrm_ = 0;
    uint32_t tex_spec_ = 0;
    uint32_t tex_ao_ = 0;
    bool has_diff_ = false;
    bool has_nrm_ = false;
    bool has_spec_ = false;
    bool has_ao_ = false;
    Shape shape_ = Shape::Sphere;
    MaterialParams mat_;
    ViewMode view_mode_ = ViewMode::Final;
    bool diffuse_is_srgb_ = true;
    std::array<float, 9> uv_diff_{1.0f, 0.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f};
    std::array<float, 9> uv_nrm_{1.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 1.0f};
    std::array<float, 9> uv_spec_{1.0f, 0.0f, 0.0f,
                                 0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f};
    std::array<float, 9> uv_ao_{1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f};
    int uv_src_diff_ = static_cast<int>(UVSource::Tex0);
    int uv_src_nrm_ = static_cast<int>(UVSource::Tex0);
    int uv_src_spec_ = static_cast<int>(UVSource::Tex0);
    int uv_src_ao_ = static_cast<int>(UVSource::Tex0);

    RvmatPreviewCameraController camera_controller_;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    float drag_start_azimuth_ = 0.0f;
    float drag_start_elevation_ = 0.0f;
    float drag_start_pivot_[3] = {0.0f, 0.0f, 0.0f};
    Glib::RefPtr<Gtk::GestureDrag> drag_orbit_;
    Glib::RefPtr<Gtk::GestureDrag> drag_pan_;
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_zoom_;

    void on_realize_gl();
    void on_unrealize_gl();
    bool on_render_gl(const Glib::RefPtr<Gdk::GLContext>&);
    void cleanup_gl();
    uint32_t compile_shader(uint32_t type, const char* src);
    uint32_t link_program(uint32_t vs, uint32_t fs);
    void upload_texture(uint32_t& tex, int width, int height, const uint8_t* rgba_data);
    void build_sphere_mesh();
    void build_tile_mesh();
};
