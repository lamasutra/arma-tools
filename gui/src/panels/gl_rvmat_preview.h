#pragma once

#include <gtkmm.h>

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

private:
    struct Vertex {
        float p[3];
        float n[3];
        float uv[2];
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
    int loc_has_diff_ = -1;
    int loc_has_nrm_ = -1;
    int loc_has_spec_ = -1;
    int loc_mat_ambient_ = -1;
    int loc_mat_diffuse_ = -1;
    int loc_mat_emissive_ = -1;
    int loc_mat_specular_ = -1;
    int loc_mat_spec_power_ = -1;

    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t ebo_ = 0;
    int index_count_ = 0;

    uint32_t tex_diff_ = 0;
    uint32_t tex_nrm_ = 0;
    uint32_t tex_spec_ = 0;
    bool has_diff_ = false;
    bool has_nrm_ = false;
    bool has_spec_ = false;
    MaterialParams mat_;

    void on_realize_gl();
    void on_unrealize_gl();
    bool on_render_gl(const Glib::RefPtr<Gdk::GLContext>&);
    void cleanup_gl();
    uint32_t compile_shader(uint32_t type, const char* src);
    uint32_t link_program(uint32_t vs, uint32_t fs);
    void upload_texture(uint32_t& tex, int width, int height, const uint8_t* rgba_data);
    void build_sphere_mesh();
};
