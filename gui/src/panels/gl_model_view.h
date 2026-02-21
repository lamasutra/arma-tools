#pragma once

#include <gtkmm.h>
#include <armatools/p3d.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class GLModelView : public Gtk::GLArea {
public:
    GLModelView();
    ~GLModelView() override;

    void set_lod(const armatools::p3d::LOD& lod);
    void set_texture(const std::string& key, int width, int height,
                     const uint8_t* rgba_data);
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

    // 5c. Named selection highlighting (preparation)
    void set_highlight_faces(const std::vector<uint32_t>& face_indices);

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
    int loc_has_texture_ = -1;
    int loc_light_dir_ = -1;
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
    std::unordered_map<std::string, bool> texture_has_alpha_;

    // Camera state
    float azimuth_ = 0.4f;
    float elevation_ = 0.3f;
    float distance_ = 5.0f;
    float pivot_[3] = {0, 0, 0};

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

    // 5c. Named selection highlighting
    std::vector<uint32_t> highlighted_faces_;

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
    Glib::RefPtr<Gtk::GestureDrag> drag_pan_;
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_zoom_;

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
};
