#pragma once

#include <gtkmm.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <epoxy/gl.h>

#include <armatools/wrp.h>

class LodTexturesLoaderService;

class GLWrpTerrainView : public Gtk::GLArea {
public:
    GLWrpTerrainView();
    ~GLWrpTerrainView() override;

    void clear_world();
    void set_world_data(const armatools::wrp::WorldData& world);
    void set_objects(const std::vector<armatools::wrp::ObjectRecord>& objects);
    void set_wireframe(bool on);
    void set_show_objects(bool on);
    void set_color_mode(int mode);
    void set_satellite_palette(const std::vector<std::array<float, 3>>& palette);
    void set_on_object_picked(std::function<void(size_t)> cb);
    void set_on_texture_debug_info(std::function<void(const std::string&)> cb);
    void set_texture_loader_service(const std::shared_ptr<LodTexturesLoaderService>& service);

private:
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float h = 0.0f;
        float m = 0.0f;
        float sr = 0.3f;
        float sg = 0.3f;
        float sb = 0.3f;
    };

    // Input world subset used for rendering.
    std::vector<float> heights_;
    int grid_w_ = 0;
    int grid_h_ = 0;
    float cell_size_ = 1.0f;
    float min_elevation_ = 0.0f;
    float max_elevation_ = 1.0f;
    std::vector<float> surface_classes_;
    std::vector<float> texture_indices_;
    std::vector<std::array<float, 3>> satellite_palette_;
    std::vector<float> object_points_;
    std::vector<float> object_positions_;

    // Camera.
    float pivot_[3] = {0.0f, 0.0f, 0.0f};
    float azimuth_ = 0.5f;
    float elevation_ = 0.8f;
    float distance_ = 500.0f;
    float drag_start_azimuth_ = 0.0f;
    float drag_start_elevation_ = 0.0f;
    float drag_start_pivot_[3] = {0.0f, 0.0f, 0.0f};

    // Render flags.
    bool wireframe_ = false;
    bool show_objects_ = true;
    int color_mode_ = 0; // 0=elevation, 1=surface mask, 2=texture index, 3=satellite
    float texture_index_max_ = 1.0f;

    // GL resources.
    uint32_t prog_terrain_ = 0;
    uint32_t prog_points_ = 0;
    int loc_mvp_terrain_ = -1;
    int loc_hmin_terrain_ = -1;
    int loc_hmax_terrain_ = -1;
    int loc_mode_terrain_ = -1;
    int loc_mvp_points_ = -1;
    uint32_t terrain_vao_ = 0;
    uint32_t terrain_vbo_ = 0;
    uint32_t terrain_ebo_ = 0;
    int terrain_index_count_ = 0;
    uint32_t points_vao_ = 0;
    uint32_t points_vbo_ = 0;
    int points_count_ = 0;

    std::shared_ptr<LodTexturesLoaderService> texture_loader_;
    std::vector<armatools::wrp::TextureEntry> texture_entries_;
    GLuint texture_atlas_ = 0;
    std::vector<uint8_t> texture_atlas_pixels_;
    int atlas_width_ = 0;
    int atlas_height_ = 0;
    std::vector<std::array<float, 4>> texture_lookup_uvs_;
    GLuint texture_lookup_tex_ = 0;
    int texture_lookup_size_ = 0;
    GLuint texture_index_tex_ = 0;
    int texture_index_tex_w_ = 0;
    int texture_index_tex_h_ = 0;
    float texture_world_scale_ = 32.0f;
    bool has_texture_atlas_ = false;
    bool has_texture_lookup_ = false;
    bool has_texture_index_ = false;
    int loc_texture_atlas_ = -1;
    int loc_texture_lookup_ = -1;
    int loc_texture_index_ = -1;
    int loc_texture_lookup_size_ = -1;
    int loc_texture_world_scale_ = -1;
    int loc_texture_cell_size_ = -1;
    int loc_texture_grid_w_ = -1;
    int loc_texture_grid_h_ = -1;
    int loc_has_texture_atlas_ = -1;
    int loc_has_texture_lookup_ = -1;
    int loc_has_texture_index_ = -1;
    int loc_camera_xz_ = -1;
    int loc_near_texture_distance_ = -1;
    float near_texture_distance_ = 1500.0f;
    sigc::connection texture_rebuild_idle_;
    std::vector<uint32_t> terrain_visible_indices_;
    int terrain_visible_index_count_ = 0;
    // Gesture controllers.
    Glib::RefPtr<Gtk::GestureDrag> drag_orbit_;
    Glib::RefPtr<Gtk::GestureDrag> drag_pan_;
    Glib::RefPtr<Gtk::EventControllerScroll> scroll_zoom_;
    Glib::RefPtr<Gtk::GestureClick> click_select_;
    Glib::RefPtr<Gtk::EventControllerKey> key_move_;
    sigc::connection move_tick_conn_;
    bool move_fwd_ = false;
    bool move_back_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;
    bool move_fast_ = false;
    bool alt_pressed_ = false;
    std::function<void(size_t)> on_object_picked_;
    std::function<void(const std::string&)> on_texture_debug_info_;
    std::string last_texture_debug_info_;
    double click_press_x_ = 0.0;
    double click_press_y_ = 0.0;

    // Gtk::GLArea hooks.
    void on_realize_gl();
    void on_unrealize_gl();
    bool on_render_gl(const Glib::RefPtr<Gdk::GLContext>& ctx);

    // GL helpers.
    void cleanup_gl();
    uint32_t compile_shader(uint32_t type, const char* src);
    uint32_t link_program(uint32_t vs, uint32_t fs);
    void rebuild_terrain_buffers();
    void rebuild_object_buffers();
    void build_mvp(float* mvp) const;
    void update_visible_terrain_indices(const float* mvp, const float* eye);
    void pick_object_at(double x, double y);
    void move_camera_local(float forward, float right);
    bool movement_tick();
    void rebuild_texture_atlas(const std::vector<armatools::wrp::TextureEntry>& textures);
    void schedule_texture_rebuild();
    void upload_texture_atlas();
    void upload_texture_lookup();
    void upload_texture_index();
    void cleanup_texture_atlas_gl();
    void cleanup_texture_lookup_gl();
    void cleanup_texture_index_gl();
};
