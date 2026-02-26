#include "gl_wrp_terrain_view.h"

#include "gl_error_log.h"
#include "infra/gl/load_resource_text.h"
#include "log_panel.h"
#include "p3d_model_loader.h"
#include "textures_loader.h"
#include <armatools/objcat.h>

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

static constexpr const char* kTerrainVertResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_wrp_terrain.vert";
static constexpr const char* kTerrainFragResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_wrp_terrain.frag";
static constexpr const char* kPointVertResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_wrp_point.vert";
static constexpr const char* kPointFragResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_wrp_point.frag";
static constexpr const char* kSelectedObjectVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
uniform vec3 uOffset;
out vec3 vNormal;
void main() {
    gl_Position = uMVP * vec4(aPos + uOffset, 1.0);
    vNormal = normalize(aNormal);
}
)";
static constexpr const char* kSelectedObjectFragSrc = R"(
#version 330 core
in vec3 vNormal;
uniform vec3 uLightDir;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    float ndotl = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    float lit = 0.30 + 0.70 * ndotl;
    FragColor = vec4(uColor * lit, 1.0);
}
)";

struct FrustumPlane {
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 0.0f;
};

static void mat4_identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            tmp[j * 4 + i] = 0.0f;
            for (int k = 0; k < 4; ++k)
                tmp[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    }
    std::memcpy(out, tmp, sizeof(tmp));
}

static void mat4_perspective(float* m, float fov_rad, float aspect, float near_z, float far_z) {
    std::memset(m, 0, 16 * sizeof(float));
    const float f = 1.0f / std::tan(fov_rad * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far_z + near_z) / (near_z - far_z);
    m[11] = -1.0f;
    m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
}

static void vec3_cross(float* out, const float* a, const float* b) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void vec3_normalize(float* v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-8f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

static void mat4_look_at(float* m, const float* eye, const float* center, const float* up) {
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    vec3_normalize(f);
    float s[3];
    vec3_cross(s, f, up);
    vec3_normalize(s);
    float u[3];
    vec3_cross(u, s, f);

    mat4_identity(m);
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}

static std::array<FrustumPlane, 6> extract_frustum_planes(const float* m) {
    std::array<FrustumPlane, 6> planes{};
    planes[0] = {m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]};  // left
    planes[1] = {m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]};  // right
    planes[2] = {m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]};  // bottom
    planes[3] = {m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]};  // top
    planes[4] = {m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]}; // near
    planes[5] = {m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]}; // far

    for (auto& p : planes) {
        const float len = std::sqrt(p.a * p.a + p.b * p.b + p.c * p.c);
        if (len > 1e-8f) {
            p.a /= len;
            p.b /= len;
            p.c /= len;
            p.d /= len;
        }
    }
    return planes;
}

static bool aabb_inside_frustum(const std::array<FrustumPlane, 6>& frustum,
                                float min_x, float min_y, float min_z,
                                float max_x, float max_y, float max_z) {
    for (const auto& p : frustum) {
        const float px = (p.a >= 0.0f) ? max_x : min_x;
        const float py = (p.b >= 0.0f) ? max_y : min_y;
        const float pz = (p.c >= 0.0f) ? max_z : min_z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f)
            return false;
    }
    return true;
}

static std::array<float, 3> lod_tint_color(int lod) {
    switch (lod) {
    case 0: return {0.10f, 0.85f, 0.10f};
    case 1: return {0.25f, 0.75f, 0.95f};
    case 2: return {0.95f, 0.85f, 0.20f};
    case 3: return {0.95f, 0.45f, 0.15f};
    default: return {0.85f, 0.10f, 0.10f};
    }
}

static std::vector<uint8_t> make_missing_checkerboard_rgba() {
    static constexpr int kW = 4;
    static constexpr int kH = 4;
    std::vector<uint8_t> out(static_cast<size_t>(kW * kH * 4), 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool a = ((x + y) & 1) == 0;
            const size_t off = static_cast<size_t>(y * kW + x) * 4u;
            out[off + 0] = a ? 240 : 24;
            out[off + 1] = 0;
            out[off + 2] = a ? 240 : 24;
            out[off + 3] = 255;
        }
    }
    return out;
}

static uint32_t make_shader_key(int surface_cap, int quality_tier, bool has_normals, bool has_macro) {
    const uint32_t s = static_cast<uint32_t>(std::clamp(surface_cap, 1, 4));
    const uint32_t q = static_cast<uint32_t>(std::clamp(quality_tier, 0, 2));
    const uint32_t n = has_normals ? 1u : 0u;
    const uint32_t m = has_macro ? 1u : 0u;
    return (s << 4) | (q << 2) | (n << 1) | m;
}

static bool is_visual_resolution_name(const std::string& resolution_name) {
    return !resolution_name.empty() && resolution_name[0] >= '0' && resolution_name[0] <= '9';
}

static float wrap_degrees(float deg) {
    float out = std::fmod(deg, 360.0f);
    if (out < 0.0f) out += 360.0f;
    return out;
}

static std::string make_compass_text(float azimuth_rad) {
    static constexpr std::array<const char*, 8> kNorthRel = {
        "FWD", "FR", "RIGHT", "BR", "BACK", "BL", "LEFT", "FL"
    };
    const float heading_deg = wrap_degrees(-azimuth_rad * (180.0f / 3.14159265358979323846f));
    const float north_rel_deg = wrap_degrees(360.0f - heading_deg);
    const int idx = (static_cast<int>(std::floor((north_rel_deg + 22.5f) / 45.0f))) & 7;
    std::ostringstream ss;
    ss << "N:" << kNorthRel[static_cast<size_t>(idx)]
       << "  HDG " << static_cast<int>(std::lround(heading_deg)) << " deg";
    return ss.str();
}

} // namespace

GLWrpTerrainView::GLWrpTerrainView() {
    set_has_depth_buffer(true);
    set_auto_render(true);
    set_hexpand(true);
    set_vexpand(true);
    set_size_request(300, 220);
    set_focusable(true);

    signal_realize().connect(sigc::mem_fun(*this, &GLWrpTerrainView::on_realize_gl), false);
    signal_unrealize().connect(sigc::mem_fun(*this, &GLWrpTerrainView::on_unrealize_gl), false);
    signal_render().connect(sigc::mem_fun(*this, &GLWrpTerrainView::on_render_gl), false);

    drag_orbit_ = Gtk::GestureDrag::create();
    drag_orbit_->set_button(GDK_BUTTON_PRIMARY);
    drag_orbit_->signal_drag_begin().connect([this](double, double) {
        const auto state = camera_controller_.camera_state();
        drag_start_azimuth_ = state.azimuth;
        drag_start_elevation_ = state.elevation;
    });
    drag_orbit_->signal_drag_update().connect([this](double dx, double dy) {
        camera_controller_.orbit_from_drag(
            drag_start_azimuth_, drag_start_elevation_, dx, dy);
        queue_render();
    });
    add_controller(drag_orbit_);

    drag_pan_ = Gtk::GestureDrag::create();
    drag_pan_->set_button(GDK_BUTTON_MIDDLE);
    drag_pan_->signal_drag_begin().connect([this](double, double) {
        const auto state = camera_controller_.camera_state();
        std::memcpy(drag_start_pivot_, state.pivot, sizeof(drag_start_pivot_));
    });
    drag_pan_->signal_drag_update().connect([this](double dx, double dy) {
        camera_controller_.pan_from_drag(drag_start_pivot_, dx, dy);
        queue_render();
    });
    add_controller(drag_pan_);

    scroll_zoom_ = Gtk::EventControllerScroll::create();
    scroll_zoom_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_zoom_->signal_scroll().connect([this](double, double dy) -> bool {
        camera_controller_.zoom_from_scroll(dy);
        queue_render();
        return true;
    }, false);
    add_controller(scroll_zoom_);

    click_select_ = Gtk::GestureClick::create();
    click_select_->set_button(GDK_BUTTON_PRIMARY);
    click_select_->signal_pressed().connect([this](int, double x, double y) {
        grab_focus();
        click_press_x_ = x;
        click_press_y_ = y;
    });
    click_select_->signal_released().connect([this](int, double x, double y) {
        const double dx = x - click_press_x_;
        const double dy = y - click_press_y_;
        if ((dx * dx + dy * dy) <= 16.0)
            pick_object_at(x, y);
    });
    add_controller(click_select_);

    key_move_ = Gtk::EventControllerKey::create();
    key_move_->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
            bool handled = true;
            switch (keyval) {
            case GDK_KEY_w:
            case GDK_KEY_W:
                move_fwd_ = true;
                break;
            case GDK_KEY_s:
            case GDK_KEY_S:
                move_back_ = true;
                break;
            case GDK_KEY_a:
            case GDK_KEY_A:
                move_left_ = true;
                break;
            case GDK_KEY_d:
            case GDK_KEY_D:
                move_right_ = true;
                break;
            case GDK_KEY_q:
            case GDK_KEY_Q:
                move_up_ = true;
                break;
            case GDK_KEY_z:
            case GDK_KEY_Z:
                move_down_ = true;
                break;
            case GDK_KEY_Shift_L:
            case GDK_KEY_Shift_R:
                move_fast_ = true;
                break;
            case GDK_KEY_Alt_L:
            case GDK_KEY_Alt_R:
                alt_pressed_ = true;
                break;
            case GDK_KEY_0:
                debug_material_mode_ = 0;
                queue_render();
                break;
            case GDK_KEY_1:
                debug_material_mode_ = 1;
                queue_render();
                break;
            case GDK_KEY_2:
                debug_material_mode_ = 2;
                queue_render();
                break;
            case GDK_KEY_3:
                debug_material_mode_ = 3;
                queue_render();
                break;
            case GDK_KEY_4:
                debug_material_mode_ = 4;
                queue_render();
                break;
            case GDK_KEY_5:
                debug_material_mode_ = 5;
                queue_render();
                break;
            case GDK_KEY_6:
                debug_material_mode_ = 6;
                queue_render();
                break;
            case GDK_KEY_7:
                seam_debug_mode_ = 0;
                queue_render();
                break;
            case GDK_KEY_8:
                seam_debug_mode_ = 1;
                queue_render();
                break;
            case GDK_KEY_9:
                seam_debug_mode_ = 2;
                queue_render();
                break;
            default:
                handled = false;
                break;
            }
            if ((state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType(0))
                move_fast_ = true;
            if ((state & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType(0))
                alt_pressed_ = true;
            if (handled && !move_tick_conn_.connected()) {
                move_tick_conn_ = Glib::signal_timeout().connect(
                    sigc::mem_fun(*this, &GLWrpTerrainView::movement_tick), 16);
            }
            return handled;
        },
        false);
    key_move_->signal_key_released().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            switch (keyval) {
            case GDK_KEY_w:
            case GDK_KEY_W: move_fwd_ = false; break;
            case GDK_KEY_s:
            case GDK_KEY_S: move_back_ = false; break;
            case GDK_KEY_a:
            case GDK_KEY_A: move_left_ = false; break;
            case GDK_KEY_d:
            case GDK_KEY_D: move_right_ = false; break;
            case GDK_KEY_q:
            case GDK_KEY_Q: move_up_ = false; break;
            case GDK_KEY_z:
            case GDK_KEY_Z: move_down_ = false; break;
            case GDK_KEY_Shift_L:
            case GDK_KEY_Shift_R: move_fast_ = false; break;
            case GDK_KEY_Alt_L:
            case GDK_KEY_Alt_R: alt_pressed_ = false; break;
            default: break;
            }
            if ((state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType(0))
                move_fast_ = false;
            if ((state & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType(0))
                alt_pressed_ = false;
            if (!move_fwd_ && !move_back_ && !move_left_ && !move_right_
                && !move_up_ && !move_down_) {
                move_tick_conn_.disconnect();
            }
        });
    add_controller(key_move_);

    start_texture_workers();
}

GLWrpTerrainView::~GLWrpTerrainView() {
    stop_texture_workers();
}

void GLWrpTerrainView::start_texture_workers() {
    stop_texture_workers();
    tile_workers_stop_ = false;
    const unsigned hc = std::thread::hardware_concurrency();
    const unsigned desired = std::clamp(hc > 1 ? hc - 1 : 2u, 2u, 8u);
    tile_workers_.reserve(desired);
    for (unsigned i = 0; i < desired; ++i) {
        tile_workers_.emplace_back([this]() { texture_worker_loop(); });
    }
}

void GLWrpTerrainView::stop_texture_workers() {
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        tile_workers_stop_ = true;
        tile_jobs_queue_.clear();
        tile_ready_queue_.clear();
        tile_jobs_pending_.clear();
    }
    tile_jobs_cv_.notify_all();
    for (auto& worker : tile_workers_) {
        if (worker.joinable()) worker.join();
    }
    tile_workers_.clear();
}

void GLWrpTerrainView::texture_worker_loop() {
    for (;;) {
        TileLoadJob job;
        {
            std::unique_lock<std::mutex> lock(tile_jobs_mutex_);
            tile_jobs_cv_.wait(lock, [this]() {
                return tile_workers_stop_ || !tile_jobs_queue_.empty();
            });
            if (tile_workers_stop_) return;
            job = std::move(tile_jobs_queue_.front());
            tile_jobs_queue_.pop_front();
        }

        auto tex = load_tile_texture_sync(job);

        {
            std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
            tile_jobs_pending_.erase(job.tile_index);
            if (!tile_workers_stop_) {
                TileLoadResult ready;
                ready.tile_index = job.tile_index;
                ready.generation = job.generation;
                ready.texture = std::move(tex);
                tile_ready_queue_.push_back(std::move(ready));
            }
        }
    }
}

void GLWrpTerrainView::clear_world() {
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        tile_jobs_queue_.clear();
        tile_ready_queue_.clear();
        tile_jobs_pending_.clear();
    }
    tile_generation_++;
    atlas_dirty_ = true;
    atlas_empty_logged_ = false;
    atlas_rebuild_debounce_frames_ = 0;
    texture_entries_.clear();
    for (auto& p : layer_atlas_pixels_) p.clear();
    layer_atlas_w_.fill(0);
    layer_atlas_h_.fill(0);
    has_layer_atlas_.fill(false);
    material_lookup_pixels_.clear();
    material_lookup_w_ = 0;
    material_lookup_rows_ = 0;
    texture_index_tex_w_ = 0;
    texture_index_tex_h_ = 0;
    has_material_lookup_ = false;
    has_texture_index_ = false;
    tile_texture_cache_.clear();
    tile_missing_logged_once_.clear();
    last_visible_tile_indices_.clear();
    texture_cache_hits_ = 0;
    texture_cache_misses_ = 0;
    visible_tile_count_ = 0;
    terrain_draw_calls_ = 0;
    visible_patch_count_ = 0;
    last_loaded_texture_count_ = 0;

    cleanup_texture_atlas_gl();
    cleanup_texture_lookup_gl();
    cleanup_texture_index_gl();
    if (texture_rebuild_idle_) texture_rebuild_idle_.disconnect();

    heights_.clear();
    surface_classes_.clear();
    tile_texture_indices_.clear();
    satellite_palette_.clear();
    grid_w_ = 0;
    grid_h_ = 0;
    tile_grid_w_ = 0;
    tile_grid_h_ = 0;
    world_size_x_ = 0.0f;
    world_size_z_ = 0.0f;
    cell_size_ = 1.0f;
    terrain_max_z_ = 0.0f;
    tile_cell_size_ = 1.0f;
    object_points_.clear();
    object_positions_.clear();
    objects_.clear();
    clear_selected_object_render();
    min_elevation_ = 0.0f;
    max_elevation_ = 1.0f;
    texture_index_max_ = 1.0f;

    if (get_realized()) {
        rebuild_terrain_buffers();
        rebuild_object_buffers();
    }

    emit_terrain_stats();
    queue_render();
}

void GLWrpTerrainView::set_world_data(const armatools::wrp::WorldData& world) {
    const int src_w = world.grid.terrain_x;
    const int src_h = world.grid.terrain_y;
    if (src_w <= 1 || src_h <= 1 || world.elevations.empty()) {
        clear_world();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        tile_jobs_queue_.clear();
        tile_ready_queue_.clear();
        tile_jobs_pending_.clear();
    }
    tile_generation_++;
    atlas_dirty_ = true;
    atlas_empty_logged_ = false;
    atlas_rebuild_debounce_frames_ = 0;

    grid_w_ = src_w;
    grid_h_ = src_h;

    world_size_x_ = static_cast<float>(world.bounds.world_size_x);
    world_size_z_ = static_cast<float>(world.bounds.world_size_y);
    if (world_size_x_ <= 0.0f)
        world_size_x_ = static_cast<float>(std::max(world.grid.cells_x, 1))
            * static_cast<float>(std::max(world.grid.cell_size, 1.0));
    if (world_size_z_ <= 0.0f)
        world_size_z_ = static_cast<float>(std::max(world.grid.cells_y, 1))
            * static_cast<float>(std::max(world.grid.cell_size, 1.0));

    // Task 29 requirement: geometry spacing based on worldSize / heightmapSize.
    cell_size_ = world_size_x_ / static_cast<float>(std::max(grid_w_, 1));
    if (cell_size_ <= 0.0f)
        cell_size_ = std::max(1.0f, static_cast<float>(world.grid.cell_size));
    terrain_max_z_ = (grid_h_ > 0)
        ? (static_cast<float>(grid_h_ - 1) * cell_size_)
        : 0.0f;

    heights_.assign(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_), 0.0f);
    min_elevation_ = std::numeric_limits<float>::max();
    max_elevation_ = std::numeric_limits<float>::lowest();

    const size_t src_count = world.elevations.size();
    for (int z = 0; z < grid_h_; ++z) {
        for (int x = 0; x < grid_w_; ++x) {
            const size_t idx = static_cast<size_t>(z) * static_cast<size_t>(grid_w_) + static_cast<size_t>(x);
            float h = 0.0f;
            if (idx < src_count) h = world.elevations[idx];
            heights_[idx] = h;
            min_elevation_ = std::min(min_elevation_, h);
            max_elevation_ = std::max(max_elevation_, h);
        }
    }
    if (max_elevation_ <= min_elevation_)
        max_elevation_ = min_elevation_ + 1.0f;

    // Surface class grid in land cell space.
    const int land_w = std::max(world.grid.cells_x, 0);
    const int land_h = std::max(world.grid.cells_y, 0);
    const bool has_flags = land_w > 0 && land_h > 0
        && world.cell_bit_flags.size() >= static_cast<size_t>(land_w) * static_cast<size_t>(land_h);

    surface_classes_.assign(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_), 0.0f);

    auto clampi = [](int v, int lo, int hi) {
        return std::max(lo, std::min(v, hi));
    };

    auto flag_class = [&](int x, int z) -> float {
        if (!has_flags) return 0.0f;
        const float wx = static_cast<float>(x) * cell_size_;
        const float wz = static_cast<float>(z) * cell_size_;
        const float land_cell_x = world_size_x_ / static_cast<float>(std::max(land_w, 1));
        const float land_cell_z = world_size_z_ / static_cast<float>(std::max(land_h, 1));
        const int fx = clampi(static_cast<int>(std::floor(wx / std::max(land_cell_x, 0.0001f))),
                              0, land_w - 1);
        const int fz = clampi(static_cast<int>(std::floor(wz / std::max(land_cell_z, 0.0001f))),
                              0, land_h - 1);
        const size_t fi = static_cast<size_t>(fz) * static_cast<size_t>(land_w) + static_cast<size_t>(fx);
        if (fi >= world.cell_bit_flags.size()) return 0.0f;
        const uint32_t f = world.cell_bit_flags[fi];
        if (f & 0x40) return 5.0f;
        if (f & 0x20) return 4.0f;
        return static_cast<float>(f & 0x03);
    };

    // Material/tile grid from WRP cell texture indexes.
    const size_t tex_count = world.cell_texture_indexes.size();
    const size_t land_cells = static_cast<size_t>(std::max(land_w, 0)) * static_cast<size_t>(std::max(land_h, 0));
    const size_t terr_cells = static_cast<size_t>(std::max(grid_w_, 0)) * static_cast<size_t>(std::max(grid_h_, 0));

    tile_grid_w_ = 0;
    tile_grid_h_ = 0;
    if (land_w > 0 && land_h > 0 && tex_count == land_cells) {
        tile_grid_w_ = land_w;
        tile_grid_h_ = land_h;
    } else if (tex_count == terr_cells) {
        tile_grid_w_ = grid_w_;
        tile_grid_h_ = grid_h_;
    } else if (!world.cell_texture_indexes.empty()) {
        // Conservative fallback: assume square-ish grid.
        int side = static_cast<int>(std::sqrt(static_cast<double>(tex_count)));
        side = std::max(side, 1);
        tile_grid_w_ = side;
        tile_grid_h_ = static_cast<int>((tex_count + static_cast<size_t>(side) - 1) / static_cast<size_t>(side));
    }

    tile_texture_indices_.clear();
    if (tile_grid_w_ > 0 && tile_grid_h_ > 0) {
        tile_texture_indices_.assign(static_cast<size_t>(tile_grid_w_) * static_cast<size_t>(tile_grid_h_), 0);
        const size_t copy_n = std::min(tile_texture_indices_.size(), world.cell_texture_indexes.size());
        std::copy_n(world.cell_texture_indexes.begin(), copy_n, tile_texture_indices_.begin());
    }

    texture_index_max_ = 1.0f;
    for (uint16_t idx : tile_texture_indices_) {
        texture_index_max_ = std::max(texture_index_max_, static_cast<float>(idx));
    }

    tile_cell_size_ = (tile_grid_w_ > 0) ? (world_size_x_ / static_cast<float>(tile_grid_w_)) : cell_size_;
    if (tile_cell_size_ <= 0.0f) tile_cell_size_ = cell_size_;

    auto tile_index_at_world = [&](float wx, float wz) -> int {
        if (tile_grid_w_ <= 0 || tile_grid_h_ <= 0 || tile_texture_indices_.empty()) return -1;
        const float src_wz = source_z_from_render(wz);
        const int tx = clampi(static_cast<int>(std::floor(wx / std::max(tile_cell_size_, 0.0001f))),
                              0, tile_grid_w_ - 1);
        const int tz = clampi(static_cast<int>(std::floor(src_wz / std::max(tile_cell_size_, 0.0001f))),
                              0, tile_grid_h_ - 1);
        const size_t ti = static_cast<size_t>(tz) * static_cast<size_t>(tile_grid_w_) + static_cast<size_t>(tx);
        if (ti >= tile_texture_indices_.size()) return -1;
        return static_cast<int>(tile_texture_indices_[ti]);
    };

    for (int z = 0; z < grid_h_; ++z) {
        for (int x = 0; x < grid_w_; ++x) {
            const size_t idx = static_cast<size_t>(z) * static_cast<size_t>(grid_w_) + static_cast<size_t>(x);
            surface_classes_[idx] = flag_class(x, z);
            const int ti = tile_index_at_world(static_cast<float>(x) * cell_size_, static_cast<float>(z) * cell_size_);
            if (ti >= 0) texture_index_max_ = std::max(texture_index_max_, static_cast<float>(ti));
        }
    }

    texture_entries_ = world.textures;
    material_lookup_w_ = static_cast<int>(texture_entries_.size());
    material_lookup_rows_ = 0;
    material_lookup_pixels_.clear();
    tile_texture_cache_.clear();
    tile_missing_logged_once_.clear();
    last_visible_tile_indices_.clear();
    texture_cache_hits_ = 0;
    texture_cache_misses_ = 0;

    texture_index_tex_w_ = tile_grid_w_;
    texture_index_tex_h_ = tile_grid_h_;
    has_texture_index_ = tile_grid_w_ > 0 && tile_grid_h_ > 0 && !tile_texture_indices_.empty();

    set_objects(world.objects);

    camera_controller_.set_world_defaults(
        world_size_x_, world_size_z_, min_elevation_, max_elevation_);

    if (get_realized()) {
        rebuild_terrain_buffers();
        rebuild_object_buffers();
        upload_texture_index();
    }

    if (color_mode_ == 2) schedule_texture_rebuild();

    std::ostringstream ss;
    ss << "GLWrpTerrainView: terrain=" << grid_w_ << "x" << grid_h_
       << " land=" << land_w << "x" << land_h
       << " tile=" << tile_grid_w_ << "x" << tile_grid_h_
       << " geomCell=" << cell_size_ << "m"
       << " tileCell=" << tile_cell_size_ << "m"
       << " textures=" << texture_entries_.size();
    app_log(LogLevel::Debug, ss.str());

    emit_terrain_stats();
    queue_render();
}

void GLWrpTerrainView::set_objects(const std::vector<armatools::wrp::ObjectRecord>& objects) {
    objects_ = objects;
    object_points_.clear();
    object_positions_.clear();
    object_points_.reserve(objects.size() * 6);
    object_positions_.reserve(objects.size() * 3);
    for (const auto& obj : objects) {
        const auto cat = armatools::objcat::category(obj.model_name);
        float cr = 0.85f, cg = 0.85f, cb = 0.85f;
        if (cat == "vegetation") { cr = 0.15f; cg = 0.75f; cb = 0.20f; }
        else if (cat == "buildings") { cr = 0.90f; cg = 0.20f; cb = 0.20f; }
        else if (cat == "rocks") { cr = 0.50f; cg = 0.50f; cb = 0.52f; }
        else if (cat == "walls") { cr = 0.72f; cg = 0.64f; cb = 0.52f; }
        else if (cat == "military") { cr = 0.62f; cg = 0.62f; cb = 0.25f; }
        else if (cat == "infrastructure") { cr = 0.20f; cg = 0.20f; cb = 0.20f; }
        const float px = static_cast<float>(obj.position[0]);
        const float py = static_cast<float>(obj.position[1]) + 1.0f;
        const float src_z = static_cast<float>(obj.position[2]);
        const float pz = flip_terrain_z_ ? (terrain_max_z_ - src_z) : src_z;
        object_points_.push_back(px);
        object_points_.push_back(py);
        object_points_.push_back(pz);
        object_points_.push_back(cr);
        object_points_.push_back(cg);
        object_points_.push_back(cb);
        object_positions_.push_back(px);
        object_positions_.push_back(py);
        object_positions_.push_back(pz);
    }
    clear_selected_object_render();
    if (get_realized()) rebuild_object_buffers();
    queue_render();
}

void GLWrpTerrainView::set_wireframe(bool on) {
    wireframe_ = on;
    queue_render();
}

void GLWrpTerrainView::set_show_objects(bool on) {
    show_objects_ = on;
    queue_render();
}

void GLWrpTerrainView::set_show_patch_boundaries(bool on) {
    show_patch_boundaries_ = on;
    queue_render();
}

void GLWrpTerrainView::set_show_patch_lod_colors(bool on) {
    show_patch_lod_colors_ = on;
    queue_render();
}

void GLWrpTerrainView::set_show_tile_boundaries(bool on) {
    show_tile_boundaries_ = on;
    queue_render();
}

void GLWrpTerrainView::set_terrain_far_distance(float distance_m) {
    terrain_far_distance_ = std::clamp(distance_m, 500.0f, 250000.0f);
    queue_render();
}

void GLWrpTerrainView::set_material_quality_distances(float mid_distance_m, float far_distance_m) {
    material_mid_distance_ = std::clamp(mid_distance_m, 100.0f, 200000.0f);
    material_far_distance_ = std::clamp(far_distance_m, material_mid_distance_ + 1.0f, 250000.0f);
    queue_render();
}

void GLWrpTerrainView::set_seam_debug_mode(int mode) {
    seam_debug_mode_ = std::clamp(mode, 0, 2);
    queue_render();
}

void GLWrpTerrainView::set_camera_mode(wrpterrain::CameraMode mode) {
    if (!camera_controller_.set_camera_mode(mode)) return;
    queue_render();
}

wrpterrain::CameraMode GLWrpTerrainView::camera_mode() const {
    return camera_controller_.camera_mode();
}

void GLWrpTerrainView::set_color_mode(int mode) {
    const int prev_mode = color_mode_;
    color_mode_ = std::clamp(mode, 0, 3);
    if (color_mode_ == 2 && !texture_entries_.empty() && !tile_texture_indices_.empty()) {
        schedule_texture_rebuild();
    } else if (prev_mode == 2 && color_mode_ != 2) {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        tile_jobs_queue_.clear();
        tile_ready_queue_.clear();
        tile_jobs_pending_.clear();
    }
    queue_render();
}

void GLWrpTerrainView::set_satellite_palette(const std::vector<std::array<float, 3>>& palette) {
    satellite_palette_ = palette;
    if (get_realized()) rebuild_terrain_buffers();
    queue_render();
}

void GLWrpTerrainView::set_on_object_picked(std::function<void(size_t)> cb) {
    on_object_picked_ = std::move(cb);
}

void GLWrpTerrainView::set_on_texture_debug_info(std::function<void(const std::string&)> cb) {
    on_texture_debug_info_ = std::move(cb);
}

void GLWrpTerrainView::set_on_terrain_stats(std::function<void(const std::string&)> cb) {
    on_terrain_stats_ = std::move(cb);
    emit_terrain_stats();
}

void GLWrpTerrainView::set_on_compass_info(std::function<void(const std::string&)> cb) {
    on_compass_info_ = std::move(cb);
    if (on_compass_info_) {
        if (last_compass_info_.empty()) {
            const auto state = camera_controller_.camera_state();
            last_compass_info_ = make_compass_text(state.azimuth);
        }
        on_compass_info_(last_compass_info_);
    }
}

void GLWrpTerrainView::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_loader_ = service;
}

void GLWrpTerrainView::schedule_texture_rebuild() {
    if (!texture_loader_) return;
    if (texture_entries_.empty() || tile_texture_indices_.empty()) return;
    atlas_dirty_ = true;
    if (!texture_rebuild_idle_) {
        texture_rebuild_idle_ = Glib::signal_idle().connect([this]() {
            queue_render();
            texture_rebuild_idle_.disconnect();
            return false;
        });
    }
}

void GLWrpTerrainView::set_texture_loader_service(
    const std::shared_ptr<TexturesLoaderService>& service) {
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        texture_loader_ = service;
        tile_jobs_queue_.clear();
        tile_ready_queue_.clear();
        tile_jobs_pending_.clear();
    }
    tile_generation_++;
    atlas_dirty_ = true;
    atlas_rebuild_debounce_frames_ = 0;
    if (!texture_loader_) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        return;
    }
    if (color_mode_ == 2 && !texture_entries_.empty() && !tile_texture_indices_.empty())
        schedule_texture_rebuild();
}

void GLWrpTerrainView::rebuild_texture_atlas(const std::vector<armatools::wrp::TextureEntry>&) {
    stream_visible_tile_textures();
}

void GLWrpTerrainView::upload_texture_atlas() {
    if (!get_realized()) return;
    make_current();
    if (has_error()) return;
    for (int role = 0; role < kTerrainRoleCount; ++role) {
        auto& tex = layer_atlas_tex_[static_cast<size_t>(role)];
        const auto& pixels = layer_atlas_pixels_[static_cast<size_t>(role)];
        const int w = layer_atlas_w_[static_cast<size_t>(role)];
        const int h = layer_atlas_h_[static_cast<size_t>(role)];
        if (pixels.empty() || w <= 0 || h <= 0) {
            if (tex) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
            has_layer_atlas_[static_cast<size_t>(role)] = false;
            continue;
        }
        if (tex) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
        float max_aniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        if (max_aniso > 1.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            std::min(4.0f, max_aniso));
        }
#endif
        glGenerateMipmap(GL_TEXTURE_2D);
        has_layer_atlas_[static_cast<size_t>(role)] = true;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GLWrpTerrainView::upload_texture_lookup() {
    if (!get_realized() || material_lookup_pixels_.empty()
        || material_lookup_w_ <= 0 || material_lookup_rows_ <= 0) {
        return;
    }
    make_current();
    if (has_error()) return;
    if (material_lookup_tex_) {
        glDeleteTextures(1, &material_lookup_tex_);
        material_lookup_tex_ = 0;
    }
    glGenTextures(1, &material_lookup_tex_);
    glBindTexture(GL_TEXTURE_2D, material_lookup_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, material_lookup_w_, material_lookup_rows_, 0,
                 GL_RGBA, GL_FLOAT, material_lookup_pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    has_material_lookup_ = true;
}

void GLWrpTerrainView::upload_texture_index() {
    if (!get_realized() || tile_texture_indices_.empty() || tile_grid_w_ <= 0 || tile_grid_h_ <= 0)
        return;
    make_current();
    if (has_error()) return;
    if (texture_index_tex_) {
        glDeleteTextures(1, &texture_index_tex_);
        texture_index_tex_ = 0;
    }

    std::vector<float> tex_index_float;
    tex_index_float.reserve(tile_texture_indices_.size());
    for (uint16_t v : tile_texture_indices_)
        tex_index_float.push_back(static_cast<float>(v));

    glGenTextures(1, &texture_index_tex_);
    glBindTexture(GL_TEXTURE_2D, texture_index_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, tile_grid_w_, tile_grid_h_, 0,
                 GL_RED, GL_FLOAT, tex_index_float.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    texture_index_tex_w_ = tile_grid_w_;
    texture_index_tex_h_ = tile_grid_h_;
    has_texture_index_ = true;
}

void GLWrpTerrainView::cleanup_texture_atlas_gl() {
    bool any = false;
    for (auto tex : layer_atlas_tex_) {
        if (tex != 0) {
            any = true;
            break;
        }
    }
    if (!any) return;
    if (!get_realized()) {
        layer_atlas_tex_.fill(0);
        has_layer_atlas_.fill(false);
        return;
    }
    make_current();
    for (auto& tex : layer_atlas_tex_) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
    }
    has_layer_atlas_.fill(false);
}

void GLWrpTerrainView::cleanup_texture_lookup_gl() {
    if (material_lookup_tex_ == 0) {
        has_material_lookup_ = false;
        return;
    }
    if (!get_realized()) {
        material_lookup_tex_ = 0;
        has_material_lookup_ = false;
        return;
    }
    make_current();
    glDeleteTextures(1, &material_lookup_tex_);
    material_lookup_tex_ = 0;
    has_material_lookup_ = false;
}

void GLWrpTerrainView::cleanup_texture_index_gl() {
    if (texture_index_tex_ == 0) {
        has_texture_index_ = false;
        return;
    }
    if (!get_realized()) {
        texture_index_tex_ = 0;
        has_texture_index_ = false;
        return;
    }
    make_current();
    glDeleteTextures(1, &texture_index_tex_);
    texture_index_tex_ = 0;
    has_texture_index_ = false;
}

void GLWrpTerrainView::on_realize_gl() {
    make_current();
    if (has_error()) {
        app_log(LogLevel::Error, "GLWrpTerrainView: GL context creation failed");
        return;
    }

    const std::string point_vert_src = infra::gl::load_resource_text(kPointVertResource);
    const std::string point_frag_src = infra::gl::load_resource_text(kPointFragResource);
    auto pvs = compile_shader(GL_VERTEX_SHADER, point_vert_src.c_str());
    auto pfs = compile_shader(GL_FRAGMENT_SHADER, point_frag_src.c_str());
    prog_points_ = link_program(pvs, pfs);
    glDeleteShader(pvs);
    glDeleteShader(pfs);

    auto ovs = compile_shader(GL_VERTEX_SHADER, kSelectedObjectVertSrc);
    auto ofs = compile_shader(GL_FRAGMENT_SHADER, kSelectedObjectFragSrc);
    prog_selected_object_ = link_program(ovs, ofs);
    glDeleteShader(ovs);
    glDeleteShader(ofs);

    loc_mvp_points_ = glGetUniformLocation(prog_points_, "uMVP");
    loc_mvp_selected_object_ = glGetUniformLocation(prog_selected_object_, "uMVP");
    loc_offset_selected_object_ = glGetUniformLocation(prog_selected_object_, "uOffset");
    loc_light_dir_selected_object_ = glGetUniformLocation(prog_selected_object_, "uLightDir");
    loc_color_selected_object_ = glGetUniformLocation(prog_selected_object_, "uColor");
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_fragment_samplers_);
    if (max_fragment_samplers_ <= 0) max_fragment_samplers_ = 16;

    // 2 fixed samplers (index + material lookup) plus quality-dependent layered channels.
    const int need_mid = 8;   // index + lookup + sat + mask + 4 detail maps
    const int need_near = 16; // index + lookup + sat + mask + (macro/normal/detail)*4
    if (max_fragment_samplers_ >= need_near) max_quality_supported_ = 2;
    else if (max_fragment_samplers_ >= need_mid) max_quality_supported_ = 1;
    else max_quality_supported_ = 0;

    active_quality_tier_ = max_quality_supported_;
    active_surface_cap_ = 4;
    active_terrain_program_key_ = ensure_terrain_program(
        make_shader_key(active_surface_cap_, active_quality_tier_, true, true),
        active_surface_cap_, active_quality_tier_, true, true);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    rebuild_terrain_buffers();
    rebuild_object_buffers();
    upload_texture_atlas();
    upload_texture_lookup();
    upload_texture_index();
    log_gl_errors("GLWrpTerrainView::on_realize_gl");
}

void GLWrpTerrainView::on_unrealize_gl() {
    make_current();
    if (has_error()) return;
    cleanup_gl();
    log_gl_errors("GLWrpTerrainView::on_unrealize_gl");
}

bool GLWrpTerrainView::on_render_gl(const Glib::RefPtr<Gdk::GLContext>&) {
    glClearColor(0.14f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto camera_state = camera_controller_.camera_state();
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    camera_controller_.build_eye_center(eye, center);

    float mvp[16];
    build_mvp(mvp);
    update_visible_patches(mvp, eye);

    if (color_mode_ == 2)
        stream_visible_tile_textures();

    bool has_normals = false;
    bool has_macro = false;
    if (color_mode_ == 2) {
        for (int ti : last_visible_tile_indices_) {
            auto it = tile_texture_cache_.find(ti);
            if (it == tile_texture_cache_.end()) continue;
            const int surf = std::clamp(it->second.surface_count, 0, 4);
            for (int i = 0; i < surf; ++i) {
                has_normals = has_normals
                    || it->second.surfaces[static_cast<size_t>(i)].normal.present;
                has_macro = has_macro
                    || it->second.surfaces[static_cast<size_t>(i)].macro.present;
            }
            if (has_normals && has_macro) break;
        }
    }

    int desired_quality = 2;
    const float camera_distance = camera_controller_.distance();
    if (camera_distance > material_far_distance_) desired_quality = 0;
    else if (camera_distance > material_mid_distance_) desired_quality = 1;
    desired_quality = std::clamp(desired_quality, 0, max_quality_supported_);
    active_quality_tier_ = desired_quality;
    const int surface_cap_hw = std::clamp((max_fragment_samplers_ - 4) / 3, 1, 4);
    const int render_surface_cap = std::clamp(
        std::min(active_surface_cap_, surface_cap_hw), 1, 4);
    active_surface_cap_ = render_surface_cap;
    const uint32_t shader_key = make_shader_key(
        render_surface_cap,
        active_quality_tier_,
        has_normals,
        has_macro);
    active_terrain_program_key_ = ensure_terrain_program(
        shader_key,
        render_surface_cap,
        active_quality_tier_,
        has_normals,
        has_macro);
    auto program_it = terrain_program_cache_.find(active_terrain_program_key_);
    if (program_it == terrain_program_cache_.end() || program_it->second.program == 0) return true;
    TerrainProgram& tp = program_it->second;

    int features_per_surface = 0;
    if (active_quality_tier_ == 1) features_per_surface = 1;
    else if (active_quality_tier_ >= 2) features_per_surface = (has_macro || has_normals) ? 3 : 1;
    active_sampler_count_ = 2 + 1 + ((active_quality_tier_ > 0) ? 1 : 0)
        + render_surface_cap * features_per_surface;

    terrain_draw_calls_ = 0;

    if (!terrain_patches_.empty() && !visible_patch_indices_.empty()) {
        glUseProgram(tp.program);
        if (tp.loc_mvp >= 0) glUniformMatrix4fv(tp.loc_mvp, 1, GL_FALSE, mvp);
        if (tp.loc_hmin >= 0) glUniform1f(tp.loc_hmin, min_elevation_);
        if (tp.loc_hmax >= 0) glUniform1f(tp.loc_hmax, max_elevation_);
        if (tp.loc_mode >= 0) glUniform1i(tp.loc_mode, color_mode_);
        if (tp.loc_camera_xz >= 0) glUniform2f(tp.loc_camera_xz, eye[0], eye[2]);
        if (tp.loc_material_mid_distance >= 0)
            glUniform1f(tp.loc_material_mid_distance, material_mid_distance_);
        if (tp.loc_material_far_distance >= 0)
            glUniform1f(tp.loc_material_far_distance, material_far_distance_);
        if (tp.loc_texture_cell_size >= 0) glUniform1f(tp.loc_texture_cell_size, tile_cell_size_);
        if (tp.loc_texture_grid_w >= 0) glUniform1i(tp.loc_texture_grid_w, texture_index_tex_w_);
        if (tp.loc_texture_grid_h >= 0) glUniform1i(tp.loc_texture_grid_h, texture_index_tex_h_);
        if (tp.loc_material_lookup_rows >= 0) glUniform1i(tp.loc_material_lookup_rows, material_lookup_rows_);
        if (tp.loc_has_texture_index >= 0) glUniform1i(tp.loc_has_texture_index, has_texture_index_ ? 1 : 0);
        if (tp.loc_has_material_lookup >= 0) glUniform1i(tp.loc_has_material_lookup, has_material_lookup_ ? 1 : 0);
        if (tp.loc_sampler_count >= 0) glUniform1i(tp.loc_sampler_count, active_sampler_count_);
        if (tp.loc_debug_mode >= 0) glUniform1i(tp.loc_debug_mode, debug_material_mode_);
        if (tp.loc_seam_debug_mode >= 0) glUniform1i(tp.loc_seam_debug_mode, seam_debug_mode_);
        if (tp.loc_terrain_max_z >= 0) glUniform1f(tp.loc_terrain_max_z, terrain_max_z_);
        if (tp.loc_flip_terrain_z >= 0) glUniform1i(tp.loc_flip_terrain_z, flip_terrain_z_ ? 1 : 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, has_texture_index_ ? texture_index_tex_ : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, has_material_lookup_ ? material_lookup_tex_ : 0);
        for (int role = 0; role < kTerrainRoleCount; ++role) {
            glActiveTexture(static_cast<GLenum>(GL_TEXTURE2 + role));
            const bool has = has_layer_atlas_[static_cast<size_t>(role)];
            glBindTexture(GL_TEXTURE_2D, has ? layer_atlas_tex_[static_cast<size_t>(role)] : 0);
        }
        glActiveTexture(GL_TEXTURE0);

        if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        for (int patch_idx : visible_patch_indices_) {
            if (patch_idx < 0 || patch_idx >= static_cast<int>(terrain_patches_.size())) continue;
            auto& patch = terrain_patches_[static_cast<size_t>(patch_idx)];
            const int lod = std::clamp(patch.current_lod, 0, static_cast<int>(lod_index_buffers_.size()) - 1);
            const auto& ib = lod_index_buffers_[static_cast<size_t>(lod)];
            if (patch.vao == 0 || ib.ibo == 0 || ib.index_count <= 0) continue;

            if (tp.loc_show_patch_bounds >= 0)
                glUniform1i(tp.loc_show_patch_bounds, show_patch_boundaries_ ? 1 : 0);
            if (tp.loc_show_tile_bounds >= 0)
                glUniform1i(tp.loc_show_tile_bounds, show_tile_boundaries_ ? 1 : 0);
            if (tp.loc_show_lod_tint >= 0)
                glUniform1i(tp.loc_show_lod_tint, show_patch_lod_colors_ ? 1 : 0);
            if (tp.loc_tile_cell_size >= 0)
                glUniform1f(tp.loc_tile_cell_size, tile_cell_size_);
            if (tp.loc_patch_bounds >= 0)
                glUniform4f(tp.loc_patch_bounds, patch.min_x, patch.min_z, patch.max_x, patch.max_z);
            if (tp.loc_patch_lod >= 0)
                glUniform1i(tp.loc_patch_lod, lod);
            if (tp.loc_patch_lod_color >= 0) {
                const auto tint = lod_tint_color(lod);
                glUniform3f(tp.loc_patch_lod_color, tint[0], tint[1], tint[2]);
            }

            glBindVertexArray(patch.vao);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.ibo);
            glDrawElements(GL_TRIANGLES, ib.index_count, GL_UNSIGNED_INT, nullptr);
            terrain_draw_calls_++;
        }

        if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    if (show_objects_ && selected_object_.valid && prog_selected_object_
        && !selected_object_.lod_meshes.empty()) {
        const int lod = choose_selected_object_lod(eye);
        if (lod >= 0 && lod < static_cast<int>(selected_object_.lod_meshes.size())) {
            const auto& mesh = selected_object_.lod_meshes[static_cast<size_t>(lod)];
            if (mesh.vao != 0 && mesh.vertex_count > 0) {
                glUseProgram(prog_selected_object_);
                if (loc_mvp_selected_object_ >= 0)
                    glUniformMatrix4fv(loc_mvp_selected_object_, 1, GL_FALSE, mvp);
                if (loc_offset_selected_object_ >= 0) {
                    glUniform3f(loc_offset_selected_object_,
                                selected_object_.offset[0],
                                selected_object_.offset[1],
                                selected_object_.offset[2]);
                }
                if (loc_light_dir_selected_object_ >= 0)
                    glUniform3f(loc_light_dir_selected_object_, 0.26f, 0.93f, 0.19f);
                if (loc_color_selected_object_ >= 0) {
                    glUniform3f(loc_color_selected_object_,
                                selected_object_.color[0],
                                selected_object_.color[1],
                                selected_object_.color[2]);
                }
                if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glBindVertexArray(mesh.vao);
                glDrawArrays(GL_TRIANGLES, 0, mesh.vertex_count);
                terrain_draw_calls_++;
                if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
    }

    if (show_objects_ && points_vao_ && points_count_ > 0 && prog_points_) {
        glUseProgram(prog_points_);
        glUniformMatrix4fv(loc_mvp_points_, 1, GL_FALSE, mvp);
        glBindVertexArray(points_vao_);
        glDrawArrays(GL_POINTS, 0, points_count_);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    if (on_texture_debug_info_) {
        std::string info;
        if (color_mode_ == 2 && tile_grid_w_ > 0 && tile_grid_h_ > 0 && !tile_texture_indices_.empty()) {
            const float* pivot = camera_controller_.pivot();
            const float pivot_src_z = source_z_from_render(pivot[2]);
            const int cx = std::clamp(static_cast<int>(std::floor(pivot[0] / std::max(tile_cell_size_, 0.0001f))),
                                      0, tile_grid_w_ - 1);
            const int cz = std::clamp(static_cast<int>(std::floor(pivot_src_z / std::max(tile_cell_size_, 0.0001f))),
                                      0, tile_grid_h_ - 1);
            const size_t cidx = static_cast<size_t>(cz) * static_cast<size_t>(tile_grid_w_) + static_cast<size_t>(cx);
            const int ti = (cidx < tile_texture_indices_.size())
                ? static_cast<int>(tile_texture_indices_[cidx]) : -1;
            std::string state = "invalid";
            int surface_count = 0;
            if (ti >= 0 && ti < static_cast<int>(texture_entries_.size())) {
                auto it = tile_texture_cache_.find(ti);
                if (it != tile_texture_cache_.end()) {
                    surface_count = std::clamp(it->second.surface_count, 0, 4);
                    state = it->second.missing ? "missing" : "resolved";
                } else {
                    state = "pending";
                }
            }
            std::ostringstream ss;
            ss << "Tile[" << cx << "," << cz << "] idx=" << ti
               << " state=" << state
               << " surfaces=" << surface_count
               << " cap=" << active_surface_cap_
               << " tier=" << active_quality_tier_
               << " key=0x" << std::hex << active_terrain_program_key_ << std::dec
               << " samplers=" << active_sampler_count_
               << " | patches " << visible_patch_count_ << "/" << terrain_patches_.size()
               << " draws " << terrain_draw_calls_
               << " tiles " << visible_tile_count_
               << " dbg(" << debug_material_mode_ << "/" << seam_debug_mode_ << ")";
            info = ss.str();
        }
        if (info != last_texture_debug_info_) {
            last_texture_debug_info_ = info;
            on_texture_debug_info_(info);
        }
    }

    emit_terrain_stats();
    if (on_compass_info_) {
        const auto compass = make_compass_text(camera_state.azimuth);
        if (compass != last_compass_info_) {
            last_compass_info_ = compass;
            on_compass_info_(compass);
        }
    }
    log_gl_errors("GLWrpTerrainView::on_render_gl");
    return true;
}

void GLWrpTerrainView::cleanup_patch_buffers() {
    if (!get_realized()) {
        for (auto& p : terrain_patches_) {
            p.vao = 0;
            p.vbo = 0;
        }
        terrain_patches_.clear();
        visible_patch_indices_.clear();
        return;
    }

    for (auto& p : terrain_patches_) {
        if (p.vao) {
            glDeleteVertexArrays(1, &p.vao);
            p.vao = 0;
        }
        if (p.vbo) {
            glDeleteBuffers(1, &p.vbo);
            p.vbo = 0;
        }
    }
    terrain_patches_.clear();
    visible_patch_indices_.clear();
}

void GLWrpTerrainView::cleanup_lod_buffers() {
    for (auto& lod : lod_index_buffers_) {
        if (lod.ibo && get_realized()) {
            glDeleteBuffers(1, &lod.ibo);
        }
        lod.ibo = 0;
        lod.index_count = 0;
    }
}

void GLWrpTerrainView::cleanup_gl() {
    cleanup_patch_buffers();
    cleanup_lod_buffers();
    clear_selected_object_render();

    if (points_vao_) { glDeleteVertexArrays(1, &points_vao_); points_vao_ = 0; }
    if (points_vbo_) { glDeleteBuffers(1, &points_vbo_); points_vbo_ = 0; }
    points_count_ = 0;

    for (auto& [_, program] : terrain_program_cache_) {
        if (program.program) glDeleteProgram(program.program);
        program.program = 0;
    }
    terrain_program_cache_.clear();
    active_terrain_program_key_ = 0;
    if (prog_points_) { glDeleteProgram(prog_points_); prog_points_ = 0; }
    if (prog_selected_object_) {
        glDeleteProgram(prog_selected_object_);
        prog_selected_object_ = 0;
    }
    cleanup_texture_atlas_gl();
    cleanup_texture_lookup_gl();
    cleanup_texture_index_gl();
}

uint32_t GLWrpTerrainView::compile_shader(uint32_t type, const char* src) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        app_log(LogLevel::Error, std::string("GLWrpTerrainView shader compile error: ") + log);
        set_error(Glib::Error(GDK_GL_ERROR, 0, std::string("Shader compile error: ") + log));
    }
    return shader;
}

uint32_t GLWrpTerrainView::link_program(uint32_t vs, uint32_t fs) {
    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        app_log(LogLevel::Error, std::string("GLWrpTerrainView program link error: ") + log);
        set_error(Glib::Error(GDK_GL_ERROR, 0, std::string("Program link error: ") + log));
    }
    return prog;
}

uint32_t GLWrpTerrainView::ensure_terrain_program(uint32_t key,
                                                  int surface_cap,
                                                  int quality_tier,
                                                  bool has_normals,
                                                  bool has_macro) {
    auto found = terrain_program_cache_.find(key);
    if (found != terrain_program_cache_.end() && found->second.program != 0) {
        return key;
    }

    static const std::string terrain_vert_src = infra::gl::load_resource_text(kTerrainVertResource);
    static const std::string terrain_frag_template_src =
        infra::gl::load_resource_text(kTerrainFragResource);

    std::string fs_src(terrain_frag_template_src);
    const size_t first_nl = fs_src.find('\n');
    if (first_nl != std::string::npos) {
        std::ostringstream defs;
        defs << "#define SURFACE_CAP " << std::clamp(surface_cap, 1, 4) << "\n";
        defs << "#define QUALITY_TIER " << std::clamp(quality_tier, 0, 2) << "\n";
        defs << "#define HAS_NORMALS " << (has_normals ? 1 : 0) << "\n";
        defs << "#define HAS_MACRO " << (has_macro ? 1 : 0) << "\n";
        fs_src.insert(first_nl + 1, defs.str());
    }

    auto vs = compile_shader(GL_VERTEX_SHADER, terrain_vert_src.c_str());
    auto fs = compile_shader(GL_FRAGMENT_SHADER, fs_src.c_str());
    auto prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    TerrainProgram p;
    p.program = prog;
    p.loc_layer_atlas.fill(-1);
    p.loc_mvp = glGetUniformLocation(prog, "uMVP");
    p.loc_hmin = glGetUniformLocation(prog, "uMinH");
    p.loc_hmax = glGetUniformLocation(prog, "uMaxH");
    p.loc_mode = glGetUniformLocation(prog, "uMode");
    p.loc_texture_index = glGetUniformLocation(prog, "uTextureIndex");
    p.loc_material_lookup = glGetUniformLocation(prog, "uMaterialLookup");
    p.loc_material_lookup_rows = glGetUniformLocation(prog, "uMaterialLookupRows");
    p.loc_texture_cell_size = glGetUniformLocation(prog, "uTextureCellSize");
    p.loc_texture_grid_w = glGetUniformLocation(prog, "uTextureGridW");
    p.loc_texture_grid_h = glGetUniformLocation(prog, "uTextureGridH");
    p.loc_has_texture_index = glGetUniformLocation(prog, "uHasTextureIndex");
    p.loc_has_material_lookup = glGetUniformLocation(prog, "uHasMaterialLookup");
    p.loc_camera_xz = glGetUniformLocation(prog, "uCameraXZ");
    p.loc_material_mid_distance = glGetUniformLocation(prog, "uMaterialMidDistance");
    p.loc_material_far_distance = glGetUniformLocation(prog, "uMaterialFarDistance");
    p.loc_show_patch_bounds = glGetUniformLocation(prog, "uShowPatchBounds");
    p.loc_show_tile_bounds = glGetUniformLocation(prog, "uShowTileBounds");
    p.loc_show_lod_tint = glGetUniformLocation(prog, "uShowLodTint");
    p.loc_patch_bounds = glGetUniformLocation(prog, "uPatchBounds");
    p.loc_patch_lod_color = glGetUniformLocation(prog, "uPatchLodColor");
    p.loc_tile_cell_size = glGetUniformLocation(prog, "uTileCellSize");
    p.loc_patch_lod = glGetUniformLocation(prog, "uPatchLod");
    p.loc_sampler_count = glGetUniformLocation(prog, "uSamplerCount");
    p.loc_debug_mode = glGetUniformLocation(prog, "uDebugMode");
    p.loc_seam_debug_mode = glGetUniformLocation(prog, "uSeamDebugMode");
    p.loc_terrain_max_z = glGetUniformLocation(prog, "uTerrainMaxZ");
    p.loc_flip_terrain_z = glGetUniformLocation(prog, "uFlipTerrainZ");
    for (int i = 0; i < kTerrainRoleCount; ++i) {
        std::ostringstream name;
        name << "uLayerAtlas" << i;
        p.loc_layer_atlas[static_cast<size_t>(i)] = glGetUniformLocation(prog, name.str().c_str());
    }

    glUseProgram(prog);
    if (p.loc_texture_index >= 0) glUniform1i(p.loc_texture_index, 0);
    if (p.loc_material_lookup >= 0) glUniform1i(p.loc_material_lookup, 1);
    for (int i = 0; i < kTerrainRoleCount; ++i) {
        if (p.loc_layer_atlas[static_cast<size_t>(i)] >= 0)
            glUniform1i(p.loc_layer_atlas[static_cast<size_t>(i)], 2 + i);
    }
    glUseProgram(0);

    terrain_program_cache_[key] = p;
    return key;
}

void GLWrpTerrainView::rebuild_shared_lod_buffers() {
    cleanup_lod_buffers();

    static constexpr std::array<int, 5> kLodSteps = {1, 2, 4, 8, 16};
    const int side = patch_quads_ + 1;
    const int core_count = side * side;
    const int top_off = core_count;
    const int bottom_off = top_off + side;
    const int left_off = bottom_off + side;
    const int right_off = left_off + side;

    auto core_index = [side](int x, int z) -> uint32_t {
        return static_cast<uint32_t>(z * side + x);
    };

    for (size_t i = 0; i < lod_index_buffers_.size(); ++i) {
        const int step = kLodSteps[i];
        std::vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(patch_quads_ / step) * static_cast<size_t>(patch_quads_ / step) * 6u +
                        static_cast<size_t>(patch_quads_) * 24u);

        for (int z = 0; z < patch_quads_; z += step) {
            for (int x = 0; x < patch_quads_; x += step) {
                const uint32_t i00 = core_index(x, z);
                const uint32_t i10 = core_index(x + step, z);
                const uint32_t i01 = core_index(x, z + step);
                const uint32_t i11 = core_index(x + step, z + step);
                indices.push_back(i00); indices.push_back(i01); indices.push_back(i10);
                indices.push_back(i10); indices.push_back(i01); indices.push_back(i11);
            }
        }

        // Skirts: always full-resolution edges.
        for (int x = 0; x < patch_quads_; ++x) {
            const uint32_t c0 = core_index(x, 0);
            const uint32_t c1 = core_index(x + 1, 0);
            const uint32_t s0 = static_cast<uint32_t>(top_off + x);
            const uint32_t s1 = static_cast<uint32_t>(top_off + x + 1);
            indices.push_back(c0); indices.push_back(s0); indices.push_back(c1);
            indices.push_back(c1); indices.push_back(s0); indices.push_back(s1);
        }
        for (int x = 0; x < patch_quads_; ++x) {
            const uint32_t c0 = core_index(x, patch_quads_);
            const uint32_t c1 = core_index(x + 1, patch_quads_);
            const uint32_t s0 = static_cast<uint32_t>(bottom_off + x);
            const uint32_t s1 = static_cast<uint32_t>(bottom_off + x + 1);
            indices.push_back(c1); indices.push_back(s0); indices.push_back(c0);
            indices.push_back(c1); indices.push_back(s1); indices.push_back(s0);
        }
        for (int z = 0; z < patch_quads_; ++z) {
            const uint32_t c0 = core_index(0, z);
            const uint32_t c1 = core_index(0, z + 1);
            const uint32_t s0 = static_cast<uint32_t>(left_off + z);
            const uint32_t s1 = static_cast<uint32_t>(left_off + z + 1);
            indices.push_back(c1); indices.push_back(s0); indices.push_back(c0);
            indices.push_back(c1); indices.push_back(s1); indices.push_back(s0);
        }
        for (int z = 0; z < patch_quads_; ++z) {
            const uint32_t c0 = core_index(patch_quads_, z);
            const uint32_t c1 = core_index(patch_quads_, z + 1);
            const uint32_t s0 = static_cast<uint32_t>(right_off + z);
            const uint32_t s1 = static_cast<uint32_t>(right_off + z + 1);
            indices.push_back(c0); indices.push_back(s0); indices.push_back(c1);
            indices.push_back(c1); indices.push_back(s0); indices.push_back(s1);
        }

        auto& lod = lod_index_buffers_[i];
        lod.step = step;
        lod.index_count = static_cast<int>(indices.size());
        glGenBuffers(1, &lod.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                     indices.data(), GL_STATIC_DRAW);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

float GLWrpTerrainView::render_z_from_grid(int gz) const {
    const int clamped = std::clamp(gz, 0, std::max(0, grid_h_ - 1));
    const float src = static_cast<float>(clamped) * cell_size_;
    return flip_terrain_z_ ? (terrain_max_z_ - src) : src;
}

float GLWrpTerrainView::source_z_from_render(float wz) const {
    return flip_terrain_z_ ? (terrain_max_z_ - wz) : wz;
}

float GLWrpTerrainView::sample_height_clamped(int gx, int gz) const {
    if (grid_w_ <= 0 || grid_h_ <= 0 || heights_.empty()) return 0.0f;
    gx = std::clamp(gx, 0, grid_w_ - 1);
    gz = std::clamp(gz, 0, grid_h_ - 1);
    const size_t idx = static_cast<size_t>(gz) * static_cast<size_t>(grid_w_)
                     + static_cast<size_t>(gx);
    if (idx >= heights_.size()) return 0.0f;
    return heights_[idx];
}

std::array<float, 3> GLWrpTerrainView::sample_world_normal_clamped(int gx, int gz) const {
    const float hxl = sample_height_clamped(gx - 1, gz);
    const float hxr = sample_height_clamped(gx + 1, gz);
    const float hzd = sample_height_clamped(gx, gz - 1);
    const float hzu = sample_height_clamped(gx, gz + 1);
    const float inv_span = 1.0f / std::max(cell_size_ * 2.0f, 0.0001f);
    const float ddx = (hxr - hxl) * inv_span;
    const float ddz = (hzu - hzd) * inv_span;
    float n[3] = {-ddx, 1.0f, -ddz};
    vec3_normalize(n);
    return {n[0], n[1], n[2]};
}

#ifndef NDEBUG
void GLWrpTerrainView::validate_patch_edge_heights() const {
    if (terrain_patches_.empty() || patch_cols_ <= 0 || patch_rows_ <= 0) return;
    auto patch_ref = [this](int px, int pz) -> const TerrainPatch& {
        const size_t idx = static_cast<size_t>(pz) * static_cast<size_t>(patch_cols_)
                         + static_cast<size_t>(px);
        assert(idx < terrain_patches_.size());
        return terrain_patches_[idx];
    };
    auto height_at_local = [this](const TerrainPatch& p, int lx, int lz) -> float {
        const int gx = std::clamp(p.base_grid_x + lx, 0, grid_w_ - 1);
        const int gz = std::clamp(p.base_grid_z + lz, 0, grid_h_ - 1);
        return sample_height_clamped(gx, gz);
    };

    constexpr float eps = 1e-4f;
    for (int pz = 0; pz < patch_rows_; ++pz) {
        for (int px = 0; px < patch_cols_; ++px) {
            const auto& p = patch_ref(px, pz);
            if (px + 1 < patch_cols_) {
                const auto& n = patch_ref(px + 1, pz);
                for (int v = 0; v <= patch_quads_; ++v) {
                    const float h0 = height_at_local(p, patch_quads_, v);
                    const float h1 = height_at_local(n, 0, v);
                    assert(std::fabs(h0 - h1) <= eps);
                }
            }
            if (pz + 1 < patch_rows_) {
                const auto& n = patch_ref(px, pz + 1);
                for (int v = 0; v <= patch_quads_; ++v) {
                    const float h0 = height_at_local(p, v, patch_quads_);
                    const float h1 = height_at_local(n, v, 0);
                    assert(std::fabs(h0 - h1) <= eps);
                }
            }
        }
    }
}
#endif

void GLWrpTerrainView::rebuild_patch_buffers() {
    cleanup_patch_buffers();

    if (grid_w_ <= 1 || grid_h_ <= 1 || heights_.empty()) return;

    patch_quads_ = (std::max(grid_w_, grid_h_) <= 512) ? 32 : 64;
    patch_cols_ = std::max(1, (grid_w_ + patch_quads_ - 1) / patch_quads_);
    patch_rows_ = std::max(1, (grid_h_ + patch_quads_ - 1) / patch_quads_);
    skirt_drop_m_ = std::clamp(cell_size_ * 0.7f, 2.0f, 10.0f);

    rebuild_shared_lod_buffers();

    const int side = patch_quads_ + 1;
    const int core_count = side * side;
    const int top_off = core_count;
    const int bottom_off = top_off + side;
    const int left_off = bottom_off + side;
    const int right_off = left_off + side;

    auto idx_core = [side](int x, int z) {
        return static_cast<size_t>(z * side + x);
    };

    auto clampi = [](int v, int lo, int hi) {
        return std::max(lo, std::min(v, hi));
    };

    auto tile_index_at_world = [&](float wx, float wz) -> int {
        if (tile_grid_w_ <= 0 || tile_grid_h_ <= 0 || tile_texture_indices_.empty()) return -1;
        const float src_wz = source_z_from_render(wz);
        const int tx = clampi(static_cast<int>(std::floor(wx / std::max(tile_cell_size_, 0.0001f))),
                              0, tile_grid_w_ - 1);
        const int tz = clampi(static_cast<int>(std::floor(src_wz / std::max(tile_cell_size_, 0.0001f))),
                              0, tile_grid_h_ - 1);
        const size_t ti = static_cast<size_t>(tz) * static_cast<size_t>(tile_grid_w_) + static_cast<size_t>(tx);
        if (ti >= tile_texture_indices_.size()) return -1;
        return static_cast<int>(tile_texture_indices_[ti]);
    };

    terrain_patches_.reserve(static_cast<size_t>(patch_cols_) * static_cast<size_t>(patch_rows_));

    for (int pz = 0; pz < patch_rows_; ++pz) {
        for (int px = 0; px < patch_cols_; ++px) {
            const int base_x = px * patch_quads_;
            const int base_z = pz * patch_quads_;

            std::vector<Vertex> verts;
            verts.resize(static_cast<size_t>(core_count + side * 4));

            float min_x = std::numeric_limits<float>::max();
            float min_y = std::numeric_limits<float>::max();
            float min_z = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            float max_y = std::numeric_limits<float>::lowest();
            float max_z = std::numeric_limits<float>::lowest();

            for (int vz = 0; vz < side; ++vz) {
                const int src_z = std::clamp(base_z + vz, 0, grid_h_ - 1);
                for (int vx = 0; vx < side; ++vx) {
                    const int src_x = std::clamp(base_x + vx, 0, grid_w_ - 1);
                    const size_t src_idx = static_cast<size_t>(src_z) * static_cast<size_t>(grid_w_)
                        + static_cast<size_t>(src_x);
                    const float h = sample_height_clamped(src_x, src_z);
                    const float m = (src_idx < surface_classes_.size()) ? surface_classes_[src_idx] : 0.0f;
                    const float wx = static_cast<float>(src_x) * cell_size_;
                    const float wz = render_z_from_grid(src_z);
                    const auto n = sample_world_normal_clamped(src_x, src_z);

                    float sr = 0.30f, sg = 0.30f, sb = 0.30f;
                    const int ti = tile_index_at_world(wx, wz);
                    if (ti >= 0 && static_cast<size_t>(ti) < satellite_palette_.size()) {
                        sr = satellite_palette_[static_cast<size_t>(ti)][0];
                        sg = satellite_palette_[static_cast<size_t>(ti)][1];
                        sb = satellite_palette_[static_cast<size_t>(ti)][2];
                    }

                    const size_t vi = idx_core(vx, vz);
                    verts[vi] = Vertex{wx, h, wz, h, m, sr, sg, sb, n[0], n[1], n[2]};

                    min_x = std::min(min_x, wx);
                    min_y = std::min(min_y, h);
                    min_z = std::min(min_z, wz);
                    max_x = std::max(max_x, wx);
                    max_y = std::max(max_y, h);
                    max_z = std::max(max_z, wz);
                }
            }

            auto make_skirt = [&](size_t dst_idx, size_t src_idx) {
                verts[dst_idx] = verts[src_idx];
                verts[dst_idx].y -= skirt_drop_m_;
                min_y = std::min(min_y, verts[dst_idx].y);
            };

            for (int x = 0; x < side; ++x)
                make_skirt(static_cast<size_t>(top_off + x), idx_core(x, 0));
            for (int x = 0; x < side; ++x)
                make_skirt(static_cast<size_t>(bottom_off + x), idx_core(x, patch_quads_));
            for (int z = 0; z < side; ++z)
                make_skirt(static_cast<size_t>(left_off + z), idx_core(0, z));
            for (int z = 0; z < side; ++z)
                make_skirt(static_cast<size_t>(right_off + z), idx_core(patch_quads_, z));

            TerrainPatch patch;
            patch.patch_x = px;
            patch.patch_z = pz;
            patch.base_grid_x = base_x;
            patch.base_grid_z = base_z;
            patch.min_x = min_x;
            patch.min_y = min_y;
            patch.min_z = min_z;
            patch.max_x = max_x;
            patch.max_y = max_y;
            patch.max_z = max_z;
            patch.center_x = 0.5f * (min_x + max_x);
            patch.center_y = 0.5f * (min_y + max_y);
            patch.center_z = 0.5f * (min_z + max_z);
            patch.current_lod = 0;

            if (tile_grid_w_ > 0 && tile_grid_h_ > 0) {
                patch.tile_min_x = clampi(static_cast<int>(std::floor(min_x / std::max(tile_cell_size_, 0.0001f))),
                                          0, tile_grid_w_ - 1);
                patch.tile_max_x = clampi(static_cast<int>(std::floor(max_x / std::max(tile_cell_size_, 0.0001f))),
                                          0, tile_grid_w_ - 1);
                const int tz0 = clampi(static_cast<int>(std::floor(
                    source_z_from_render(min_z) / std::max(tile_cell_size_, 0.0001f))), 0, tile_grid_h_ - 1);
                const int tz1 = clampi(static_cast<int>(std::floor(
                    source_z_from_render(max_z) / std::max(tile_cell_size_, 0.0001f))), 0, tile_grid_h_ - 1);
                patch.tile_min_z = std::min(tz0, tz1);
                patch.tile_max_z = std::max(tz0, tz1);
            }

            glGenVertexArrays(1, &patch.vao);
            glGenBuffers(1, &patch.vbo);
            glBindVertexArray(patch.vao);
            glBindBuffer(GL_ARRAY_BUFFER, patch.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                         verts.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(3 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(4 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(5 * sizeof(float)));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<void*>(8 * sizeof(float)));
            glBindVertexArray(0);

            terrain_patches_.push_back(std::move(patch));
        }
    }

#ifndef NDEBUG
    validate_patch_edge_heights();
#endif
    visible_patch_indices_.reserve(terrain_patches_.size());
}

void GLWrpTerrainView::rebuild_terrain_buffers() {
    make_current();
    if (has_error()) return;

    rebuild_patch_buffers();
    upload_texture_index();
}

int GLWrpTerrainView::choose_patch_lod(const TerrainPatch& patch, const float* eye) const {
    const float dx = patch.center_x - eye[0];
    const float dy = patch.center_y - eye[1];
    const float dz = patch.center_z - eye[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    const float patch_span = static_cast<float>(patch_quads_) * cell_size_;
    const float b0 = std::max(220.0f, patch_span * 1.25f);
    const float b1 = b0 * 2.0f;
    const float b2 = b0 * 4.0f;
    const float b3 = b0 * 8.0f;
    const std::array<float, 4> bounds = {b0, b1, b2, b3};

    int lod = std::clamp(patch.current_lod, 0, 4);
    const float hysteresis = std::max(30.0f, patch_span * 0.20f);

    while (lod < 4 && dist > (bounds[static_cast<size_t>(lod)] + hysteresis)) lod++;
    while (lod > 0 && dist < (bounds[static_cast<size_t>(lod - 1)] - hysteresis)) lod--;
    return lod;
}

void GLWrpTerrainView::update_visible_patches(const float* mvp, const float* eye) {
    visible_patch_indices_.clear();
    visible_patch_count_ = 0;

    if (terrain_patches_.empty()) return;

    const auto frustum = extract_frustum_planes(mvp);
    const float far2 = terrain_far_distance_ * terrain_far_distance_;

    for (size_t i = 0; i < terrain_patches_.size(); ++i) {
        auto& patch = terrain_patches_[i];

        const float dx = patch.center_x - eye[0];
        const float dy = patch.center_y - eye[1];
        const float dz = patch.center_z - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > far2) continue;

        if (!aabb_inside_frustum(frustum,
                                 patch.min_x, patch.min_y, patch.min_z,
                                 patch.max_x, patch.max_y, patch.max_z)) {
            continue;
        }

        patch.current_lod = choose_patch_lod(patch, eye);
        visible_patch_indices_.push_back(static_cast<int>(i));
    }

    visible_patch_count_ = static_cast<int>(visible_patch_indices_.size());
}

std::vector<int> GLWrpTerrainView::collect_visible_tile_indices() const {
    std::vector<int> out;
    if (tile_grid_w_ <= 0 || tile_grid_h_ <= 0 || tile_texture_indices_.empty()) return out;

    std::unordered_set<int> uniq;
    uniq.reserve(visible_patch_indices_.size() * 8);

    for (int patch_idx : visible_patch_indices_) {
        if (patch_idx < 0 || patch_idx >= static_cast<int>(terrain_patches_.size())) continue;
        const auto& patch = terrain_patches_[static_cast<size_t>(patch_idx)];
        for (int z = patch.tile_min_z; z <= patch.tile_max_z; ++z) {
            for (int x = patch.tile_min_x; x <= patch.tile_max_x; ++x) {
                const size_t idx = static_cast<size_t>(z) * static_cast<size_t>(tile_grid_w_)
                                 + static_cast<size_t>(x);
                if (idx >= tile_texture_indices_.size()) continue;
                const int ti = static_cast<int>(tile_texture_indices_[idx]);
                if (ti < 0 || ti >= static_cast<int>(texture_entries_.size())) continue;
                uniq.insert(ti);
            }
        }
    }

    out.assign(uniq.begin(), uniq.end());
    std::sort(out.begin(), out.end());
    return out;
}

GLWrpTerrainView::CachedTileTexture GLWrpTerrainView::load_tile_texture_sync(const TileLoadJob& job) {
    CachedTileTexture out;
    out.missing = true;
    out.layered = false;
    out.surface_count = 0;
    out.sat.present = true;
    out.sat.width = 4;
    out.sat.height = 4;
    out.sat.rgba = make_missing_checkerboard_rgba();

    std::shared_ptr<TexturesLoaderService> loader;
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        loader = texture_loader_;
    }
    if (!loader) return out;

    if (auto layered = loader->load_terrain_layered_material(job.candidates)) {
        auto copy_layer = [](CachedTileTexture::LayerImage& dst,
                             const TexturesLoaderService::TerrainTextureLayer& src) {
            if (!src.present || src.image.width <= 0 || src.image.height <= 0
                || src.image.pixels.empty()) return;
            dst.present = true;
            dst.width = src.image.width;
            dst.height = src.image.height;
            dst.rgba = src.image.pixels;
        };

        out.layered = layered->layered;
        out.surface_count = std::clamp(layered->surface_count, 0, 4);
        copy_layer(out.sat, layered->satellite);
        copy_layer(out.mask, layered->mask);
        for (int i = 0; i < out.surface_count; ++i) {
            copy_layer(out.surfaces[static_cast<size_t>(i)].macro, layered->surfaces[static_cast<size_t>(i)].macro);
            copy_layer(out.surfaces[static_cast<size_t>(i)].normal, layered->surfaces[static_cast<size_t>(i)].normal);
            copy_layer(out.surfaces[static_cast<size_t>(i)].detail, layered->surfaces[static_cast<size_t>(i)].detail);
        }
        out.missing = !out.sat.present && !out.mask.present;
        if (!out.sat.present) {
            out.sat.present = true;
            out.sat.width = 4;
            out.sat.height = 4;
            out.sat.rgba = make_missing_checkerboard_rgba();
        }
        if (out.surface_count <= 0) {
            out.layered = false;
            out.surface_count = 0;
        }
        return out;
    }

    for (const auto& candidate : job.candidates) {
        if (candidate.empty()) continue;
        if (auto data = loader->load_terrain_texture_entry(candidate)) {
            if (data->image.width > 0 && data->image.height > 0 && !data->image.pixels.empty()) {
                out.missing = false;
                out.layered = false;
                out.surface_count = 0;
                out.sat.present = true;
                out.sat.width = data->image.width;
                out.sat.height = data->image.height;
                out.sat.rgba = data->image.pixels;
                return out;
            }
        }
    }

    return out;
}

void GLWrpTerrainView::enqueue_visible_tile_jobs(const std::vector<int>& selected_tiles) {
    if (!texture_loader_) return;
    if (texture_entries_.empty()) return;

    bool notify = false;
    std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
    for (int ti : selected_tiles) {
        if (ti < 0 || ti >= static_cast<int>(texture_entries_.size())) continue;
        if (tile_texture_cache_.find(ti) != tile_texture_cache_.end()) continue;
        if (tile_jobs_pending_.find(ti) != tile_jobs_pending_.end()) continue;

        TileLoadJob job;
        job.tile_index = ti;
        job.generation = tile_generation_;
        const auto& entry = texture_entries_[static_cast<size_t>(ti)];
        if (!entry.filenames.empty()) {
            job.candidates = entry.filenames;
        }
        if (!entry.filename.empty()) {
            if (std::find(job.candidates.begin(), job.candidates.end(), entry.filename)
                == job.candidates.end()) {
                job.candidates.push_back(entry.filename);
            }
        }

        if (job.candidates.empty()) {
            CachedTileTexture missing;
            missing.missing = true;
            missing.layered = false;
            missing.surface_count = 0;
            missing.sat.present = true;
            missing.sat.width = 4;
            missing.sat.height = 4;
            missing.sat.rgba = make_missing_checkerboard_rgba();
            missing.last_used_stamp = tile_cache_stamp_++;
            tile_texture_cache_[ti] = std::move(missing);
            atlas_dirty_ = true;
            continue;
        }

        tile_jobs_pending_.insert(ti);
        tile_jobs_queue_.push_back(std::move(job));
        texture_cache_misses_++;
        notify = true;
    }
    if (notify) tile_jobs_cv_.notify_all();
}

int GLWrpTerrainView::drain_ready_tile_results(int max_results) {
    std::vector<TileLoadResult> ready;
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        const int take = std::min(max_results, static_cast<int>(tile_ready_queue_.size()));
        ready.reserve(static_cast<size_t>(take));
        for (int i = 0; i < take; ++i) {
            ready.push_back(std::move(tile_ready_queue_.front()));
            tile_ready_queue_.pop_front();
        }
    }

    int applied = 0;
    for (auto& result : ready) {
        if (result.generation != tile_generation_) continue;
        result.texture.last_used_stamp = tile_cache_stamp_++;
        if (result.texture.missing && tile_missing_logged_once_.insert(result.tile_index).second) {
            app_log(LogLevel::Warning,
                    "GLWrpTerrainView: missing texture for tile material index "
                    + std::to_string(result.tile_index));
        }
        tile_texture_cache_[result.tile_index] = std::move(result.texture);
        texture_cache_hits_++;
        applied++;
    }
    return applied;
}

void GLWrpTerrainView::rebuild_tile_atlas_from_cache(const std::vector<int>& selected_tiles) {
    static constexpr int kPad = 2;
    static constexpr int kRowMax = 4096;
    static constexpr int kLookupRows = 15; // meta + sat + mask + 12 surface rows

    material_lookup_w_ = static_cast<int>(texture_entries_.size());
    material_lookup_rows_ = kLookupRows;
    material_lookup_pixels_.assign(
        static_cast<size_t>(std::max(material_lookup_w_, 1))
            * static_cast<size_t>(kLookupRows) * 4u,
        0.0f);

    auto lookup_ptr = [&](int tile_idx, int row) -> float* {
        if (tile_idx < 0 || tile_idx >= material_lookup_w_) return nullptr;
        if (row < 0 || row >= material_lookup_rows_) return nullptr;
        const size_t off = (static_cast<size_t>(row) * static_cast<size_t>(material_lookup_w_)
                            + static_cast<size_t>(tile_idx)) * 4u;
        return material_lookup_pixels_.data() + off;
    };

    auto write_slot = [&](int tile_idx, int row, float u, float v, float w, float h) {
        float* p = lookup_ptr(tile_idx, row);
        if (!p) return;
        p[0] = u;
        p[1] = v;
        p[2] = w;
        p[3] = h;
    };

    auto get_layer_for_role =
        [&](const CachedTileTexture& tex, int role) -> const CachedTileTexture::LayerImage* {
        if (role == 0) return &tex.sat;
        if (role == 1) return &tex.mask;
        if (role >= 2) {
            const int idx = role - 2;
            const int surface = idx / 3;
            const int channel = idx % 3;
            if (surface < 0 || surface >= 4) return nullptr;
            if (channel == 0) return &tex.surfaces[static_cast<size_t>(surface)].macro;
            if (channel == 1) return &tex.surfaces[static_cast<size_t>(surface)].normal;
            return &tex.surfaces[static_cast<size_t>(surface)].detail;
        }
        return nullptr;
    };

    int max_surface_count = 1;
    int resolved_layers = 0;

    for (int ti : selected_tiles) {
        auto it = tile_texture_cache_.find(ti);
        if (it == tile_texture_cache_.end()) continue;
        it->second.last_used_stamp = tile_cache_stamp_++;
        auto* meta = lookup_ptr(ti, 0);
        if (meta) {
            int surf_count = std::clamp(it->second.surface_count, 0, 4);
            meta[0] = static_cast<float>(surf_count);
            meta[1] = (it->second.layered && surf_count > 0) ? 1.0f : 0.0f;
            bool has_normals = false;
            bool has_macro = false;
            for (int i = 0; i < surf_count; ++i) {
                const auto& s = it->second.surfaces[static_cast<size_t>(i)];
                has_normals = has_normals || s.normal.present;
                has_macro = has_macro || s.macro.present;
            }
            meta[2] = has_normals ? 1.0f : 0.0f;
            meta[3] = has_macro ? 1.0f : 0.0f;
            max_surface_count = std::max(max_surface_count, std::max(1, surf_count));
        }
    }

    for (int role = 0; role < kTerrainRoleCount; ++role) {
        struct Packed {
            int tile_idx = -1;
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
        };

        std::vector<Packed> packed;
        packed.reserve(selected_tiles.size());
        int x = 0;
        int y = 0;
        int row_h = 0;
        int row_w_max = 0;

        for (int ti : selected_tiles) {
            auto it = tile_texture_cache_.find(ti);
            if (it == tile_texture_cache_.end()) continue;
            const auto* layer = get_layer_for_role(it->second, role);
            if (!layer || !layer->present || layer->rgba.empty()
                || layer->width <= 0 || layer->height <= 0) {
                continue;
            }
            const int w = std::max(1, layer->width);
            const int h = std::max(1, layer->height);
            const int pw = w + 2 * kPad;
            const int ph = h + 2 * kPad;
            if (x > 0 && (x + pw) > kRowMax) {
                row_w_max = std::max(row_w_max, x);
                x = 0;
                y += row_h;
                row_h = 0;
            }
            packed.push_back(Packed{ti, x + kPad, y + kPad, w, h});
            x += pw;
            row_h = std::max(row_h, ph);
            row_w_max = std::max(row_w_max, x);
        }

        if (packed.empty()) {
            layer_atlas_pixels_[static_cast<size_t>(role)].clear();
            layer_atlas_w_[static_cast<size_t>(role)] = 0;
            layer_atlas_h_[static_cast<size_t>(role)] = 0;
            has_layer_atlas_[static_cast<size_t>(role)] = false;
            continue;
        }

        row_w_max = std::max(row_w_max, x);
        const int atlas_w = std::max(1, row_w_max);
        const int atlas_h = std::max(1, y + row_h);
        auto& atlas_pixels = layer_atlas_pixels_[static_cast<size_t>(role)];
        atlas_pixels.assign(static_cast<size_t>(atlas_w) * static_cast<size_t>(atlas_h) * 4u, 0);

        for (const auto& p : packed) {
            auto it = tile_texture_cache_.find(p.tile_idx);
            if (it == tile_texture_cache_.end()) continue;
            const auto* layer = get_layer_for_role(it->second, role);
            if (!layer || layer->rgba.empty()) continue;

            for (int row = 0; row < p.h; ++row) {
                const size_t src_off = static_cast<size_t>(row) * static_cast<size_t>(p.w) * 4u;
                const size_t dst_off = (static_cast<size_t>(p.y + row) * static_cast<size_t>(atlas_w)
                                       + static_cast<size_t>(p.x)) * 4u;
                std::memcpy(atlas_pixels.data() + dst_off,
                            layer->rgba.data() + src_off,
                            static_cast<size_t>(p.w) * 4u);
            }

            for (int row = 0; row < p.h; ++row) {
                const size_t row_off = static_cast<size_t>(p.y + row) * static_cast<size_t>(atlas_w);
                const size_t left_src = (row_off + static_cast<size_t>(p.x)) * 4u;
                const size_t right_src = (row_off + static_cast<size_t>(p.x + p.w - 1)) * 4u;
                for (int pad = 1; pad <= kPad; ++pad) {
                    std::memcpy(atlas_pixels.data() + (left_src - static_cast<size_t>(pad) * 4u),
                                atlas_pixels.data() + left_src, 4u);
                    std::memcpy(atlas_pixels.data() + (right_src + static_cast<size_t>(pad) * 4u),
                                atlas_pixels.data() + right_src, 4u);
                }
            }
            for (int col = -kPad; col < p.w + kPad; ++col) {
                const int sx = p.x + col;
                const size_t top_src = (static_cast<size_t>(p.y) * static_cast<size_t>(atlas_w)
                                       + static_cast<size_t>(sx)) * 4u;
                const size_t bot_src = (static_cast<size_t>(p.y + p.h - 1) * static_cast<size_t>(atlas_w)
                                       + static_cast<size_t>(sx)) * 4u;
                for (int pad = 1; pad <= kPad; ++pad) {
                    const size_t top_dst = (static_cast<size_t>(p.y - pad) * static_cast<size_t>(atlas_w)
                                           + static_cast<size_t>(sx)) * 4u;
                    const size_t bot_dst = (static_cast<size_t>(p.y + p.h - 1 + pad)
                                           * static_cast<size_t>(atlas_w)
                                           + static_cast<size_t>(sx)) * 4u;
                    std::memcpy(atlas_pixels.data() + top_dst, atlas_pixels.data() + top_src, 4u);
                    std::memcpy(atlas_pixels.data() + bot_dst, atlas_pixels.data() + bot_src, 4u);
                }
            }

            int lookup_row = 1;
            if (role == 1) lookup_row = 2;
            else if (role >= 2) lookup_row = 3 + (role - 2);
            write_slot(
                p.tile_idx, lookup_row,
                static_cast<float>(p.x) / static_cast<float>(atlas_w),
                static_cast<float>(p.y) / static_cast<float>(atlas_h),
                static_cast<float>(p.w) / static_cast<float>(atlas_w),
                static_cast<float>(p.h) / static_cast<float>(atlas_h));
            resolved_layers++;
        }

        layer_atlas_w_[static_cast<size_t>(role)] = atlas_w;
        layer_atlas_h_[static_cast<size_t>(role)] = atlas_h;
        has_layer_atlas_[static_cast<size_t>(role)] = true;
    }

    active_surface_cap_ = std::clamp(max_surface_count, 1, 4);
    last_loaded_texture_count_ = resolved_layers;
    has_material_lookup_ = !material_lookup_pixels_.empty() && material_lookup_w_ > 0;

    bool any_atlas = false;
    for (bool has : has_layer_atlas_) any_atlas = any_atlas || has;
    if (!any_atlas) {
        if (!atlas_empty_logged_) {
            app_log(LogLevel::Debug,
                    "GLWrpTerrainView: terrain layered atlases empty (waiting for tile loads)");
            atlas_empty_logged_ = true;
        }
    } else {
        atlas_empty_logged_ = false;
    }

    if (get_realized()) {
        upload_texture_atlas();
        upload_texture_lookup();
    }
}

void GLWrpTerrainView::stream_visible_tile_textures() {
    if (!texture_loader_ || texture_entries_.empty()) {
        visible_tile_count_ = 0;
        return;
    }

    auto visible = collect_visible_tile_indices();
    visible_tile_count_ = static_cast<int>(visible.size());

    std::vector<int> selected = std::move(visible);
    static constexpr size_t kMaxAtlasTextures = 256;
    if (selected.size() > kMaxAtlasTextures) {
        selected.resize(kMaxAtlasTextures);
    }

    enqueue_visible_tile_jobs(selected);
    const int applied = drain_ready_tile_results(64);
    if (applied > 0) {
        atlas_dirty_ = true;
        atlas_rebuild_debounce_frames_ = 0;
    }

    const bool selected_changed = (selected != last_visible_tile_indices_);
    if (selected_changed) {
        last_visible_tile_indices_ = selected;
        if (!atlas_dirty_) {
            atlas_rebuild_debounce_frames_++;
            if (atlas_rebuild_debounce_frames_ >= 4) {
                atlas_dirty_ = true;
                atlas_rebuild_debounce_frames_ = 0;
            }
        }
    }

    if (atlas_dirty_) {
        rebuild_tile_atlas_from_cache(selected);
        atlas_dirty_ = false;
        atlas_rebuild_debounce_frames_ = 0;
    }

    while (tile_texture_cache_.size() > tile_cache_budget_entries_) {
        auto victim_it = tile_texture_cache_.end();
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (auto it = tile_texture_cache_.begin(); it != tile_texture_cache_.end(); ++it) {
            if (it->second.last_used_stamp < oldest) {
                oldest = it->second.last_used_stamp;
                victim_it = it;
            }
        }
        if (victim_it == tile_texture_cache_.end()) break;
        tile_texture_cache_.erase(victim_it);
    }

    bool pending = false;
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        pending = !tile_jobs_pending_.empty() || !tile_ready_queue_.empty();
    }
    if (pending) queue_render();
}

void GLWrpTerrainView::rebuild_object_buffers() {
    make_current();
    if (has_error()) return;

    if (points_vao_) { glDeleteVertexArrays(1, &points_vao_); points_vao_ = 0; }
    if (points_vbo_) { glDeleteBuffers(1, &points_vbo_); points_vbo_ = 0; }
    points_count_ = 0;

    if (object_points_.empty()) return;
    points_count_ = static_cast<int>(object_points_.size() / 6);

    glGenVertexArrays(1, &points_vao_);
    glGenBuffers(1, &points_vbo_);
    glBindVertexArray(points_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, points_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(object_points_.size() * sizeof(float)),
                 object_points_.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
}

void GLWrpTerrainView::clear_selected_object_render() {
    const bool can_delete = get_realized();
    if (can_delete) {
        make_current();
    }
    if (can_delete && !has_error()) {
        for (auto& lod : selected_object_.lod_meshes) {
            if (lod.vao) glDeleteVertexArrays(1, &lod.vao);
            if (lod.vbo) glDeleteBuffers(1, &lod.vbo);
            lod.vao = 0;
            lod.vbo = 0;
            lod.vertex_count = 0;
        }
    }
    selected_object_ = SelectedObjectRender{};
}

bool GLWrpTerrainView::is_renderable_object_lod(const armatools::p3d::LOD& lod) {
    if (lod.face_data.empty() || lod.vertices.empty()) return false;
    if (is_visual_resolution_name(lod.resolution_name)) return true;
    return lod.resolution >= 0.0f && lod.resolution < 10000.0f;
}

int GLWrpTerrainView::choose_selected_object_lod(const float* eye) {
    if (!selected_object_.valid || selected_object_.lod_meshes.empty()) return 0;
    const float dx = selected_object_.offset[0] - eye[0];
    const float dy = selected_object_.offset[1] - eye[1];
    const float dz = selected_object_.offset[2] - eye[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int max_lod = static_cast<int>(selected_object_.lod_meshes.size()) - 1;
    int lod = std::clamp(selected_object_.current_lod, 0, max_lod);
    while (lod < max_lod) {
        const float bound = selected_object_.lod_base_distance * std::pow(2.0f, static_cast<float>(lod));
        if (dist <= bound * 1.18f) break;
        ++lod;
    }
    while (lod > 0) {
        const float prev_bound =
            selected_object_.lod_base_distance * std::pow(2.0f, static_cast<float>(lod - 1));
        if (dist >= prev_bound * 0.82f) break;
        --lod;
    }
    selected_object_.current_lod = lod;
    return lod;
}

bool GLWrpTerrainView::build_selected_object_render(
    size_t object_index, const armatools::p3d::P3DFile& model) {
    if (object_index >= objects_.size() || !get_realized()) return false;

    make_current();
    if (has_error()) return false;
    clear_selected_object_render();

    std::vector<const armatools::p3d::LOD*> render_lods;
    render_lods.reserve(model.lods.size());
    for (const auto& lod : model.lods) {
        if (is_renderable_object_lod(lod)) render_lods.push_back(&lod);
    }
    if (render_lods.empty()) return false;

    std::sort(render_lods.begin(), render_lods.end(),
              [](const armatools::p3d::LOD* a, const armatools::p3d::LOD* b) {
                  return a->resolution < b->resolution;
              });
    if (render_lods.size() > 6) render_lods.resize(6);

    const auto& obj = objects_[object_index];
    const float scale = (std::isfinite(obj.scale) && obj.scale > 0.0)
        ? static_cast<float>(obj.scale)
        : 1.0f;
    float model_radius = 0.0f;

    for (const auto* lod : render_lods) {
        std::vector<float> verts;
        verts.reserve(static_cast<size_t>(lod->face_data.size()) * 18u);
        for (const auto& face : lod->face_data) {
            if (face.vertices.size() < 3) continue;
            for (size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                const size_t tri[3] = {0, i, i + 1};
                float tri_pos[3][3] = {};
                float tri_nrm[3][3] = {};
                bool has_vertex_normals = true;
                for (int t = 0; t < 3; ++t) {
                    const auto& fv = face.vertices[tri[t]];
                    if (fv.point_index >= lod->vertices.size()) {
                        tri_pos[t][0] = 0.0f;
                        tri_pos[t][1] = 0.0f;
                        tri_pos[t][2] = 0.0f;
                    } else {
                        const auto& p = lod->vertices[fv.point_index];
                        tri_pos[t][0] = -p[0] * scale;
                        tri_pos[t][1] = p[1] * scale;
                        tri_pos[t][2] = p[2] * scale;
                    }

                    if (fv.normal_index >= 0
                        && static_cast<size_t>(fv.normal_index) < lod->normals.size()) {
                        const auto& n = lod->normals[static_cast<size_t>(fv.normal_index)];
                        tri_nrm[t][0] = -n[0];
                        tri_nrm[t][1] = n[1];
                        tri_nrm[t][2] = n[2];
                        vec3_normalize(tri_nrm[t]);
                    } else {
                        has_vertex_normals = false;
                    }
                }

                if (!has_vertex_normals) {
                    float e1[3] = {
                        tri_pos[1][0] - tri_pos[0][0],
                        tri_pos[1][1] - tri_pos[0][1],
                        tri_pos[1][2] - tri_pos[0][2]};
                    float e2[3] = {
                        tri_pos[2][0] - tri_pos[0][0],
                        tri_pos[2][1] - tri_pos[0][1],
                        tri_pos[2][2] - tri_pos[0][2]};
                    float fn[3];
                    vec3_cross(fn, e1, e2);
                    vec3_normalize(fn);
                    if (!std::isfinite(fn[0]) || !std::isfinite(fn[1]) || !std::isfinite(fn[2])) {
                        fn[0] = 0.0f; fn[1] = 1.0f; fn[2] = 0.0f;
                    }
                    for (int t = 0; t < 3; ++t) {
                        tri_nrm[t][0] = fn[0];
                        tri_nrm[t][1] = fn[1];
                        tri_nrm[t][2] = fn[2];
                    }
                }

                for (int t = 0; t < 3; ++t) {
                    verts.push_back(tri_pos[t][0]);
                    verts.push_back(tri_pos[t][1]);
                    verts.push_back(tri_pos[t][2]);
                    verts.push_back(tri_nrm[t][0]);
                    verts.push_back(tri_nrm[t][1]);
                    verts.push_back(tri_nrm[t][2]);
                }
            }
        }

        if (verts.empty()) continue;

        SelectedObjectLodMesh out;
        out.vertex_count = static_cast<int>(verts.size() / 6u);
        out.resolution = lod->resolution;
        glGenVertexArrays(1, &out.vao);
        glGenBuffers(1, &out.vbo);
        glBindVertexArray(out.vao);
        glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
        selected_object_.lod_meshes.push_back(out);

        float lod_radius = lod->bounding_radius * scale;
        if (lod_radius <= 0.001f) {
            const float dx = (lod->bounding_box_max[0] - lod->bounding_box_min[0]) * scale;
            const float dy = (lod->bounding_box_max[1] - lod->bounding_box_min[1]) * scale;
            const float dz = (lod->bounding_box_max[2] - lod->bounding_box_min[2]) * scale;
            lod_radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        model_radius = std::max(model_radius, lod_radius);
    }

    if (selected_object_.lod_meshes.empty()) {
        clear_selected_object_render();
        return false;
    }

    selected_object_.valid = true;
    selected_object_.object_index = object_index;
    selected_object_.model_name = obj.model_name;
    selected_object_.offset[0] = static_cast<float>(obj.position[0]);
    selected_object_.offset[1] = static_cast<float>(obj.position[1]);
    selected_object_.offset[2] = flip_terrain_z_
        ? (terrain_max_z_ - static_cast<float>(obj.position[2]))
        : static_cast<float>(obj.position[2]);
    selected_object_.current_lod = 0;
    selected_object_.lod_base_distance = std::max(40.0f, std::max(4.0f, model_radius * 2.0f) * 22.0f);

    const auto cat = armatools::objcat::category(obj.model_name);
    if (cat == "vegetation") selected_object_.color = {0.20f, 0.72f, 0.24f};
    else if (cat == "buildings") selected_object_.color = {0.88f, 0.33f, 0.30f};
    else if (cat == "rocks") selected_object_.color = {0.65f, 0.65f, 0.68f};
    else if (cat == "walls") selected_object_.color = {0.78f, 0.70f, 0.58f};
    else if (cat == "military") selected_object_.color = {0.74f, 0.75f, 0.40f};
    else if (cat == "infrastructure") selected_object_.color = {0.48f, 0.48f, 0.48f};
    else selected_object_.color = {0.94f, 0.82f, 0.26f};

    return true;
}

void GLWrpTerrainView::build_mvp(float* mvp) const {
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    camera_controller_.build_eye_center(eye, center);

    float view[16];
    float up[3] = {0.0f, 1.0f, 0.0f};
    mat4_look_at(view, eye, center, up);

    const int w = get_width();
    const int h = get_height();
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    float proj[16];
    mat4_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 1.0f, 500000.0f);
    mat4_multiply(mvp, proj, view);
}

void GLWrpTerrainView::emit_terrain_stats() {
    if (!on_terrain_stats_) return;
    size_t pending_jobs = 0;
    size_t ready_jobs = 0;
    {
        std::lock_guard<std::mutex> lock(tile_jobs_mutex_);
        pending_jobs = tile_jobs_pending_.size();
        ready_jobs = tile_ready_queue_.size();
    }
    std::ostringstream ss;
    ss << "Patches " << visible_patch_count_ << "/" << terrain_patches_.size()
       << " | Draws " << terrain_draw_calls_
       << " | Tiles " << visible_tile_count_
       << " | Jobs " << pending_jobs << "/" << ready_jobs
       << " | Cache H/M " << texture_cache_hits_ << "/" << texture_cache_misses_
       << " | Atlas textures " << last_loaded_texture_count_;
    if (selected_object_.valid) {
        ss << " | SelLOD " << (selected_object_.current_lod + 1)
           << "/" << selected_object_.lod_meshes.size();
    }
    const auto next = ss.str();
    if (next != last_terrain_stats_) {
        last_terrain_stats_ = next;
        on_terrain_stats_(next);
    }
}

void GLWrpTerrainView::pick_object_at(double x, double y) {
    if (object_positions_.empty()) return;

    float mvp[16];
    build_mvp(mvp);
    const int w = get_width();
    const int h = get_height();
    if (w <= 0 || h <= 0) return;

    size_t best_idx = static_cast<size_t>(-1);
    double best_d2 = 1e30;
    for (size_t i = 0; i + 2 < object_positions_.size(); i += 3) {
        const float px = object_positions_[i + 0];
        const float py = object_positions_[i + 1];
        const float pz = object_positions_[i + 2];

        const float cx = mvp[0] * px + mvp[4] * py + mvp[8] * pz + mvp[12];
        const float cy = mvp[1] * px + mvp[5] * py + mvp[9] * pz + mvp[13];
        const float cz = mvp[2] * px + mvp[6] * py + mvp[10] * pz + mvp[14];
        const float cw = mvp[3] * px + mvp[7] * py + mvp[11] * pz + mvp[15];
        if (cw <= 0.0001f) continue;

        const float ndc_x = cx / cw;
        const float ndc_y = cy / cw;
        const float ndc_z = cz / cw;
        if (ndc_z < -1.0f || ndc_z > 1.0f) continue;

        const double sx = (static_cast<double>(ndc_x) * 0.5 + 0.5) * static_cast<double>(w);
        const double sy = (1.0 - (static_cast<double>(ndc_y) * 0.5 + 0.5)) * static_cast<double>(h);
        const double dx = sx - x;
        const double dy = sy - y;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best_idx = i / 3;
        }
    }

    if (best_idx != static_cast<size_t>(-1) && best_d2 <= 144.0) {
        bool selected_built = false;
        if (model_loader_ && best_idx < objects_.size()) {
            try {
                auto model = model_loader_->load_p3d(objects_[best_idx].model_name);
                selected_built = build_selected_object_render(best_idx, model);
            } catch (const std::exception& e) {
                app_log(LogLevel::Warning,
                        "GLWrpTerrainView: selected object model load failed: "
                        + objects_[best_idx].model_name + " | " + e.what());
            } catch (...) {
                app_log(LogLevel::Warning,
                        "GLWrpTerrainView: selected object model load failed: "
                        + objects_[best_idx].model_name);
            }
        }
        if (!selected_built) clear_selected_object_render();
        if (on_object_picked_) on_object_picked_(best_idx);
        queue_render();
    }
}

void GLWrpTerrainView::move_camera_local(float forward, float right) {
    camera_controller_.move_local(forward, right, 0.0f);
    queue_render();
}

bool GLWrpTerrainView::movement_tick() {
    float forward = 0.0f;
    float right = 0.0f;
    float vertical = 0.0f;
    if (move_fwd_) forward += 1.0f;
    if (move_back_) forward -= 1.0f;
    if (move_right_) right += 1.0f;
    if (move_left_) right -= 1.0f;
    if (move_up_) vertical += 1.0f;
    if (move_down_) vertical -= 1.0f;
    if (forward == 0.0f && right == 0.0f && vertical == 0.0f) return false;

    float step = std::max(0.5f, camera_controller_.distance() * 0.006f);
    if (move_fast_ && !alt_pressed_) step *= 3.0f;
    camera_controller_.move_local(forward * step, right * step, vertical * step);
    queue_render();
    return true;
}
