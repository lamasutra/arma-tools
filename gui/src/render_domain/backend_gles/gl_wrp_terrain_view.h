#pragma once

#include "app/wrp_terrain_camera_controller.h"
#include "domain/wrp_terrain_camera_types.h"

#include <gtkmm.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <deque>
#include <thread>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <epoxy/gl.h>

#include <armatools/p3d.h>
#include <armatools/wrp.h>

class TexturesLoaderService;
class P3dModelLoaderService;
#include "textures_loader.h"

class GLWrpTerrainView : public Gtk::GLArea {
public:
    GLWrpTerrainView();
    ~GLWrpTerrainView() override;

    void clear_world();
    void set_world_data(const armatools::wrp::WorldData& world);
    void set_objects(std::vector<armatools::wrp::ObjectRecord> objects);
    void set_wireframe(bool on);
    void set_show_objects(bool on);
    void set_object_max_distance(float distance_m);
    void set_object_category_filters(bool buildings, bool vegetation, bool rocks, bool props);
    void set_show_object_bounds(bool on);
    void set_show_water(bool on);
    void set_water_level(float level);
    void set_color_mode(int mode);
    void set_satellite_palette(const std::vector<std::array<float, 3>>& palette);
    void set_on_object_picked(std::function<void(size_t)> cb);
    void set_on_texture_debug_info(std::function<void(const std::string&)> cb);
    void set_on_terrain_stats(std::function<void(const std::string&)> cb);
    void set_on_compass_info(std::function<void(const std::string&)> cb);
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<TexturesLoaderService>& service);
    void set_show_patch_boundaries(bool on);
    void set_show_patch_lod_colors(bool on);
    void set_show_tile_boundaries(bool on);
    void set_terrain_far_distance(float distance_m);
    void set_material_quality_distances(float mid_distance_m, float far_distance_m);
    void set_seam_debug_mode(int mode);
    void set_camera_mode(wrpterrain::CameraMode mode);
    [[nodiscard]] wrpterrain::CameraMode camera_mode() const;

    void set_gravity_enabled(bool enabled);
    [[nodiscard]] bool gravity_enabled() const;

private:
    static constexpr int kMaxTerrainSurfaces = 4;
    static constexpr int kTerrainRoleCount = 14; // sat, mask, (macro/normal/detail)*4

    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float h = 0.0f;
        float m = 0.0f;
        float sr = 0.3f;
        float sg = 0.3f;
        float sb = 0.3f;
        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
    };

    struct TerrainPatch {
        int patch_x = 0;
        int patch_z = 0;
        int base_grid_x = 0;
        int base_grid_z = 0;
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        float center_x = 0.0f;
        float center_y = 0.0f;
        float center_z = 0.0f;
        int tile_min_x = 0;
        int tile_min_z = 0;
        int tile_max_x = 0;
        int tile_max_z = 0;
        int current_lod = 0;
        uint32_t vao = 0;
        uint32_t vbo = 0;
    };

    struct LodIndexBuffer {
        uint32_t ibo = 0;
        int index_count = 0;
        int step = 1;
    };

    struct CachedTileTexture {
        bool missing = false;
        bool layered = false;
        uint64_t last_used_stamp = 0;
        int surface_count = 0;
        struct LayerImage {
            bool present = false;
            int width = 0;
            int height = 0;
            std::vector<uint8_t> rgba;
        };
        LayerImage sat;
        LayerImage mask;
        struct SurfaceImages {
            LayerImage macro;
            LayerImage normal;
            LayerImage detail;
        };
        std::array<SurfaceImages, kMaxTerrainSurfaces> surfaces{};
    };

    struct TileLoadJob {
        int tile_index = -1;
        uint64_t generation = 0;
        std::vector<std::string> candidates;
    };

    struct TileLoadResult {
        int tile_index = -1;
        uint64_t generation = 0;
        CachedTileTexture texture;
    };

    // Input world subset used for rendering.
    std::vector<float> heights_;
    int grid_w_ = 0; // heightmap width
    int grid_h_ = 0; // heightmap height
    float cell_size_ = 1.0f; // geometry spacing
    float terrain_max_z_ = 0.0f;
    float world_size_x_ = 0.0f;
    float world_size_z_ = 0.0f;
    float min_elevation_ = 0.0f;
    float max_elevation_ = 1.0f;
    std::vector<float> surface_classes_;
    std::vector<uint16_t> tile_texture_indices_;
    int tile_grid_w_ = 0;
    int tile_grid_h_ = 0;
    float tile_cell_size_ = 1.0f;
    std::vector<std::array<float, 3>> satellite_palette_;
    std::vector<float> object_positions_;
    std::vector<armatools::wrp::ObjectRecord> objects_;

    // Camera state/behavior extracted for testability.
    WrpTerrainCameraController camera_controller_;
    float drag_start_azimuth_ = 0.0f;
    float drag_start_elevation_ = 0.0f;
    float drag_start_pivot_[3] = {0.0f, 0.0f, 0.0f};

    // Render flags.
    bool wireframe_ = false;
    bool show_objects_ = true;
    int color_mode_ = 0; // 0=elevation, 1=surface mask, 2=texture index, 3=satellite
    float texture_index_max_ = 1.0f;
    bool show_patch_boundaries_ = false;
    bool show_patch_lod_colors_ = false;
    bool show_tile_boundaries_ = false;
    bool show_object_bounds_ = false;
    bool show_water_ = true;
    float water_level_ = 0.0f;
    bool gravity_enabled_ = false;
    bool object_filter_buildings_ = true;
    bool object_filter_vegetation_ = true;
    bool object_filter_rocks_ = true;
    bool object_filter_props_ = true;
    float terrain_far_distance_ = 25000.0f;
    float object_max_distance_ = 4500.0f;
    float material_mid_distance_ = 1800.0f;
    float material_far_distance_ = 5200.0f;
    float object_spatial_cell_size_ = 160.0f;
    uint64_t object_asset_stamp_ = 1;
    size_t object_asset_budget_ = 160;

    // Terrain geometry.
    std::vector<TerrainPatch> terrain_patches_;
    std::array<LodIndexBuffer, 5> lod_index_buffers_{};
    std::vector<int> visible_patch_indices_;
    int patch_quads_ = 64;
    int patch_cols_ = 0;
    int patch_rows_ = 0;
    float skirt_drop_m_ = 6.0f;

    // GL resources.
    uint32_t prog_points_ = 0;
    int loc_mvp_points_ = -1;

    uint32_t prog_objects_ = 0;
    int loc_mvp_objects_ = -1;
    int loc_light_dir_objects_ = -1;
    int loc_color_objects_ = -1;
    int loc_texture_objects_ = -1;
    int loc_has_texture_objects_ = -1;
    uint32_t objects_instance_vbo_ = 0;
    uint32_t prog_selected_object_ = 0;
    int loc_mvp_selected_object_ = -1;
    int loc_offset_selected_object_ = -1;
    int loc_light_dir_selected_object_ = -1;
    int loc_color_selected_object_ = -1;

    uint32_t prog_water_ = 0;
    int loc_mvp_water_ = -1;
    int loc_offset_water_ = -1;
    int loc_color_water_ = -1;

    struct SelectedObjectLodMesh {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        int vertex_count = 0;
        float resolution = 0.0f;
    };

    struct SelectedObjectRender {
        bool valid = false;
        size_t object_index = static_cast<size_t>(-1);
        std::string model_name;
        float offset[3] = {0.0f, 0.0f, 0.0f};
        std::array<float, 3> color = {0.95f, 0.82f, 0.25f};
        float lod_base_distance = 120.0f;
        std::vector<SelectedObjectLodMesh> lod_meshes;
        int current_lod = 0;
    };

    enum class ObjectCategory : uint8_t {
        Buildings = 0,
        Vegetation = 1,
        Rocks = 2,
        Props = 3
    };

    struct ObjectMeshGroup {
        uint32_t vao = 0;
        uint32_t vbo = 0;
        int vertex_count = 0;
        uint32_t texture = 0;
        bool has_alpha = false;
    };

    struct ObjectLodMesh {
        std::vector<ObjectMeshGroup> groups;
        float resolution = 0.0f;
        float bounding_radius = 1.0f;
    };

    struct ObjectModelAsset {
        enum class State : uint8_t {
            Unloaded = 0,
            Loading = 1,
            Ready = 2,
            Failed = 3
        };
        State state = State::Unloaded;
        std::string model_name;
        ObjectCategory category = ObjectCategory::Props;
        std::vector<ObjectLodMesh> lod_meshes;
        GLuint fallback_texture = 0;
        float bounding_radius = 1.0f;
        uint64_t last_used_stamp = 0;
        bool missing_logged = false;
    };

    struct ObjectInstance {
        size_t object_index = static_cast<size_t>(-1);
        uint32_t model_id = 0;
        ObjectCategory category = ObjectCategory::Props;
        float model[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
        float position[3] = {0.0f, 0.0f, 0.0f};
        float max_scale = 1.0f;
        float bound_radius = 1.0f;
        int current_lod = 0;
    };

    std::unordered_map<std::string, uint32_t> object_model_lookup_;
    std::vector<ObjectModelAsset> object_model_assets_;
    std::vector<ObjectInstance> object_instances_;
    std::unordered_map<int64_t, std::vector<uint32_t>> object_spatial_grid_;

    struct TerrainProgram {
        uint32_t program = 0;
        int loc_mvp = -1;
        int loc_hmin = -1;
        int loc_hmax = -1;
        int loc_mode = -1;
        int loc_texture_index = -1;
        int loc_material_lookup = -1;
        int loc_material_lookup_rows = -1;
        int loc_texture_cell_size = -1;
        int loc_texture_grid_w = -1;
        int loc_texture_grid_h = -1;
        int loc_has_texture_index = -1;
        int loc_has_material_lookup = -1;
        int loc_camera_xz = -1;
        int loc_material_mid_distance = -1;
        int loc_material_far_distance = -1;
        int loc_show_patch_bounds = -1;
        int loc_show_tile_bounds = -1;
        int loc_show_lod_tint = -1;
        int loc_patch_bounds = -1;
        int loc_patch_lod_color = -1;
        int loc_tile_cell_size = -1;
        int loc_patch_lod = -1;
        int loc_sampler_count = -1;
        int loc_debug_mode = -1;
        int loc_seam_debug_mode = -1;
        int loc_terrain_max_z = -1;
        int loc_flip_terrain_z = -1;
        std::array<int, kTerrainRoleCount> loc_layer_atlas{};
    };
    std::unordered_map<uint32_t, TerrainProgram> terrain_program_cache_;
    uint32_t active_terrain_program_key_ = 0;
    int max_fragment_samplers_ = 16;
    int max_quality_supported_ = 2;
    int active_quality_tier_ = 0;
    int active_sampler_count_ = 0;
    int active_surface_cap_ = 1;
    int debug_material_mode_ = 0; // 0=final,1=sat,2=mask,3+=surface channels
    int seam_debug_mode_ = 0; // 0=final,1=depth-only,2=normals
    bool flip_terrain_z_ = true;
    SelectedObjectRender selected_object_;

    std::shared_ptr<P3dModelLoaderService> model_loader_;
    std::shared_ptr<TexturesLoaderService> texture_loader_;
    std::vector<armatools::wrp::TextureEntry> texture_entries_;
    std::array<GLuint, kTerrainRoleCount> layer_atlas_tex_{};
    std::array<std::vector<uint8_t>, kTerrainRoleCount> layer_atlas_pixels_{};
    std::array<int, kTerrainRoleCount> layer_atlas_w_{};
    std::array<int, kTerrainRoleCount> layer_atlas_h_{};
    std::array<bool, kTerrainRoleCount> has_layer_atlas_{};
    GLuint material_lookup_tex_ = 0;
    std::vector<float> material_lookup_pixels_;
    int material_lookup_w_ = 0;
    int material_lookup_rows_ = 0;
    GLuint texture_index_tex_ = 0;
    int texture_index_tex_w_ = 0;
    int texture_index_tex_h_ = 0;
    bool has_texture_index_ = false;
    bool has_material_lookup_ = false;
    sigc::connection texture_rebuild_idle_;
    std::unordered_map<int, CachedTileTexture> tile_texture_cache_;
    std::unordered_set<int> tile_missing_logged_once_;
    std::vector<int> last_visible_tile_indices_;
    uint64_t tile_cache_stamp_ = 1;
    size_t tile_cache_budget_entries_ = 384;
    uint64_t texture_cache_hits_ = 0;
    uint64_t texture_cache_misses_ = 0;
    int visible_tile_count_ = 0;
    int terrain_draw_calls_ = 0;
    int visible_patch_count_ = 0;
    int last_loaded_texture_count_ = 0;
    int object_candidate_count_ = 0;
    int object_visible_count_ = 0;
    int object_rendered_instances_ = 0;
    int object_distance_culled_count_ = 0;
    int object_frustum_culled_count_ = 0;
    int object_filtered_count_ = 0;
    int object_placeholder_count_ = 0;
    int object_draw_calls_ = 0;
    int object_instanced_batches_ = 0;
    uint64_t tile_generation_ = 1;
    bool atlas_dirty_ = true;
    bool atlas_empty_logged_ = false;
    int atlas_rebuild_debounce_frames_ = 0;
    std::mutex tile_jobs_mutex_;
    std::condition_variable tile_jobs_cv_;
    std::deque<TileLoadJob> tile_jobs_queue_;
    std::deque<TileLoadResult> tile_ready_queue_;
    std::unordered_set<int> tile_jobs_pending_;
    std::vector<std::thread> tile_workers_;
    bool tile_workers_stop_ = false;

    // Object background loading
    struct ObjectLoadJob {
        uint32_t model_id = 0;
        uint64_t generation = 0;
    };
    struct ObjectLoadPriorityJob {
        uint32_t model_id = 0;
        float distance = 0.0f;
        uint64_t generation = 0;
        bool operator<(const ObjectLoadPriorityJob& other) const {
            return distance > other.distance; // min-heap by distance (closer = higher priority)
        }
    };
    struct PreparedMeshGroup {
        std::vector<float> verts;
        std::string texture_key;
    };
    struct PreparedLodMesh {
        std::vector<PreparedMeshGroup> groups;
        float resolution = 0.0f;
        float bounding_radius = 1.0f;
        std::unordered_map<std::string, std::shared_ptr<const TexturesLoaderService::TextureData>> loaded_textures;
    };
    struct ObjectLoadResult {
        uint32_t model_id = 0;
        uint64_t generation = 0;
        bool success = false;
        std::vector<PreparedLodMesh> lods;
        float model_bounding_radius = 1.0f;
    };

    uint64_t object_generation_ = 1;
    std::mutex object_jobs_mutex_;
    std::condition_variable object_jobs_cv_;
    std::vector<ObjectLoadPriorityJob> object_jobs_queue_;
    std::deque<ObjectLoadResult> object_ready_queue_;
    std::unordered_set<uint32_t> object_jobs_pending_;
    std::vector<std::thread> object_workers_;
    bool object_workers_stop_ = false;

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
    std::function<void(const std::string&)> on_terrain_stats_;
    std::function<void(const std::string&)> on_compass_info_;
    std::string last_texture_debug_info_;
    std::string last_terrain_stats_;
    std::string last_compass_info_;
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
    uint32_t ensure_terrain_program(uint32_t key,
                                    int surface_cap,
                                    int quality_tier,
                                    bool has_normals,
                                    bool has_macro);
    void rebuild_terrain_buffers();
    void rebuild_patch_buffers();
    void rebuild_shared_lod_buffers();
    void cleanup_patch_buffers();
    void cleanup_lod_buffers();

    void clear_object_scene();
    void cleanup_object_model_assets();
    void build_object_instances();
    void build_object_instance_matrix(const armatools::wrp::ObjectRecord& obj, float* out_model) const;
    static int64_t spatial_cell_key(int cx, int cz);
    static ObjectCategory classify_object_category(const std::string& model_name);
    static std::array<float, 3> object_category_color(ObjectCategory category);
    bool object_category_enabled(ObjectCategory category) const;
    float object_category_max_distance(ObjectCategory category) const;
    bool ensure_object_model_asset(uint32_t model_id);
    bool build_object_model_asset(ObjectModelAsset& asset, const std::shared_ptr<const armatools::p3d::P3DFile>& model);
    void delete_object_model_asset_gl(ObjectModelAsset& asset);
    void evict_object_model_assets();
    int choose_object_lod(ObjectInstance& instance,
                          const ObjectModelAsset& asset,
                          float distance_m,
                          float projected_radius_px) const;
    void render_visible_object_meshes(const float* mvp, const float* eye);
    void append_object_bounds_vertices(const ObjectInstance& instance,
                                       const std::array<float, 3>& color,
                                       std::vector<float>& out) const;
    bool build_selected_object_render(size_t object_index, const std::shared_ptr<const armatools::p3d::P3DFile>& model);
    void clear_selected_object_render();
    int choose_selected_object_lod(const float* eye);
    static bool is_renderable_object_lod(const armatools::p3d::LOD& lod);
    void build_mvp(float* mvp) const;
    float render_z_from_grid(int gz) const;
    float source_z_from_render(float wz) const;
    void update_visible_patches(const float* mvp, const float* eye);
    int choose_patch_lod(const TerrainPatch& patch, const float* eye) const;
    float sample_height_clamped(int gx, int gz) const;
    float sample_height_at_world(float wx, float wz) const;
    std::array<float, 3> sample_world_normal_clamped(int gx, int gz) const;
#ifndef NDEBUG
    void validate_patch_edge_heights() const;
#endif
    void stream_visible_tile_textures();
    void enqueue_visible_tile_jobs(const std::vector<int>& selected_tiles);
    int drain_ready_tile_results(int max_results);
    void rebuild_tile_atlas_from_cache(const std::vector<int>& selected_tiles);
    CachedTileTexture load_tile_texture_sync(const TileLoadJob& job);
    void start_texture_workers();
    void stop_texture_workers();
    void texture_worker_loop();

    void start_object_workers();
    void stop_object_workers();
    void object_worker_loop();
    ObjectLoadResult prepare_object_model_asset_sync(uint32_t model_id, uint64_t generation);
    void drain_ready_object_results();

    std::vector<int> collect_visible_tile_indices() const;
    void emit_terrain_stats();
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
