#pragma once

#include <gtkmm.h>
#include <armatools/p3d.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class GLModelView : public Gtk::GLArea {
public:
    struct MaterialParams {
        float ambient[3]{0.18f, 0.18f, 0.18f};
        float diffuse[3]{1.0f, 1.0f, 1.0f};
        float emissive[3]{0.0f, 0.0f, 0.0f};
        float specular[3]{0.08f, 0.08f, 0.08f};
        float specular_power = 32.0f;
        int shader_mode = 0; // 0=default, 1=normal/spec, 2=emissive, 3=alpha-test
    };

    GLModelView();
    ~GLModelView() override;

    void set_lod(const armatools::p3d::LOD& lod);
    void set_lods(const std::vector<armatools::p3d::LOD>& lods);
    void set_texture(const std::string& key, int width, int height,
                     const uint8_t* rgba_data);
    void set_normal_map(const std::string& key, int width, int height,
                        const uint8_t* rgba_data);
    void set_specular_map(const std::string& key, int width, int height,
                          const uint8_t* rgba_data);
    void set_material_params(const std::string& key, const MaterialParams& params);
    void reset_camera();
    void set_camera_from_bounds(float cx, float cy, float cz, float radius);
    void set_wireframe(bool on);
    void set_textured(bool on);

    // Capture current framebuffer as a pixbuf
    Glib::RefPtr<Gdk::Pixbuf> snapshot() const;

    // 5a. Grid/axis display
    void set_show_grid(bool on);

    // 5b. Background color
    void set_background_color(float r, float g, float b);

    enum class CameraMode { Orbit, FirstPerson };
    void set_camera_mode(CameraMode mode);
    CameraMode camera_mode() const;

    enum class HighlightMode { Points, Lines };
    // Named selection highlight geometry.
    void set_highlight_geometry(const std::vector<float>& positions, HighlightMode mode);

    // Camera state access (for synchronized views)
    struct CameraState {
        float azimuth;
        float elevation;
        float distance;
        float pivot[3];
    };
    CameraState get_camera_state() const;
    void set_camera_state(const CameraState& state);

    // Emitted after any camera manipulation (orbit, pan, zoom)
    sigc::signal<void()>& signal_camera_changed();

private:
    sigc::signal<void()> signal_camera_changed_;
    bool suppress_camera_signal_ = false;
    // GL resources
    uint32_t prog_solid_ = 0;
    uint32_t prog_wire_ = 0;
    int loc_mvp_solid_ = -1;
    int loc_normal_mat_ = -1;
    int loc_texture_ = -1;
    int loc_normal_map_ = -1;
    int loc_specular_map_ = -1;
    int loc_has_texture_ = -1;
    int loc_has_normal_map_ = -1;
    int loc_has_specular_map_ = -1;
    int loc_light_dir_ = -1;
    int loc_has_material_ = -1;
    int loc_mat_ambient_ = -1;
    int loc_mat_diffuse_ = -1;
    int loc_mat_emissive_ = -1;
    int loc_mat_specular_ = -1;
    int loc_mat_spec_power_ = -1;
    int loc_shader_mode_ = -1;
    int loc_mvp_wire_ = -1;
    int loc_color_wire_ = -1;

    struct MeshGroup {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        int vertex_count = 0;
        std::string texture_key;
    };
    std::vector<MeshGroup> groups_;
    std::unordered_map<std::string, uint32_t> textures_;
    std::unordered_map<std::string, uint32_t> normal_maps_;
    std::unordered_map<std::string, uint32_t> specular_maps_;
    std::unordered_map<std::string, bool> texture_has_alpha_;
    std::unordered_map<std::string, MaterialParams> material_params_;
    bool debug_group_report_pending_ = false;

    // Camera state
    float azimuth_ = 0.4f;
    float elevation_ = 0.3f;
    float distance_ = 5.0f;
    float pivot_[3] = {0, 0, 0};
    CameraMode camera_mode_ = CameraMode::Orbit;
    float default_center_[3] = {0, 0, 0};
    bool has_default_center_ = false;
    float default_azimuth_ = 0.4f;
    float default_elevation_ = 0.3f;
    float default_distance_ = 5.0f;
    bool has_default_camera_ = false;

    // Rendering mode
    bool wireframe_ = false;
    bool textured_ = true;
    bool has_geometry_ = false;
    bool is_desktop_gl_ = true;

    // 5a. Grid/axis display
    bool show_grid_ = true;
    uint32_t grid_vao_ = 0;
    uint32_t grid_vbo_ = 0;
    int grid_line_count_ = 0;
    uint32_t axis_vao_ = 0;
    uint32_t axis_vbo_ = 0;

    // 5b. Background color
    float bg_color_[3] = {0.2f, 0.2f, 0.2f};

    // Named selection highlighting
    std::vector<float> highlight_geometry_;
    HighlightMode highlight_mode_ = HighlightMode::Points;
    uint32_t highlight_vao_ = 0;
    uint32_t highlight_vbo_ = 0;
    int highlight_vertex_count_ = 0;

    // Wireframe line buffer (for GLES path)
    uint32_t wire_vao_ = 0;
    uint32_t wire_vbo_ = 0;
    uint32_t wire_ebo_ = 0;
    int wire_index_count_ = 0;

    // Drag state
    double drag_start_x_ = 0;
    double drag_start_y_ = 0;
    float drag_start_azimuth_ = 0;
    float drag_start_elevation_ = 0;
    float drag_start_pivot_[3] = {0, 0, 0};

    // Gesture controllers
    Glib::RefPtr<Gtk::GestureDrag> drag_orbit_;
    Glib::RefPtr<Gtk::GestureDrag> drag_look_;
    Glib::RefPtr<Gtk::GestureDrag> drag_pan_;
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_zoom_;
    Glib::RefPtr<Gtk::GestureClick> click_focus_;
    Glib::RefPtr<Gtk::EventControllerKey> key_move_;
    sigc::connection move_tick_conn_;
    bool move_fwd_ = false;
    bool move_back_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;
    bool move_fast_ = false;

    // GL callbacks
    void on_realize_gl();
    void on_unrealize_gl();
    bool on_render_gl(const Glib::RefPtr<Gdk::GLContext>& context);

    // Helpers
    uint32_t compile_shader(uint32_t type, const char* source);
    uint32_t link_program(uint32_t vert, uint32_t frag);
    void cleanup_gl();
    void build_matrices(float* mvp, float* normal_mat);
    void build_grid_and_axis();
    void draw_grid_and_axis(const float* mvp);
    void rebuild_highlight_vertex_buffer();
    void move_camera_local(float forward, float right, float up);
    bool movement_tick();
};
