#include "gl_wrp_terrain_view.h"

#include "lod_textures_loader.h"
#include "log_panel.h"
#include <armatools/objcat.h>

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace {

static constexpr const char* TERRAIN_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aHeight;
layout(location=2) in float aMask;
layout(location=3) in vec3 aSat;
uniform mat4 uMVP;
out float vHeight;
out float vMask;
out vec3 vSat;
out vec2 vWorldXZ;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vHeight = aHeight;
    vMask = aMask;
    vSat = aSat;
    vWorldXZ = vec2(aPos.x, aPos.z);
}
)";

static constexpr const char* TERRAIN_FRAG = R"(
#version 330 core
in float vHeight;
in float vMask;
in vec3 vSat;
in vec2 vWorldXZ;
uniform float uMinH;
uniform float uMaxH;
uniform int uMode;
uniform sampler2D uTextureAtlas;
uniform sampler2D uTextureLookup;
uniform sampler2D uTextureIndex;
uniform int uTextureLookupSize;
uniform float uTextureWorldScale;
uniform float uTextureCellSize;
uniform int uTextureGridW;
uniform int uTextureGridH;
uniform bool uHasTextureAtlas;
uniform bool uHasTextureLookup;
uniform bool uHasTextureIndex;
uniform vec2 uCameraXZ;
uniform float uNearTextureDistance;
out vec4 FragColor;
vec3 hash_color(float n) {
    uint h = uint(max(n, 0.0));
    h ^= (h >> 16);
    h *= 0x7feb352du;
    h ^= (h >> 15);
    h *= 0x846ca68bu;
    h ^= (h >> 16);
    float r = float((h >> 0) & 255u) / 255.0;
    float g = float((h >> 8) & 255u) / 255.0;
    float b = float((h >> 16) & 255u) / 255.0;
    return vec3(0.20 + 0.75 * r, 0.20 + 0.75 * g, 0.20 + 0.75 * b);
}
void main() {
    vec3 c;
    if (uMode == 3) {
        c = vSat;
    } else if (uMode == 2) {
        vec3 tex_color = vec3(0.0);
        bool has_texture = false;
        int desired = -1;
        float camera_dist = distance(vWorldXZ, uCameraXZ);
        if (uHasTextureIndex && uTextureGridW > 0 && uTextureGridH > 0) {
            float cell = max(uTextureCellSize, 0.0001);
            int gx = int(floor(vWorldXZ.x / cell));
            int gz = int(floor(vWorldXZ.y / cell));
            gx = clamp(gx, 0, uTextureGridW - 1);
            gz = clamp(gz, 0, uTextureGridH - 1);
            desired = int(floor(texelFetch(uTextureIndex, ivec2(gx, gz), 0).r + 0.5));
        }
        if (camera_dist <= uNearTextureDistance
            && uHasTextureAtlas && uHasTextureLookup && uTextureLookupSize > 0) {
            if (desired >= 0 && desired < uTextureLookupSize) {
                vec4 slot = texelFetch(uTextureLookup, ivec2(desired, 0), 0);
                if (slot.z > 0.0 && slot.w > 0.0) {
                    vec2 world_uv = vWorldXZ / max(uTextureWorldScale, 0.0001);
                    vec2 tile_uv = fract(world_uv);
                    vec2 atlas_uv = slot.xy + tile_uv * slot.zw;
                    tex_color = texture(uTextureAtlas, atlas_uv).rgb;
                    has_texture = true;
                }
            }
        }
        if (has_texture) {
            c = tex_color;
        } else if (desired >= 0 && desired < 65535) {
            c = vSat;
        } else {
            if (desired < 0) c = vec3(0.35, 0.0, 0.35);
            else c = hash_color(float(desired + 1));
        }
    } else if (uMode == 1) {
        int cls = int(vMask + 0.5);
        if (cls == 1) c = vec3(0.70, 0.60, 0.35);          // tidal
        else if (cls == 2) c = vec3(0.92, 0.86, 0.55);     // coastline
        else if (cls == 3) c = vec3(0.16, 0.38, 0.72);     // sea
        else if (cls == 4) c = vec3(0.12, 0.46, 0.14);     // forest
        else if (cls == 5) c = vec3(0.25, 0.25, 0.25);     // roadway
        else c = vec3(0.45, 0.36, 0.22);                   // ground
    } else {
        float denom = max(0.001, uMaxH - uMinH);
        float t = clamp((vHeight - uMinH) / denom, 0.0, 1.0);
        vec3 low = vec3(0.10, 0.35, 0.12);
        vec3 mid = vec3(0.55, 0.45, 0.25);
        vec3 high = vec3(0.90, 0.90, 0.88);
        c = t < 0.5 ? mix(low, mid, t * 2.0) : mix(mid, high, (t - 0.5) * 2.0);
    }
    FragColor = vec4(c, 1.0);
}
)";

static constexpr const char* POINT_VERT = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    gl_PointSize = 4.0;
    vColor = aColor;
}
)";

static constexpr const char* POINT_FRAG = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

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
        drag_start_azimuth_ = azimuth_;
        drag_start_elevation_ = elevation_;
    });
    drag_orbit_->signal_drag_update().connect([this](double dx, double dy) {
        azimuth_ = drag_start_azimuth_ - static_cast<float>(dx) * 0.008f;
        elevation_ = std::clamp(drag_start_elevation_ - static_cast<float>(dy) * 0.008f,
                                -1.57f, 1.57f);
        queue_render();
    });
    add_controller(drag_orbit_);

    drag_pan_ = Gtk::GestureDrag::create();
    drag_pan_->set_button(GDK_BUTTON_MIDDLE);
    drag_pan_->signal_drag_begin().connect([this](double, double) {
        std::memcpy(drag_start_pivot_, pivot_, sizeof(pivot_));
    });
    drag_pan_->signal_drag_update().connect([this](double dx, double dy) {
        const float scale = 0.5f;
        const float ca = std::cos(azimuth_);
        const float sa = std::sin(azimuth_);
        const float rx = ca;
        const float rz = -sa;
        pivot_[0] = drag_start_pivot_[0] - static_cast<float>(dx) * scale * rx;
        pivot_[2] = drag_start_pivot_[2] - static_cast<float>(dx) * scale * rz;
        pivot_[1] = drag_start_pivot_[1] + static_cast<float>(dy) * scale;
        queue_render();
    });
    add_controller(drag_pan_);

    scroll_zoom_ = Gtk::EventControllerScroll::create();
    scroll_zoom_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_zoom_->signal_scroll().connect([this](double, double dy) -> bool {
        distance_ *= (dy > 0.0) ? 0.9f : 1.1f;
        distance_ = std::clamp(distance_, 0.1f, 10.0f);
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
}

GLWrpTerrainView::~GLWrpTerrainView() = default;

void GLWrpTerrainView::clear_world() {
    texture_entries_.clear();
    texture_atlas_pixels_.clear();
    texture_lookup_uvs_.clear();
    texture_lookup_size_ = 0;
    texture_index_tex_w_ = 0;
    texture_index_tex_h_ = 0;
    texture_world_scale_ = 32.0f;
    has_texture_atlas_ = false;
    has_texture_lookup_ = false;
    has_texture_index_ = false;
    cleanup_texture_atlas_gl();
    cleanup_texture_lookup_gl();
    cleanup_texture_index_gl();
    if (texture_rebuild_idle_) texture_rebuild_idle_.disconnect();

    heights_.clear();
    surface_classes_.clear();
    texture_indices_.clear();
    satellite_palette_.clear();
    grid_w_ = 0;
    grid_h_ = 0;
    object_points_.clear();
    object_positions_.clear();
    min_elevation_ = 0.0f;
    max_elevation_ = 1.0f;
    texture_index_max_ = 1.0f;
    if (get_realized()) {
        rebuild_terrain_buffers();
        rebuild_object_buffers();
    }
    queue_render();
}

void GLWrpTerrainView::set_world_data(const armatools::wrp::WorldData& world) {
    const int src_w = world.grid.terrain_x;
    const int src_h = world.grid.terrain_y;
    if (src_w <= 1 || src_h <= 1 || world.elevations.empty()) {
        clear_world();
        return;
    }

    // Downsample large terrains for interactive preview.
    static constexpr int kMaxGrid = 512;
    const int step = std::max({1, (src_w + kMaxGrid - 1) / kMaxGrid, (src_h + kMaxGrid - 1) / kMaxGrid});
    grid_w_ = std::max(2, (src_w + step - 1) / step);
    grid_h_ = std::max(2, (src_h + step - 1) / step);
    cell_size_ = static_cast<float>(world.grid.cell_size * static_cast<double>(step));

    heights_.resize(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_));
    surface_classes_.resize(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_), 0.0f);
    texture_indices_.resize(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_), 0.0f);
    texture_index_max_ = 1.0f;
    float texture_index_min = std::numeric_limits<float>::max();
    size_t texture_nonzero = 0;
    size_t surface_nonzero = 0;
    min_elevation_ = std::numeric_limits<float>::max();
    max_elevation_ = std::numeric_limits<float>::lowest();

    enum class FormatFamily { OprwModern, OprwLegacy, Wvr4, Wvr1, Unknown };
    FormatFamily family = FormatFamily::Unknown;
    auto normalize_sig = [](std::string sig) {
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.pop_back();
        };
        trim(sig);
        for (auto& c : sig) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return sig;
    };
    const std::string sig = normalize_sig(world.format.signature);
    if (sig == "OPRW") {
        family = (world.format.version >= 20) ? FormatFamily::OprwModern
                                              : FormatFamily::OprwLegacy;
    } else if (sig == "4WVR") {
        family = FormatFamily::Wvr4;
    } else if (sig == "1WVR") {
        family = FormatFamily::Wvr1;
    }

    const int land_w = std::max(world.grid.cells_x, 0);
    const int land_h = std::max(world.grid.cells_y, 0);
    const int terr_w = std::max(world.grid.terrain_x, 0);
    const int terr_h = std::max(world.grid.terrain_y, 0);
    const bool has_flags = land_w > 0 && land_h > 0 && !world.cell_bit_flags.empty();
    const bool has_textures_land = land_w > 0 && land_h > 0 && !world.cell_texture_indexes.empty();
    const bool has_textures_terrain = terr_w > 0 && terr_h > 0 && !world.cell_texture_indexes.empty();

    auto map_to_grid = [](int x, int z, int in_w, int in_h, int out_w, int out_h)
        -> std::pair<int, int> {
        const double ux = (in_w > 1) ? (static_cast<double>(x) / static_cast<double>(in_w - 1)) : 0.0;
        const double uz = (in_h > 1) ? (static_cast<double>(z) / static_cast<double>(in_h - 1)) : 0.0;
        const int ox = std::clamp(static_cast<int>(ux * static_cast<double>(std::max(1, out_w - 1))), 0, std::max(0, out_w - 1));
        const int oz = std::clamp(static_cast<int>(uz * static_cast<double>(std::max(1, out_h - 1))), 0, std::max(0, out_h - 1));
        return {ox, oz};
    };

    for (int z = 0; z < grid_h_; ++z) {
        const int src_z = std::min(z * step, src_h - 1);
        for (int x = 0; x < grid_w_; ++x) {
            const int src_x = std::min(x * step, src_w - 1);
            const size_t src_idx = static_cast<size_t>(src_z) * static_cast<size_t>(src_w)
                                 + static_cast<size_t>(src_x);
            const float h = src_idx < world.elevations.size() ? world.elevations[src_idx] : 0.0f;
            const size_t dst_idx = static_cast<size_t>(z) * static_cast<size_t>(grid_w_) + static_cast<size_t>(x);
            heights_[dst_idx] = h;
            min_elevation_ = std::min(min_elevation_, h);
            max_elevation_ = std::max(max_elevation_, h);

            float cls = 0.0f;
            float tex_idx = 0.0f;
            switch (family) {
            case FormatFamily::OprwModern:
            case FormatFamily::OprwLegacy: {
                if (has_flags) {
                    const auto [fx, fz] = map_to_grid(x, z, grid_w_, grid_h_, land_w, land_h);
                    const size_t fidx = static_cast<size_t>(fz) * static_cast<size_t>(land_w)
                                      + static_cast<size_t>(fx);
                    if (fidx < world.cell_bit_flags.size()) {
                        const uint32_t f = world.cell_bit_flags[fidx];
                        if (f & 0x40) cls = 5.0f;          // roadway
                        else if (f & 0x20) cls = 4.0f;     // forest
                        else cls = static_cast<float>(f & 0x03); // surface class
                    }
                }
                if (has_textures_land) {
                    const auto [tx, tz] = map_to_grid(x, z, grid_w_, grid_h_, land_w, land_h);
                    const size_t tidx = static_cast<size_t>(tz) * static_cast<size_t>(land_w)
                                      + static_cast<size_t>(tx);
                    if (tidx < world.cell_texture_indexes.size())
                        tex_idx = static_cast<float>(world.cell_texture_indexes[tidx]);
                }
                break;
            }
            case FormatFamily::Wvr4:
            case FormatFamily::Wvr1: {
                // OFP-era WRP variants keep texture index grid aligned to terrain/cell grid.
                if (has_textures_terrain) {
                    const auto [tx, tz] = map_to_grid(x, z, grid_w_, grid_h_, terr_w, terr_h);
                    const size_t tidx = static_cast<size_t>(tz) * static_cast<size_t>(terr_w)
                                      + static_cast<size_t>(tx);
                    if (tidx < world.cell_texture_indexes.size())
                        tex_idx = static_cast<float>(world.cell_texture_indexes[tidx]);
                }
                cls = 0.0f;
                break;
            }
            case FormatFamily::Unknown:
            default: {
                if (has_flags) {
                    const auto [fx, fz] = map_to_grid(x, z, grid_w_, grid_h_, land_w, land_h);
                    const size_t fidx = static_cast<size_t>(fz) * static_cast<size_t>(land_w)
                                      + static_cast<size_t>(fx);
                    if (fidx < world.cell_bit_flags.size())
                        cls = static_cast<float>(world.cell_bit_flags[fidx] & 0x03);
                }
                if (has_textures_land) {
                    const auto [tx, tz] = map_to_grid(x, z, grid_w_, grid_h_, land_w, land_h);
                    const size_t tidx = static_cast<size_t>(tz) * static_cast<size_t>(land_w)
                                      + static_cast<size_t>(tx);
                    if (tidx < world.cell_texture_indexes.size())
                        tex_idx = static_cast<float>(world.cell_texture_indexes[tidx]);
                }
                break;
            }
            }
            surface_classes_[dst_idx] = cls;
            texture_indices_[dst_idx] = tex_idx;
            if (cls != 0.0f) surface_nonzero++;
            texture_index_min = std::min(texture_index_min, tex_idx);
            texture_index_max_ = std::max(texture_index_max_, tex_idx);
            if (tex_idx > 0.0f) texture_nonzero++;
        }
    }
    if (max_elevation_ <= min_elevation_)
        max_elevation_ = min_elevation_ + 1.0f;

    if (texture_index_min == std::numeric_limits<float>::max())
        texture_index_min = 0.0f;
    std::string mode_handler = "unknown";
    switch (family) {
    case FormatFamily::OprwModern: mode_handler = "oprw-modern"; break;
    case FormatFamily::OprwLegacy: mode_handler = "oprw-legacy"; break;
    case FormatFamily::Wvr4: mode_handler = "4wvr"; break;
    case FormatFamily::Wvr1: mode_handler = "1wvr"; break;
    case FormatFamily::Unknown: break;
    }
    app_log(LogLevel::Debug,
            "GLWrpTerrainView: handler=" + mode_handler
            + " texture indices min=" + std::to_string(texture_index_min)
            + " max=" + std::to_string(texture_index_max_)
            + " nonzero=" + std::to_string(texture_nonzero)
            + " surface_nonzero=" + std::to_string(surface_nonzero)
            + " verts=" + std::to_string(heights_.size()));

    texture_entries_ = world.textures;
    if (color_mode_ == 2 && !texture_entries_.empty() && !texture_indices_.empty())
        schedule_texture_rebuild();

    set_objects(world.objects);

    // Camera pivot at terrain center.
    const float world_w = static_cast<float>(grid_w_ - 1) * cell_size_;
    const float world_h = static_cast<float>(grid_h_ - 1) * cell_size_;
    pivot_[0] = world_w * 0.5f;
    pivot_[2] = world_h * 0.5f;
    pivot_[1] = (min_elevation_ + max_elevation_) * 0.5f;
    const float radius = std::max(world_w, world_h) * 0.75f;
    distance_ = std::max(radius, 50.0f);
    azimuth_ = 0.65f;
    elevation_ = 0.85f;

    if (get_realized()) {
        rebuild_terrain_buffers();
        rebuild_object_buffers();
    }
    queue_render();
}

void GLWrpTerrainView::set_objects(const std::vector<armatools::wrp::ObjectRecord>& objects) {
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
        const float pz = static_cast<float>(obj.position[2]);
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

void GLWrpTerrainView::set_color_mode(int mode) {
    color_mode_ = std::clamp(mode, 0, 3);
    const char* mode_name = "elevation";
    if (color_mode_ == 1) mode_name = "surface";
    else if (color_mode_ == 2) mode_name = "texture";
    else if (color_mode_ == 3) mode_name = "satellite";

    std::ostringstream ss;
    ss << "GLWrpTerrainView: color mode -> " << color_mode_ << " (" << mode_name << ")"
       << " grid=" << grid_w_ << "x" << grid_h_
       << " heights=" << heights_.size()
       << " surface=" << surface_classes_.size()
       << " texture=" << texture_indices_.size()
       << " texMax=" << texture_index_max_
       << " satPalette=" << satellite_palette_.size();

    if (!heights_.empty()) {
        const int cx = std::clamp(grid_w_ / 2, 0, std::max(0, grid_w_ - 1));
        const int cz = std::clamp(grid_h_ / 2, 0, std::max(0, grid_h_ - 1));
        const size_t cidx = static_cast<size_t>(cz) * static_cast<size_t>(grid_w_)
                          + static_cast<size_t>(cx);
        if (cidx < heights_.size()) {
            const float h = heights_[cidx];
            const float m = cidx < surface_classes_.size() ? surface_classes_[cidx] : 0.0f;
            const float t = cidx < texture_indices_.size() ? texture_indices_[cidx] : 0.0f;
            ss << " sample[c=" << cx << "," << cz << "]: h=" << h
               << " mask=" << m << " tex=" << t;
            const int ti = static_cast<int>(std::floor(t + 0.5f));
            if (ti >= 0 && static_cast<size_t>(ti) < satellite_palette_.size()) {
                const auto& rgb = satellite_palette_[static_cast<size_t>(ti)];
                ss << " satRGB=[" << rgb[0] << "," << rgb[1] << "," << rgb[2] << "]";
            } else if (color_mode_ == 3) {
                ss << " satRGB=[missing for texIdx=" << ti << "]";
            }
        }
    }

    if (color_mode_ == 3 && satellite_palette_.empty())
        app_log(LogLevel::Warning, "GLWrpTerrainView: satellite mode selected but palette is empty");
    if (color_mode_ == 2 && !texture_entries_.empty() && !texture_indices_.empty())
        schedule_texture_rebuild();
    app_log(LogLevel::Debug, ss.str());
    queue_render();
}

void GLWrpTerrainView::set_satellite_palette(const std::vector<std::array<float, 3>>& palette) {
    satellite_palette_ = palette;
    std::ostringstream ss;
    ss << "GLWrpTerrainView: satellite palette updated size=" << satellite_palette_.size();
    if (!satellite_palette_.empty()) {
        const auto& c0 = satellite_palette_[0];
        ss << " first=[" << c0[0] << "," << c0[1] << "," << c0[2] << "]";
    }
    app_log(LogLevel::Debug, ss.str());
    if (get_realized()) rebuild_terrain_buffers();
    queue_render();
}

void GLWrpTerrainView::schedule_texture_rebuild() {
    if (!texture_loader_) return;
    if (texture_entries_.empty() || texture_indices_.empty()) return;
    if (texture_rebuild_idle_) return;
    texture_rebuild_idle_ = Glib::signal_idle().connect([this]() {
        rebuild_texture_atlas(texture_entries_);
        texture_rebuild_idle_.disconnect();
        return false;
    });
}

void GLWrpTerrainView::set_texture_loader_service(
    const std::shared_ptr<LodTexturesLoaderService>& service) {
    texture_loader_ = service;
    if (!texture_loader_) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        cleanup_texture_index_gl();
        return;
    }
    if (color_mode_ == 2 && !texture_entries_.empty() && !texture_indices_.empty())
        schedule_texture_rebuild();
}

void GLWrpTerrainView::rebuild_texture_atlas(const std::vector<armatools::wrp::TextureEntry>& textures) {
    texture_lookup_uvs_.assign(textures.size(), {0.0f, 0.0f, 0.0f, 0.0f});
    texture_lookup_size_ = static_cast<int>(texture_lookup_uvs_.size());
    has_texture_lookup_ = false;
    texture_atlas_pixels_.clear();
    atlas_width_ = atlas_height_ = 0;
    has_texture_atlas_ = false;
    has_texture_index_ = false;
    texture_index_tex_w_ = grid_w_;
    texture_index_tex_h_ = grid_h_;

    if (!texture_loader_ || textures.empty() || texture_indices_.empty()) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        cleanup_texture_index_gl();
        return;
    }

    std::unordered_map<int, int> index_freq;
    for (float value : texture_indices_) {
        const int ti = static_cast<int>(std::floor(value + 0.5f));
        if (ti < 0 || ti >= static_cast<int>(textures.size())) continue;
        index_freq[ti] += 1;
    }
    if (index_freq.empty()) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        cleanup_texture_index_gl();
        return;
    }
    std::vector<std::pair<int, int>> ranked;
    ranked.reserve(index_freq.size());
    for (const auto& kv : index_freq) ranked.push_back(kv);
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
              });
    static constexpr size_t kMaxAtlasTextures = 256;
    if (ranked.size() > kMaxAtlasTextures) ranked.resize(kMaxAtlasTextures);

    struct AtlasEntry {
        int index = -1;
        LodTexturesLoaderService::TextureData data;
    };
    std::vector<AtlasEntry> entries;
    entries.reserve(ranked.size());
    int resolved_paa_pac = 0;
    int resolved_rvmat = 0;
    int missing = 0;
    for (const auto& it : ranked) {
        const int idx = it.first;
        if (idx < 0 || idx >= static_cast<int>(textures.size())) continue;
        const auto& texture = textures[static_cast<std::size_t>(idx)];
        if (texture.filename.empty()) {
            missing++;
            continue;
        }
        if (auto data = texture_loader_->load_terrain_texture_entry(texture.filename)) {
            if (data->resolved_from_material) resolved_rvmat++;
            else resolved_paa_pac++;
            entries.push_back(AtlasEntry{idx, std::move(*data)});
        } else {
            missing++;
        }
    }
    if (entries.empty()) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        cleanup_texture_index_gl();
        return;
    }

    static constexpr int kAtlasPadding = 2;
    const int max_row_width = 4096;
    struct Placement { int x = 0; int y = 0; };
    std::vector<Placement> placements;
    placements.reserve(entries.size());
    int x = 0;
    int y = 0;
    int row_height = 0;
    int row_width_max = 0;
    for (const auto& entry : entries) {
        const int w = entry.data.header.width;
        const int h = entry.data.header.height;
        if (w <= 0 || h <= 0) continue;
        const int packed_w = w + 2 * kAtlasPadding;
        const int packed_h = h + 2 * kAtlasPadding;
        if (x > 0 && (x + packed_w > max_row_width)) {
            row_width_max = std::max(row_width_max, x);
            x = 0;
            y += row_height;
            row_height = 0;
        }
        placements.push_back(Placement{x, y});
        x += packed_w;
        row_height = std::max(row_height, packed_h);
        row_width_max = std::max(row_width_max, x);
    }
    row_width_max = std::max(row_width_max, x);
    const int atlas_w = std::max(row_width_max, 1);
    const int atlas_h = y + row_height;
    if (atlas_h <= 0) {
        cleanup_texture_atlas_gl();
        cleanup_texture_lookup_gl();
        cleanup_texture_index_gl();
        return;
    }

    texture_atlas_pixels_.assign(static_cast<size_t>(atlas_w) * static_cast<size_t>(atlas_h) * 4u, 0);
    int filled_entries = 0;
    for (size_t i = 0; i < entries.size() && i < placements.size(); ++i) {
        const auto& entry = entries[i];
        const int w = entry.data.header.width;
        const int h = entry.data.header.height;
        if (w <= 0 || h <= 0) continue;
        const int px = placements[i].x;
        const int py = placements[i].y;
        const int dst_x = px + kAtlasPadding;
        const int dst_y = py + kAtlasPadding;
        for (int row = 0; row < h; ++row) {
            const size_t dst = static_cast<size_t>((dst_y + row) * atlas_w + dst_x) * 4u;
            const auto* src = entry.data.image.pixels.data()
                + static_cast<size_t>(row) * static_cast<size_t>(w) * 4u;
            std::memcpy(texture_atlas_pixels_.data() + dst, src, static_cast<size_t>(w) * 4u);
        }

        for (int row = 0; row < h; ++row) {
            const size_t row_off = static_cast<size_t>(dst_y + row) * static_cast<size_t>(atlas_w);
            const size_t left_src = (row_off + static_cast<size_t>(dst_x)) * 4u;
            const size_t right_src = (row_off + static_cast<size_t>(dst_x + w - 1)) * 4u;
            for (int pad = 1; pad <= kAtlasPadding; ++pad) {
                std::memcpy(texture_atlas_pixels_.data() + (left_src - static_cast<size_t>(pad) * 4u),
                            texture_atlas_pixels_.data() + left_src, 4u);
                std::memcpy(texture_atlas_pixels_.data() + (right_src + static_cast<size_t>(pad) * 4u),
                            texture_atlas_pixels_.data() + right_src, 4u);
            }
        }
        for (int col = -kAtlasPadding; col < w + kAtlasPadding; ++col) {
            const int sx = dst_x + col;
            const size_t top_src = (static_cast<size_t>(dst_y) * static_cast<size_t>(atlas_w)
                                   + static_cast<size_t>(sx)) * 4u;
            const size_t bot_src = (static_cast<size_t>(dst_y + h - 1) * static_cast<size_t>(atlas_w)
                                   + static_cast<size_t>(sx)) * 4u;
            for (int pad = 1; pad <= kAtlasPadding; ++pad) {
                const size_t top_dst = (static_cast<size_t>(dst_y - pad) * static_cast<size_t>(atlas_w)
                                       + static_cast<size_t>(sx)) * 4u;
                const size_t bot_dst = (static_cast<size_t>(dst_y + h - 1 + pad) * static_cast<size_t>(atlas_w)
                                       + static_cast<size_t>(sx)) * 4u;
                std::memcpy(texture_atlas_pixels_.data() + top_dst, texture_atlas_pixels_.data() + top_src, 4u);
                std::memcpy(texture_atlas_pixels_.data() + bot_dst, texture_atlas_pixels_.data() + bot_src, 4u);
            }
        }

        const float off_x = static_cast<float>(dst_x) / static_cast<float>(atlas_w);
        const float off_y = static_cast<float>(dst_y) / static_cast<float>(atlas_h);
        const float scale_x = static_cast<float>(w) / static_cast<float>(atlas_w);
        const float scale_y = static_cast<float>(h) / static_cast<float>(atlas_h);
        if (entry.index >= 0 && entry.index < static_cast<int>(texture_lookup_uvs_.size())) {
            texture_lookup_uvs_[static_cast<std::size_t>(entry.index)] = {off_x, off_y, scale_x, scale_y};
            has_texture_lookup_ = true;
        }
        filled_entries++;
    }

    atlas_width_ = atlas_w;
    atlas_height_ = atlas_h;
    has_texture_atlas_ = !texture_atlas_pixels_.empty() && filled_entries > 0;
    texture_world_scale_ = std::max(cell_size_, 1.0f) * 8.0f;
    has_texture_index_ = (texture_index_tex_w_ > 0 && texture_index_tex_h_ > 0
                          && texture_indices_.size()
                             >= static_cast<size_t>(texture_index_tex_w_) * static_cast<size_t>(texture_index_tex_h_));
    if (get_realized()) {
        upload_texture_atlas();
        upload_texture_lookup();
        upload_texture_index();
    }

    std::ostringstream ss;
    ss << "GLWrpTerrainView: texture atlas built entries=" << filled_entries
       << " lookup_size=" << texture_lookup_size_
       << " total=" << textures.size()
       << " unique_used=" << index_freq.size()
       << " loaded_used=" << ranked.size()
       << " resolved_paa_pac=" << resolved_paa_pac
       << " resolved_rvmat=" << resolved_rvmat
       << " missing=" << missing;
    app_log(LogLevel::Debug, ss.str());
}

void GLWrpTerrainView::upload_texture_atlas() {
    if (!get_realized() || texture_atlas_pixels_.empty()) return;
    make_current();
    if (has_error()) return;
    if (texture_atlas_) {
        glDeleteTextures(1, &texture_atlas_);
        texture_atlas_ = 0;
    }
    glGenTextures(1, &texture_atlas_);
    glBindTexture(GL_TEXTURE_2D, texture_atlas_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_width_, atlas_height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texture_atlas_pixels_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    has_texture_atlas_ = true;
}

void GLWrpTerrainView::upload_texture_lookup() {
    if (!get_realized() || texture_lookup_uvs_.empty() || texture_lookup_size_ <= 0) return;
    make_current();
    if (has_error()) return;
    if (texture_lookup_tex_) {
        glDeleteTextures(1, &texture_lookup_tex_);
        texture_lookup_tex_ = 0;
    }
    glGenTextures(1, &texture_lookup_tex_);
    glBindTexture(GL_TEXTURE_2D, texture_lookup_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, texture_lookup_size_, 1, 0,
                 GL_RGBA, GL_FLOAT, texture_lookup_uvs_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    has_texture_lookup_ = true;
}

void GLWrpTerrainView::upload_texture_index() {
    if (!get_realized() || texture_indices_.empty() || texture_index_tex_w_ <= 0 || texture_index_tex_h_ <= 0)
        return;
    make_current();
    if (has_error()) return;
    if (texture_index_tex_) {
        glDeleteTextures(1, &texture_index_tex_);
        texture_index_tex_ = 0;
    }
    glGenTextures(1, &texture_index_tex_);
    glBindTexture(GL_TEXTURE_2D, texture_index_tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texture_index_tex_w_, texture_index_tex_h_, 0,
                 GL_RED, GL_FLOAT, texture_indices_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    has_texture_index_ = true;
}

void GLWrpTerrainView::cleanup_texture_atlas_gl() {
    if (texture_atlas_ == 0) return;
    if (!get_realized()) {
        texture_atlas_ = 0;
        return;
    }
    make_current();
    glDeleteTextures(1, &texture_atlas_);
    texture_atlas_ = 0;
    has_texture_atlas_ = false;
}

void GLWrpTerrainView::cleanup_texture_lookup_gl() {
    if (texture_lookup_tex_ == 0) return;
    if (!get_realized()) {
        texture_lookup_tex_ = 0;
        return;
    }
    make_current();
    glDeleteTextures(1, &texture_lookup_tex_);
    texture_lookup_tex_ = 0;
    has_texture_lookup_ = false;
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

void GLWrpTerrainView::set_on_object_picked(std::function<void(size_t)> cb) {
    on_object_picked_ = std::move(cb);
}

void GLWrpTerrainView::set_on_texture_debug_info(std::function<void(const std::string&)> cb) {
    on_texture_debug_info_ = std::move(cb);
}

void GLWrpTerrainView::on_realize_gl() {
    make_current();
    if (has_error()) {
        app_log(LogLevel::Error, "GLWrpTerrainView: GL context creation failed");
        return;
    }

    auto vs = compile_shader(GL_VERTEX_SHADER, TERRAIN_VERT);
    auto fs = compile_shader(GL_FRAGMENT_SHADER, TERRAIN_FRAG);
    prog_terrain_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    auto pvs = compile_shader(GL_VERTEX_SHADER, POINT_VERT);
    auto pfs = compile_shader(GL_FRAGMENT_SHADER, POINT_FRAG);
    prog_points_ = link_program(pvs, pfs);
    glDeleteShader(pvs);
    glDeleteShader(pfs);

    loc_mvp_terrain_ = glGetUniformLocation(prog_terrain_, "uMVP");
    loc_hmin_terrain_ = glGetUniformLocation(prog_terrain_, "uMinH");
    loc_hmax_terrain_ = glGetUniformLocation(prog_terrain_, "uMaxH");
    loc_mode_terrain_ = glGetUniformLocation(prog_terrain_, "uMode");
    loc_texture_atlas_ = glGetUniformLocation(prog_terrain_, "uTextureAtlas");
    loc_texture_lookup_ = glGetUniformLocation(prog_terrain_, "uTextureLookup");
    loc_texture_index_ = glGetUniformLocation(prog_terrain_, "uTextureIndex");
    loc_texture_lookup_size_ = glGetUniformLocation(prog_terrain_, "uTextureLookupSize");
    loc_texture_world_scale_ = glGetUniformLocation(prog_terrain_, "uTextureWorldScale");
    loc_texture_cell_size_ = glGetUniformLocation(prog_terrain_, "uTextureCellSize");
    loc_texture_grid_w_ = glGetUniformLocation(prog_terrain_, "uTextureGridW");
    loc_texture_grid_h_ = glGetUniformLocation(prog_terrain_, "uTextureGridH");
    loc_has_texture_atlas_ = glGetUniformLocation(prog_terrain_, "uHasTextureAtlas");
    loc_has_texture_lookup_ = glGetUniformLocation(prog_terrain_, "uHasTextureLookup");
    loc_has_texture_index_ = glGetUniformLocation(prog_terrain_, "uHasTextureIndex");
    loc_camera_xz_ = glGetUniformLocation(prog_terrain_, "uCameraXZ");
    loc_near_texture_distance_ = glGetUniformLocation(prog_terrain_, "uNearTextureDistance");
    loc_mvp_points_ = glGetUniformLocation(prog_points_, "uMVP");

    glUseProgram(prog_terrain_);
    if (loc_texture_atlas_ >= 0)
        glUniform1i(loc_texture_atlas_, 0);
    if (loc_texture_lookup_ >= 0)
        glUniform1i(loc_texture_lookup_, 1);
    if (loc_texture_index_ >= 0)
        glUniform1i(loc_texture_index_, 2);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    rebuild_terrain_buffers();
    rebuild_object_buffers();
    upload_texture_atlas();
    upload_texture_lookup();
    upload_texture_index();
}

void GLWrpTerrainView::on_unrealize_gl() {
    make_current();
    if (has_error()) return;
    cleanup_gl();
}

bool GLWrpTerrainView::on_render_gl(const Glib::RefPtr<Gdk::GLContext>&) {
    glClearColor(0.14f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!prog_terrain_) return true;

    const float eye[3] = {pivot_[0], pivot_[1] + distance_, pivot_[2]};

    float mvp[16];
    build_mvp(mvp);
    update_visible_terrain_indices(mvp, eye);

    if (terrain_vao_ && terrain_visible_index_count_ > 0) {
        glUseProgram(prog_terrain_);
        glUniformMatrix4fv(loc_mvp_terrain_, 1, GL_FALSE, mvp);
        glUniform1f(loc_hmin_terrain_, min_elevation_);
        glUniform1f(loc_hmax_terrain_, max_elevation_);
        glUniform1i(loc_mode_terrain_, color_mode_);
        if (loc_camera_xz_ >= 0)
            glUniform2f(loc_camera_xz_, eye[0], eye[2]);
        if (loc_near_texture_distance_ >= 0) {
            const float scaled = std::max(near_texture_distance_, cell_size_ * 16.0f);
            glUniform1f(loc_near_texture_distance_, scaled);
        }
        if (loc_has_texture_atlas_ >= 0)
            glUniform1i(loc_has_texture_atlas_, has_texture_atlas_ ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, has_texture_atlas_ ? texture_atlas_ : 0);
        if (loc_texture_lookup_ >= 0) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, has_texture_lookup_ ? texture_lookup_tex_ : 0);
            glUniform1i(loc_texture_lookup_, 1);
            glActiveTexture(GL_TEXTURE0);
        }
        if (loc_texture_lookup_size_ >= 0)
            glUniform1i(loc_texture_lookup_size_, texture_lookup_size_);
        if (loc_has_texture_lookup_ >= 0)
            glUniform1i(loc_has_texture_lookup_, has_texture_lookup_ ? 1 : 0);
        if (loc_texture_index_ >= 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, has_texture_index_ ? texture_index_tex_ : 0);
            glUniform1i(loc_texture_index_, 2);
            glActiveTexture(GL_TEXTURE0);
        }
        if (loc_texture_cell_size_ >= 0)
            glUniform1f(loc_texture_cell_size_, cell_size_);
        if (loc_texture_grid_w_ >= 0)
            glUniform1i(loc_texture_grid_w_, texture_index_tex_w_);
        if (loc_texture_grid_h_ >= 0)
            glUniform1i(loc_texture_grid_h_, texture_index_tex_h_);
        if (loc_has_texture_index_ >= 0)
            glUniform1i(loc_has_texture_index_, has_texture_index_ ? 1 : 0);
        if (loc_texture_world_scale_ >= 0)
            glUniform1f(loc_texture_world_scale_, texture_world_scale_);
        if (loc_texture_atlas_ >= 0)
            glUniform1i(loc_texture_atlas_, 0);
        glBindVertexArray(terrain_vao_);
        if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, terrain_visible_index_count_, GL_UNSIGNED_INT, nullptr);
        if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
        if (color_mode_ == 2 && grid_w_ > 0 && grid_h_ > 0 && !texture_indices_.empty()) {
            const int cx = std::clamp(static_cast<int>(std::floor(pivot_[0] / std::max(cell_size_, 0.0001f))),
                                      0, grid_w_ - 1);
            const int cz = std::clamp(static_cast<int>(std::floor(pivot_[2] / std::max(cell_size_, 0.0001f))),
                                      0, grid_h_ - 1);
            const size_t cidx = static_cast<size_t>(cz) * static_cast<size_t>(grid_w_) + static_cast<size_t>(cx);
            const int ti = (cidx < texture_indices_.size())
                ? static_cast<int>(std::floor(texture_indices_[cidx] + 0.5f)) : -1;
            std::string state = "invalid";
            if (ti >= 0 && ti < texture_lookup_size_ && ti < static_cast<int>(texture_lookup_uvs_.size())) {
                const auto& slot = texture_lookup_uvs_[static_cast<size_t>(ti)];
                state = (slot[2] > 0.0f && slot[3] > 0.0f) ? "resolved" : "missing";
            }
            std::ostringstream ss;
            ss << "Cell[" << cx << "," << cz << "] idx=" << ti << " slot=" << state;
            info = ss.str();
        }
        if (info != last_texture_debug_info_) {
            last_texture_debug_info_ = info;
            on_texture_debug_info_(info);
        }
    }
    return true;
}

void GLWrpTerrainView::cleanup_gl() {
    if (terrain_vao_) { glDeleteVertexArrays(1, &terrain_vao_); terrain_vao_ = 0; }
    if (terrain_vbo_) { glDeleteBuffers(1, &terrain_vbo_); terrain_vbo_ = 0; }
    if (terrain_ebo_) { glDeleteBuffers(1, &terrain_ebo_); terrain_ebo_ = 0; }
    terrain_index_count_ = 0;
    terrain_visible_index_count_ = 0;

    if (points_vao_) { glDeleteVertexArrays(1, &points_vao_); points_vao_ = 0; }
    if (points_vbo_) { glDeleteBuffers(1, &points_vbo_); points_vbo_ = 0; }
    points_count_ = 0;

    if (prog_terrain_) { glDeleteProgram(prog_terrain_); prog_terrain_ = 0; }
    if (prog_points_) { glDeleteProgram(prog_points_); prog_points_ = 0; }
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
        set_error(Glib::Error(GDK_GL_ERROR, 0, std::string("Program link error: ") + log));
    }
    return prog;
}

void GLWrpTerrainView::rebuild_terrain_buffers() {
    make_current();
    if (has_error()) return;

    if (terrain_vao_) { glDeleteVertexArrays(1, &terrain_vao_); terrain_vao_ = 0; }
    if (terrain_vbo_) { glDeleteBuffers(1, &terrain_vbo_); terrain_vbo_ = 0; }
    if (terrain_ebo_) { glDeleteBuffers(1, &terrain_ebo_); terrain_ebo_ = 0; }
    terrain_index_count_ = 0;

    if (grid_w_ <= 1 || grid_h_ <= 1 || heights_.empty()) return;

    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(grid_w_) * static_cast<size_t>(grid_h_));
    for (int z = 0; z < grid_h_; ++z) {
        for (int x = 0; x < grid_w_; ++x) {
            const size_t idx = static_cast<size_t>(z) * static_cast<size_t>(grid_w_) + static_cast<size_t>(x);
            const float h = idx < heights_.size() ? heights_[idx] : 0.0f;
            const float m = idx < surface_classes_.size() ? surface_classes_[idx] : 0.0f;
            float sr = 0.30f, sg = 0.30f, sb = 0.30f;
            const float t = idx < texture_indices_.size() ? texture_indices_[idx] : 0.0f;
            const int ti = static_cast<int>(std::floor(t + 0.5f));
            if (ti >= 0 && static_cast<size_t>(ti) < satellite_palette_.size()) {
                sr = satellite_palette_[static_cast<size_t>(ti)][0];
                sg = satellite_palette_[static_cast<size_t>(ti)][1];
                sb = satellite_palette_[static_cast<size_t>(ti)][2];
            }
            verts.push_back(Vertex{
                static_cast<float>(x) * cell_size_,
                h,
                static_cast<float>(z) * cell_size_,
                h,
                m,
                sr, sg, sb});
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(grid_w_ - 1) * static_cast<size_t>(grid_h_ - 1) * 6);
    for (int z = 0; z < grid_h_ - 1; ++z) {
        for (int x = 0; x < grid_w_ - 1; ++x) {
            const uint32_t i00 = static_cast<uint32_t>(z * grid_w_ + x);
            const uint32_t i10 = static_cast<uint32_t>(z * grid_w_ + (x + 1));
            const uint32_t i01 = static_cast<uint32_t>((z + 1) * grid_w_ + x);
            const uint32_t i11 = static_cast<uint32_t>((z + 1) * grid_w_ + (x + 1));
            indices.push_back(i00); indices.push_back(i01); indices.push_back(i10);
            indices.push_back(i10); indices.push_back(i01); indices.push_back(i11);
        }
    }
    terrain_index_count_ = static_cast<int>(indices.size());
    terrain_visible_indices_ = indices;
    terrain_visible_index_count_ = terrain_index_count_;

    glGenVertexArrays(1, &terrain_vao_);
    glGenBuffers(1, &terrain_vbo_);
    glGenBuffers(1, &terrain_ebo_);

    glBindVertexArray(terrain_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, terrain_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
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
    glBindVertexArray(0);
}

void GLWrpTerrainView::update_visible_terrain_indices(const float* mvp, const float* eye) {
    if (grid_w_ <= 1 || grid_h_ <= 1 || heights_.empty() || terrain_ebo_ == 0) {
        terrain_visible_index_count_ = 0;
        return;
    }

    terrain_visible_indices_.clear();
    terrain_visible_indices_.reserve(static_cast<size_t>(grid_w_ - 1) * static_cast<size_t>(grid_h_ - 1) * 3);
    const float max_dist = std::max(distance_ * 3.0f, cell_size_ * 48.0f);
    const float max_dist2 = max_dist * max_dist;
    const float cull_margin = 1.2f;

    for (int z = 0; z < grid_h_ - 1; ++z) {
        for (int x = 0; x < grid_w_ - 1; ++x) {
            const float wx = (static_cast<float>(x) + 0.5f) * cell_size_;
            const float wz = (static_cast<float>(z) + 0.5f) * cell_size_;
            const size_t idx = static_cast<size_t>(z) * static_cast<size_t>(grid_w_) + static_cast<size_t>(x);
            const float wy = (idx < heights_.size()) ? heights_[idx] : pivot_[1];
            const float dx = wx - eye[0];
            const float dy = wy - eye[1];
            const float dz = wz - eye[2];
            if ((dx * dx + dy * dy + dz * dz) > max_dist2)
                continue;

            const float clip_x = mvp[0] * wx + mvp[4] * wy + mvp[8] * wz + mvp[12];
            const float clip_y = mvp[1] * wx + mvp[5] * wy + mvp[9] * wz + mvp[13];
            const float clip_z = mvp[2] * wx + mvp[6] * wy + mvp[10] * wz + mvp[14];
            const float clip_w = mvp[3] * wx + mvp[7] * wy + mvp[11] * wz + mvp[15];
            if (clip_w <= 0.0001f)
                continue;
            const float ndc_x = clip_x / clip_w;
            const float ndc_y = clip_y / clip_w;
            const float ndc_z = clip_z / clip_w;
            if (ndc_x < -cull_margin || ndc_x > cull_margin
                || ndc_y < -cull_margin || ndc_y > cull_margin
                || ndc_z < -cull_margin || ndc_z > cull_margin) {
                continue;
            }

            const uint32_t i00 = static_cast<uint32_t>(z * grid_w_ + x);
            const uint32_t i10 = static_cast<uint32_t>(z * grid_w_ + (x + 1));
            const uint32_t i01 = static_cast<uint32_t>((z + 1) * grid_w_ + x);
            const uint32_t i11 = static_cast<uint32_t>((z + 1) * grid_w_ + (x + 1));
            terrain_visible_indices_.push_back(i00);
            terrain_visible_indices_.push_back(i01);
            terrain_visible_indices_.push_back(i10);
            terrain_visible_indices_.push_back(i10);
            terrain_visible_indices_.push_back(i01);
            terrain_visible_indices_.push_back(i11);
        }
    }

    terrain_visible_index_count_ = static_cast<int>(terrain_visible_indices_.size());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, terrain_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(terrain_visible_indices_.size() * sizeof(uint32_t)),
                 terrain_visible_indices_.empty() ? nullptr : terrain_visible_indices_.data(),
                 GL_DYNAMIC_DRAW);
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

void GLWrpTerrainView::build_mvp(float* mvp) const {
    float eye[3] = {pivot_[0], pivot_[1] + distance_, pivot_[2]};

    // Forward direction based on azimuth and elevation
    const float ce = std::cos(elevation_);
    const float se = std::sin(elevation_);
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);
    
    float center[3];
    center[0] = eye[0] + ce * sa;
    center[1] = eye[1] + se;
    center[2] = eye[2] + ce * ca;

    float view[16];
    float up[3] = {0.0f, 1.0f, 0.0f};
    mat4_look_at(view, eye, center, up);

    const int w = get_width();
    const int h = get_height();
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    float proj[16];
    mat4_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.5f, 500000.0f);
    mat4_multiply(mvp, proj, view);
}

void GLWrpTerrainView::pick_object_at(double x, double y) {
    if (!on_object_picked_ || object_positions_.empty()) return;

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
        on_object_picked_(best_idx);
    }
}

void GLWrpTerrainView::move_camera_local(float forward, float right) {
    const float ca = std::cos(azimuth_);
    const float sa = std::sin(azimuth_);

    // Forward direction on XZ plane
    const float fx = sa;
    const float fz = ca;
    // Right vector on XZ plane
    const float rx = ca;
    const float rz = -sa;

    pivot_[0] += fx * forward + rx * right;
    pivot_[2] += fz * forward + rz * right;
    queue_render();
}

bool GLWrpTerrainView::movement_tick() {
    float forward = 0.0f;
    float right = 0.0f;
    float vertical = 0.0f;
    if (move_fwd_) forward += 1.0f;
    if (move_back_) forward -= 1.0f;
    if (move_right_) right -= 1.0f;
    if (move_left_) right += 1.0f;
    if (move_up_) vertical += 1.0f;
    if (move_down_) vertical -= 1.0f;
    if (forward == 0.0f && right == 0.0f && vertical == 0.0f) return false;

    float step = std::max(0.5f, distance_ * 0.006f);
    if (move_fast_ && !alt_pressed_) step *= 3.0f;
    move_camera_local(forward * step, right * step);
    pivot_[1] += vertical * step;
    queue_render();
    return true;
}
