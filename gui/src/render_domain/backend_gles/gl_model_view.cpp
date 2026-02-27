#include "gl_model_view.h"

#include "gl_error_log.h"
#include "infra/gl/load_resource_text.h"
#include "log_panel.h"
#include "render_domain/rd_scene_blob.h"
#include "render_domain/rd_runtime_state.h"

#include <armatools/armapath.h>

#include <epoxy/gl.h>
#include <cmath>
#include <cstring>
#include <algorithm>

// ---- Shader resources ----

static constexpr const char* kVertResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view.vert";
static constexpr const char* kFragSolidResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_solid.frag";
static constexpr const char* kFragWireResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_wire.frag";
static constexpr const char* kVertEsResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_es.vert";
static constexpr const char* kFragSolidEsResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_solid_es.frag";
static constexpr const char* kFragWireEsResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_wire_es.frag";
static constexpr const char* kVertWireResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_wire.vert";
static constexpr const char* kVertWireEsResource =
    "/com/bigbangit/ArmaTools/data/shaders/gl_model_view_wire_es.vert";

// ---- Matrix math ----

static void mat4_identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_multiply(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            tmp[j * 4 + i] = 0;
            for (int k = 0; k < 4; k++)
                tmp[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    std::memcpy(out, tmp, sizeof(tmp));
}

static void mat4_perspective(float* m, float fov_rad, float aspect, float near, float far) {
    std::memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / std::tan(fov_rad / 2.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void vec3_cross(float* out, const float* a, const float* b) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void vec3_normalize(float* v) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-8f) { v[0] /= len; v[1] /= len; v[2] /= len; }
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
    m[0] = s[0]; m[4] = s[1]; m[8]  = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9]  = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
}

static void mat3_normal_from_mat4(float* out3, const float* mv4) {
    // Extract upper-left 3x3, compute cofactor matrix (transpose of adjugate)
    // For orthogonal view matrices, this is just the upper-left 3x3
    out3[0] = mv4[0]; out3[1] = mv4[1]; out3[2] = mv4[2];
    out3[3] = mv4[4]; out3[4] = mv4[5]; out3[5] = mv4[6];
    out3[6] = mv4[8]; out3[7] = mv4[9]; out3[8] = mv4[10];
}

// ---- GLModelView ----

GLModelView::GLModelView() {
    set_has_depth_buffer(true);
    set_auto_render(true);
    set_hexpand(true);
    set_vexpand(true);
    set_size_request(200, 200);
    set_focusable(true);

    signal_realize().connect(sigc::mem_fun(*this, &GLModelView::on_realize_gl), false);
    signal_unrealize().connect(sigc::mem_fun(*this, &GLModelView::on_unrealize_gl), false);
    signal_render().connect(sigc::mem_fun(*this, &GLModelView::on_render_gl), false);

    // Look drag (button 1) - rotate view in place around current eye.
    drag_orbit_ = Gtk::GestureDrag::create();
    drag_orbit_->set_button(GDK_BUTTON_PRIMARY);
    drag_orbit_->signal_drag_begin().connect([this](double x, double y) {
        drag_start_x_ = x;
        drag_start_y_ = y;
        const auto state = camera_controller_.camera_state();
        drag_start_azimuth_ = state.azimuth;
        drag_start_elevation_ = state.elevation;
    });
    drag_orbit_->signal_drag_update().connect([this](double dx, double dy) {
        camera_controller_.orbit_from_drag(
            drag_start_azimuth_, drag_start_elevation_, dx, dy);
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
    });
    add_controller(drag_orbit_);

    // Look drag (button 3 / right) - same behavior as primary mouse look.
    drag_look_ = Gtk::GestureDrag::create();
    drag_look_->set_button(GDK_BUTTON_SECONDARY);
    drag_look_->signal_drag_begin().connect([this](double x, double y) {
        drag_start_x_ = x;
        drag_start_y_ = y;
        const auto state = camera_controller_.camera_state();
        drag_start_azimuth_ = state.azimuth;
        drag_start_elevation_ = state.elevation;
    });
    drag_look_->signal_drag_update().connect([this](double dx, double dy) {
        camera_controller_.orbit_from_drag(
            drag_start_azimuth_, drag_start_elevation_, dx, dy);
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
    });
    add_controller(drag_look_);

    // Pan drag (button 2 / middle)
    drag_pan_ = Gtk::GestureDrag::create();
    drag_pan_->set_button(GDK_BUTTON_MIDDLE);
    drag_pan_->signal_drag_begin().connect([this](double, double) {
        const auto state = camera_controller_.camera_state();
        std::memcpy(drag_start_pivot_, state.pivot, sizeof(drag_start_pivot_));
    });
    drag_pan_->signal_drag_update().connect([this](double dx, double dy) {
        camera_controller_.pan_from_drag(drag_start_pivot_, dx, dy);
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
    });
    add_controller(drag_pan_);

    // Scroll zoom
    scroll_zoom_ = Gtk::EventControllerScroll::create();
    scroll_zoom_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_zoom_->signal_scroll().connect([this](double, double dy) -> bool {
        if (camera_controller_.scroll_zoom(dy)) {
            queue_render();
            if (!suppress_camera_signal_) signal_camera_changed_.emit();
        } else {
            float step = std::max(0.02f, camera_controller_.distance() * 0.08f);
            move_camera_local((dy > 0) ? -step : step, 0.0f, 0.0f);
        }
        return true;
    }, false);
    add_controller(scroll_zoom_);

    click_focus_ = Gtk::GestureClick::create();
    click_focus_->set_button(0);
    click_focus_->signal_pressed().connect([this](int, double, double) {
        grab_focus();
    });
    add_controller(click_focus_);

    key_move_ = Gtk::EventControllerKey::create();
    key_move_->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
            if (camera_controller_.camera_mode() != CameraMode::FirstPerson) return false;
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
            default:
                handled = false;
                break;
            }
            if ((state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType(0))
                move_fast_ = true;
            if (handled && !move_tick_conn_.connected()) {
                move_tick_conn_ = Glib::signal_timeout().connect(
                    sigc::mem_fun(*this, &GLModelView::movement_tick), 16);
            }
            return handled;
        }, false);
    key_move_->signal_key_released().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            if (camera_controller_.camera_mode() != CameraMode::FirstPerson) return;
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
            default: break;
            }
            if ((state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType(0))
                move_fast_ = false;
            if (!move_fwd_ && !move_back_ && !move_left_ && !move_right_
                && !move_up_ && !move_down_) {
                move_tick_conn_.disconnect();
            }
        });
    add_controller(key_move_);
}

GLModelView::~GLModelView() = default;

void GLModelView::on_realize_gl() {
    make_current();
    if (has_error()) {
        app_log(LogLevel::Error, "GLModelView: GL context creation failed");
        return;
    }

    is_desktop_gl_ = epoxy_is_desktop_gl();
    int ver = epoxy_gl_version();
    app_log(LogLevel::Info, "GLModelView: using " +
        std::string(is_desktop_gl_ ? "OpenGL" : "OpenGL ES") +
        " " + std::to_string(ver / 10) + "." + std::to_string(ver % 10));

    // Select shader sources based on API
    const std::string vert = infra::gl::load_resource_text(
        is_desktop_gl_ ? kVertResource : kVertEsResource);
    const std::string frag_solid = infra::gl::load_resource_text(
        is_desktop_gl_ ? kFragSolidResource : kFragSolidEsResource);
    const std::string frag_wire = infra::gl::load_resource_text(
        is_desktop_gl_ ? kFragWireResource : kFragWireEsResource);
    const std::string vert_wire = infra::gl::load_resource_text(
        is_desktop_gl_ ? kVertWireResource : kVertWireEsResource);

    // Compile shaders
    auto vs = compile_shader(GL_VERTEX_SHADER, vert.c_str());
    auto fs_solid = compile_shader(GL_FRAGMENT_SHADER, frag_solid.c_str());
    auto fs_wire = compile_shader(GL_FRAGMENT_SHADER, frag_wire.c_str());
    auto vs_wire = compile_shader(GL_VERTEX_SHADER, vert_wire.c_str());

    prog_solid_ = link_program(vs, fs_solid);
    prog_wire_ = link_program(vs_wire, fs_wire);

    glDeleteShader(vs);
    glDeleteShader(fs_solid);
    glDeleteShader(fs_wire);
    glDeleteShader(vs_wire);

    // Cache uniform locations
    loc_mvp_solid_ = glGetUniformLocation(prog_solid_, "uMVP");
    loc_normal_mat_ = glGetUniformLocation(prog_solid_, "uNormalMat");
    loc_texture_ = glGetUniformLocation(prog_solid_, "uTexture");
    loc_normal_map_ = glGetUniformLocation(prog_solid_, "uNormalMap");
    loc_specular_map_ = glGetUniformLocation(prog_solid_, "uSpecularMap");
    loc_has_texture_ = glGetUniformLocation(prog_solid_, "uHasTexture");
    loc_has_normal_map_ = glGetUniformLocation(prog_solid_, "uHasNormalMap");
    loc_has_specular_map_ = glGetUniformLocation(prog_solid_, "uHasSpecularMap");
    loc_light_dir_ = glGetUniformLocation(prog_solid_, "uLightDir");
    loc_has_material_ = glGetUniformLocation(prog_solid_, "uHasMaterial");
    loc_mat_ambient_ = glGetUniformLocation(prog_solid_, "uMatAmbient");
    loc_mat_diffuse_ = glGetUniformLocation(prog_solid_, "uMatDiffuse");
    loc_mat_emissive_ = glGetUniformLocation(prog_solid_, "uMatEmissive");
    loc_mat_specular_ = glGetUniformLocation(prog_solid_, "uMatSpecular");
    loc_mat_spec_power_ = glGetUniformLocation(prog_solid_, "uMatSpecPower");
    loc_shader_mode_ = glGetUniformLocation(prog_solid_, "uShaderMode");
    loc_mvp_wire_ = glGetUniformLocation(prog_wire_, "uMVP");
    loc_color_wire_ = glGetUniformLocation(prog_wire_, "uColor");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);  // X is negated to fix P3D left-handed mirror, which flips winding

    // Build grid and axis geometry
    build_grid_and_axis();
    log_gl_errors("GLModelView::on_realize_gl");
}

void GLModelView::on_unrealize_gl() {
    make_current();
    if (has_error()) return;
    cleanup_gl();
    log_gl_errors("GLModelView::on_unrealize_gl");
}

void GLModelView::clear_mesh_groups() {
    for (auto& g : groups_) {
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
    }
    groups_.clear();

    if (wire_vao_) { glDeleteVertexArrays(1, &wire_vao_); wire_vao_ = 0; }
    if (wire_vbo_) { glDeleteBuffers(1, &wire_vbo_); wire_vbo_ = 0; }
    if (wire_ebo_) { glDeleteBuffers(1, &wire_ebo_); wire_ebo_ = 0; }
    wire_index_count_ = 0;
}

void GLModelView::cleanup_gl() {
    clear_mesh_groups();

    for (auto& [key, tex] : textures_)
        glDeleteTextures(1, &tex);
    textures_.clear();
    for (auto& [key, tex] : normal_maps_)
        glDeleteTextures(1, &tex);
    normal_maps_.clear();
    for (auto& [key, tex] : specular_maps_)
        glDeleteTextures(1, &tex);
    specular_maps_.clear();
    texture_has_alpha_.clear();
    material_params_.clear();

    if (grid_vao_) { glDeleteVertexArrays(1, &grid_vao_); grid_vao_ = 0; }
    if (grid_vbo_) { glDeleteBuffers(1, &grid_vbo_); grid_vbo_ = 0; }
    grid_line_count_ = 0;

    if (axis_vao_) { glDeleteVertexArrays(1, &axis_vao_); axis_vao_ = 0; }
    if (axis_vbo_) { glDeleteBuffers(1, &axis_vbo_); axis_vbo_ = 0; }
    if (highlight_vao_) { glDeleteVertexArrays(1, &highlight_vao_); highlight_vao_ = 0; }
    if (highlight_vbo_) { glDeleteBuffers(1, &highlight_vbo_); highlight_vbo_ = 0; }
    highlight_vertex_count_ = 0;

    if (prog_solid_) { glDeleteProgram(prog_solid_); prog_solid_ = 0; }
    if (prog_wire_) { glDeleteProgram(prog_wire_); prog_wire_ = 0; }
    has_geometry_ = false;
}

uint32_t GLModelView::compile_shader(uint32_t type, const char* source) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        app_log(LogLevel::Error, std::string("GLModelView shader compile error: ") + log);
        set_error(Glib::Error(GDK_GL_ERROR, 0, std::string("Shader compile error: ") + log));
    }
    return shader;
}

uint32_t GLModelView::link_program(uint32_t vert, uint32_t frag) {
    uint32_t prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        app_log(LogLevel::Error, std::string("GLModelView program link error: ") + log);
        set_error(Glib::Error(GDK_GL_ERROR, 0, std::string("Program link error: ") + log));
    }
    return prog;
}

void GLModelView::build_grid_and_axis() {
    // ---- Grid: lines on XZ plane at Y=0, from -10 to +10, step 1.0 ----
    // Each line is 2 vertices (x, y, z), so 6 floats per line.
    // Lines parallel to X axis: for each z from -10 to +10 (21 lines)
    // Lines parallel to Z axis: for each x from -10 to +10 (21 lines)
    // Total: 42 lines = 84 vertices
    std::vector<float> grid_verts;
    grid_verts.reserve(42 * 2 * 3);

    for (int i = -10; i <= 10; i++) {
        float v = static_cast<float>(i);
        // Line parallel to X axis at z = v
        grid_verts.push_back(-10.0f); grid_verts.push_back(0.0f); grid_verts.push_back(v);
        grid_verts.push_back( 10.0f); grid_verts.push_back(0.0f); grid_verts.push_back(v);
        // Line parallel to Z axis at x = v
        grid_verts.push_back(v); grid_verts.push_back(0.0f); grid_verts.push_back(-10.0f);
        grid_verts.push_back(v); grid_verts.push_back(0.0f); grid_verts.push_back( 10.0f);
    }

    grid_line_count_ = static_cast<int>(grid_verts.size()) / 3;

    glGenVertexArrays(1, &grid_vao_);
    glGenBuffers(1, &grid_vbo_);
    glBindVertexArray(grid_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(grid_verts.size() * sizeof(float)),
                 grid_verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    // ---- Axis: 3 line segments from origin, length 1.0 ----
    // 6 vertices (2 per axis), each with position (3 floats) + color (3 floats)
    // We store them interleaved: pos.xyz, color.rgb
    // X axis: red, Y axis: green, Z axis: blue
    float axis_data[] = {
        // X axis (red)
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        // Y axis (green)
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        // Z axis (blue)
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &axis_vao_);
    glGenBuffers(1, &axis_vbo_);
    glBindVertexArray(axis_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, axis_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axis_data), axis_data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glBindVertexArray(0);
}

void GLModelView::draw_grid_and_axis(const float* mvp) {
    if (!prog_wire_ || !show_grid_) return;

    glUseProgram(prog_wire_);
    glUniformMatrix4fv(loc_mvp_wire_, 1, GL_FALSE, mvp);

    // Draw grid lines in gray
    glUniform3f(loc_color_wire_, 0.4f, 0.4f, 0.4f);
    glBindVertexArray(grid_vao_);
    glDrawArrays(GL_LINES, 0, grid_line_count_);

    // Draw axis lines with per-axis colors
    glBindVertexArray(axis_vao_);
    glLineWidth(2.0f);

    // X axis - red
    glUniform3f(loc_color_wire_, 1.0f, 0.0f, 0.0f);
    glDrawArrays(GL_LINES, 0, 2);

    // Y axis - green
    glUniform3f(loc_color_wire_, 0.0f, 1.0f, 0.0f);
    glDrawArrays(GL_LINES, 2, 2);

    // Z axis - blue
    glUniform3f(loc_color_wire_, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_LINES, 4, 2);

    glLineWidth(1.0f);
    glBindVertexArray(0);
}

void GLModelView::set_scene_blob(const rd_scene_blob_v1& blob,
                                 const std::vector<std::string>& material_texture_keys) {
    make_current();
    if (has_error()) return;

    std::string validation_error;
    if (!render_domain::validate_scene_blob_v1(blob, &validation_error)) {
        app_log(LogLevel::Error, "GLModelView: scene blob validation failed: " + validation_error);
        clear_mesh_groups();
        has_geometry_ = false;
        queue_render();
        return;
    }

    clear_mesh_groups();

    const bool has_normals = (blob.flags & RD_SCENE_BLOB_FLAG_HAS_NORMALS) != 0;
    const bool has_uv0 = (blob.flags & RD_SCENE_BLOB_FLAG_HAS_UV0) != 0;
    const bool index32 = (blob.flags & RD_SCENE_BLOB_FLAG_INDEX32) != 0;
    static const uint8_t kEmptyData = 0;
    const auto* blob_data = blob.data ? blob.data : &kEmptyData;

    const auto* positions = reinterpret_cast<const float*>(blob_data + blob.positions_offset);
    const auto* normals = has_normals
        ? reinterpret_cast<const float*>(blob_data + blob.normals_offset)
        : nullptr;
    const auto* uv0 = has_uv0
        ? reinterpret_cast<const float*>(blob_data + blob.uv0_offset)
        : nullptr;
    const auto* indices_u32 = index32
        ? reinterpret_cast<const uint32_t*>(blob_data + blob.indices_offset)
        : nullptr;
    const auto* indices_u16 = index32
        ? nullptr
        : reinterpret_cast<const uint16_t*>(blob_data + blob.indices_offset);
    const auto* meshes = blob.mesh_count > 0
        ? reinterpret_cast<const rd_scene_mesh_v1*>(blob_data + blob.meshes_offset)
        : nullptr;

    std::vector<float> all_positions;
    size_t empty_key_groups = 0;

    for (uint32_t mesh_idx = 0; mesh_idx < blob.mesh_count; ++mesh_idx) {
        const auto& mesh = meshes[mesh_idx];
        if (mesh.index_count < 3) continue;

        std::vector<float> verts;
        verts.reserve(static_cast<size_t>(mesh.index_count) * 11);

        for (uint32_t i = 0; i + 2 < mesh.index_count; i += 3) {
            const uint32_t i0 = mesh.index_offset + i;
            const uint32_t i1 = mesh.index_offset + i + 1;
            const uint32_t i2 = mesh.index_offset + i + 2;

            const uint32_t v0 = index32 ? indices_u32[i0] : static_cast<uint32_t>(indices_u16[i0]);
            const uint32_t v1 = index32 ? indices_u32[i1] : static_cast<uint32_t>(indices_u16[i1]);
            const uint32_t v2 = index32 ? indices_u32[i2] : static_cast<uint32_t>(indices_u16[i2]);
            if (v0 >= blob.vertex_count || v1 >= blob.vertex_count || v2 >= blob.vertex_count) {
                continue;
            }

            const float p0x = positions[v0 * 3 + 0];
            const float p0y = positions[v0 * 3 + 1];
            const float p0z = positions[v0 * 3 + 2];
            const float p1x = positions[v1 * 3 + 0];
            const float p1y = positions[v1 * 3 + 1];
            const float p1z = positions[v1 * 3 + 2];
            const float p2x = positions[v2 * 3 + 0];
            const float p2y = positions[v2 * 3 + 1];
            const float p2z = positions[v2 * 3 + 2];

            const float u0 = uv0 ? uv0[v0 * 2 + 0] : 0.0f;
            const float vv0 = uv0 ? uv0[v0 * 2 + 1] : 0.0f;
            const float u1 = uv0 ? uv0[v1 * 2 + 0] : 0.0f;
            const float vv1 = uv0 ? uv0[v1 * 2 + 1] : 0.0f;
            const float u2 = uv0 ? uv0[v2 * 2 + 0] : 0.0f;
            const float vv2 = uv0 ? uv0[v2 * 2 + 1] : 0.0f;

            const float e1x = p1x - p0x;
            const float e1y = p1y - p0y;
            const float e1z = p1z - p0z;
            const float e2x = p2x - p0x;
            const float e2y = p2y - p0y;
            const float e2z = p2z - p0z;
            const float du1 = u1 - u0;
            const float dv1 = vv1 - vv0;
            const float du2 = u2 - u0;
            const float dv2 = vv2 - vv0;
            const float denom = du1 * dv2 - dv1 * du2;

            float tx = 1.0f;
            float ty = 0.0f;
            float tz = 0.0f;
            if (std::abs(denom) > 1e-8f) {
                const float r = 1.0f / denom;
                tx = (dv2 * e1x - dv1 * e2x) * r;
                ty = (dv2 * e1y - dv1 * e2y) * r;
                tz = (dv2 * e1z - dv1 * e2z) * r;
                const float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
                if (tlen > 1e-8f) {
                    tx /= tlen;
                    ty /= tlen;
                    tz /= tlen;
                } else {
                    tx = 1.0f;
                    ty = 0.0f;
                    tz = 0.0f;
                }
            }

            const uint32_t tri_vertices[3] = {v0, v1, v2};
            for (uint32_t v : tri_vertices) {
                verts.push_back(positions[v * 3 + 0]);
                verts.push_back(positions[v * 3 + 1]);
                verts.push_back(positions[v * 3 + 2]);

                if (normals) {
                    verts.push_back(normals[v * 3 + 0]);
                    verts.push_back(normals[v * 3 + 1]);
                    verts.push_back(normals[v * 3 + 2]);
                } else {
                    verts.push_back(0.0f);
                    verts.push_back(1.0f);
                    verts.push_back(0.0f);
                }

                if (uv0) {
                    verts.push_back(uv0[v * 2 + 0]);
                    verts.push_back(uv0[v * 2 + 1]);
                } else {
                    verts.push_back(0.0f);
                    verts.push_back(0.0f);
                }

                verts.push_back(tx);
                verts.push_back(ty);
                verts.push_back(tz);

                all_positions.push_back(positions[v * 3 + 0]);
                all_positions.push_back(positions[v * 3 + 1]);
                all_positions.push_back(positions[v * 3 + 2]);
            }
        }

        if (verts.empty()) continue;

        MeshGroup g;
        if (mesh.material_index < material_texture_keys.size()) {
            g.texture_key = material_texture_keys[mesh.material_index];
        }
        if (g.texture_key.empty()) ++empty_key_groups;
        g.vertex_count = static_cast<int>(verts.size() / 11);

        glGenVertexArrays(1, &g.vao);
        glGenBuffers(1, &g.vbo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float),
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float),
                              reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float),
                              reinterpret_cast<void*>(8 * sizeof(float)));
        glBindVertexArray(0);

        groups_.push_back(std::move(g));
    }

    if (!is_desktop_gl_) {
        const size_t total_triangles = all_positions.size() / 9;
        std::vector<uint32_t> line_indices;
        line_indices.reserve(total_triangles * 6);
        for (size_t t = 0; t < total_triangles; ++t) {
            const uint32_t base = static_cast<uint32_t>(t * 3);
            line_indices.push_back(base);     line_indices.push_back(base + 1);
            line_indices.push_back(base + 1); line_indices.push_back(base + 2);
            line_indices.push_back(base + 2); line_indices.push_back(base);
        }

        wire_index_count_ = static_cast<int>(line_indices.size());
        if (wire_index_count_ > 0) {
            glGenVertexArrays(1, &wire_vao_);
            glGenBuffers(1, &wire_vbo_);
            glGenBuffers(1, &wire_ebo_);
            glBindVertexArray(wire_vao_);
            glBindBuffer(GL_ARRAY_BUFFER, wire_vbo_);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(all_positions.size() * sizeof(float)),
                         all_positions.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                                  reinterpret_cast<void*>(0));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wire_ebo_);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(line_indices.size() * sizeof(uint32_t)),
                         line_indices.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);
        }
    }

    has_geometry_ = !groups_.empty();
    debug_group_report_pending_ = true;
    app_log(LogLevel::Debug,
            "GLModelView: scene blob applied | " +
                render_domain::summarize_scene_blob_v1(blob) +
                " groups=" + std::to_string(groups_.size()) +
                " textures_loaded=" + std::to_string(textures_.size()) +
                " materials_loaded=" + std::to_string(material_params_.size()) +
                " empty_group_keys=" + std::to_string(empty_key_groups));
    queue_render();
}

void GLModelView::set_texture(const std::string& key, int width, int height,
                               const uint8_t* rgba_data) {
    make_current();
    if (has_error()) return;

    // Normalize key for case-insensitive matching with face texture keys
    auto norm_key = armatools::armapath::to_slash_lower(key);

    // Delete existing texture for this key
    auto it = textures_.find(norm_key);
    if (it != textures_.end()) {
        glDeleteTextures(1, &it->second);
        textures_.erase(it);
    }

    // Upload image data as-is (no row flip needed).
    // PAA images are top-to-bottom, and glTexImage2D treats row 0 as the
    // bottom of the texture.  Combined with P3D's top-down UV convention
    // (V=0 = top), GL V=0 maps to data[0] = image top — the two inversions
    // cancel out, so raw UVs and raw image data produce correct results.
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    textures_[norm_key] = tex;

    // Scan for alpha transparency
    bool has_alpha = false;
    size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixel_count; i++) {
        if (rgba_data[i * 4 + 3] < 255) {
            has_alpha = true;
            break;
        }
    }
    texture_has_alpha_[norm_key] = has_alpha;

    queue_render();
}

void GLModelView::reset_camera() {
    camera_controller_.reset_camera();
    const auto state = camera_controller_.camera_state();
    drag_start_x_ = 0.0;
    drag_start_y_ = 0.0;
    drag_start_azimuth_ = state.azimuth;
    drag_start_elevation_ = state.elevation;
    std::memcpy(drag_start_pivot_, state.pivot, sizeof(drag_start_pivot_));
    move_fwd_ = false;
    move_back_ = false;
    move_left_ = false;
    move_right_ = false;
    move_up_ = false;
    move_down_ = false;
    move_fast_ = false;
    move_tick_conn_.disconnect();
    queue_render();
}

void GLModelView::set_camera_from_bounds(float cx, float cy, float cz, float radius) {
    camera_controller_.set_camera_from_bounds(cx, cy, cz, radius);
    queue_render();
}

GLModelView::CameraState GLModelView::get_camera_state() const {
    return camera_controller_.camera_state();
}

void GLModelView::set_camera_state(const CameraState& state) {
    suppress_camera_signal_ = true;
    camera_controller_.set_camera_state(state);
    queue_render();
    suppress_camera_signal_ = false;
}

sigc::signal<void()>& GLModelView::signal_camera_changed() {
    return signal_camera_changed_;
}

void GLModelView::set_wireframe(bool on) {
    wireframe_ = on;
    queue_render();
}

void GLModelView::set_textured(bool on) {
    textured_ = on;
    queue_render();
}

void GLModelView::set_show_grid(bool on) {
    show_grid_ = on;
    queue_render();
}

void GLModelView::set_background_color(float r, float g, float b) {
    bg_color_[0] = r;
    bg_color_[1] = g;
    bg_color_[2] = b;
    queue_render();
}

void GLModelView::set_camera_mode(CameraMode mode) {
    if (!camera_controller_.set_camera_mode(mode)) return;

    // Reset transient input state after a mode switch.
    const auto state = camera_controller_.camera_state();
    drag_start_x_ = 0.0;
    drag_start_y_ = 0.0;
    drag_start_azimuth_ = state.azimuth;
    drag_start_elevation_ = state.elevation;
    std::memcpy(drag_start_pivot_, state.pivot, sizeof(drag_start_pivot_));
    move_fwd_ = false;
    move_back_ = false;
    move_left_ = false;
    move_right_ = false;
    move_up_ = false;
    move_down_ = false;
    move_fast_ = false;
    move_tick_conn_.disconnect();

    queue_render();
    if (!suppress_camera_signal_) signal_camera_changed_.emit();
}

GLModelView::CameraMode GLModelView::camera_mode() const {
    return camera_controller_.camera_mode();
}

Glib::RefPtr<Gdk::Pixbuf> GLModelView::snapshot() const {
    auto* self = const_cast<GLModelView*>(this);
    self->make_current();
    if (has_error()) return {};

    int w = get_width();
    int h = get_height();
    if (w <= 0 || h <= 0) return {};

    size_t sw = static_cast<size_t>(w);
    size_t sh = static_cast<size_t>(h);
    size_t row_bytes = sw * 4;

    std::vector<uint8_t> pixels(sw * sh * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // glReadPixels returns bottom-up; flip vertically
    std::vector<uint8_t> row(row_bytes);
    for (size_t y = 0; y < sh / 2; y++) {
        uint8_t* top = pixels.data() + y * row_bytes;
        uint8_t* bot = pixels.data() + (sh - 1 - y) * row_bytes;
        std::memcpy(row.data(), top, row_bytes);
        std::memcpy(top, bot, row_bytes);
        std::memcpy(bot, row.data(), row_bytes);
    }

    auto pixbuf = Gdk::Pixbuf::create_from_data(
        pixels.data(), Gdk::Colorspace::RGB, true, 8, w, h,
        static_cast<int>(row_bytes));
    return pixbuf->copy();
}

void GLModelView::set_normal_map(const std::string& key, int width, int height,
                                 const uint8_t* rgba_data) {
    make_current();
    if (has_error() || width <= 0 || height <= 0 || !rgba_data) return;
    auto norm_key = armatools::armapath::to_slash_lower(key);
    auto it = normal_maps_.find(norm_key);
    if (it != normal_maps_.end()) {
        glDeleteTextures(1, &it->second);
        normal_maps_.erase(it);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    normal_maps_[norm_key] = tex;
    queue_render();
}

void GLModelView::set_specular_map(const std::string& key, int width, int height,
                                   const uint8_t* rgba_data) {
    make_current();
    if (has_error() || width <= 0 || height <= 0 || !rgba_data) return;
    auto norm_key = armatools::armapath::to_slash_lower(key);
    auto it = specular_maps_.find(norm_key);
    if (it != specular_maps_.end()) {
        glDeleteTextures(1, &it->second);
        specular_maps_.erase(it);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    specular_maps_[norm_key] = tex;
    queue_render();
}

void GLModelView::set_material_params(const std::string& key,
                                      const MaterialParams& params) {
    auto norm_key = armatools::armapath::to_slash_lower(key);
    material_params_[norm_key] = params;
    queue_render();
}

void GLModelView::move_camera_local(float forward, float right, float up) {
    camera_controller_.move_local(forward, right, up);
    queue_render();
    if (!suppress_camera_signal_) signal_camera_changed_.emit();
}

bool GLModelView::movement_tick() {
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

    float step = std::max(0.01f, camera_controller_.distance() * 0.006f);
    if (move_fast_) step *= 3.0f;
    move_camera_local(forward * step, right * step, vertical * step);
    return true;
}

void GLModelView::rebuild_highlight_vertex_buffer() {
    make_current();
    if (has_error()) return;

    if (highlight_vao_) { glDeleteVertexArrays(1, &highlight_vao_); highlight_vao_ = 0; }
    if (highlight_vbo_) { glDeleteBuffers(1, &highlight_vbo_); highlight_vbo_ = 0; }
    highlight_vertex_count_ = 0;

    if (highlight_geometry_.empty()) {
        app_log(LogLevel::Debug, "Highlight buffer: empty geometry");
        return;
    }

    highlight_vertex_count_ = static_cast<int>(highlight_geometry_.size() / 3);
    if (highlight_vertex_count_ == 0) {
        app_log(LogLevel::Debug, "Highlight buffer: geometry data has no vertices");
        return;
    }

    glGenVertexArrays(1, &highlight_vao_);
    glGenBuffers(1, &highlight_vbo_);
    glBindVertexArray(highlight_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, highlight_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(highlight_geometry_.size() * sizeof(float)),
                 highlight_geometry_.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glBindVertexArray(0);
    app_log(LogLevel::Debug,
            "Highlight buffer rebuilt: mode=" +
            std::string(highlight_mode_ == HighlightMode::Lines ? "lines" : "points") +
            " vertices=" + std::to_string(highlight_vertex_count_));
}

void GLModelView::set_highlight_geometry(const std::vector<float>& positions,
                                         HighlightMode mode) {
    highlight_geometry_ = positions;
    highlight_mode_ = mode;
    rebuild_highlight_vertex_buffer();
    queue_render();
}

void GLModelView::build_matrices(float* mvp, float* normal_mat) {
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
    camera_controller_.build_eye_center(eye, center);

    float up[3] = {0, 1, 0};

    float view[16];
    mat4_look_at(view, eye, center, up);

    int w = get_width();
    int h = get_height();
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    float far_plane = camera_controller_.far_plane();

    float proj[16];
    mat4_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, far_plane);

    const rd_camera_blob_v1 camera = render_domain::make_camera_blob_v1(view, proj, eye);
    std::string camera_error;
    if (!render_domain::validate_camera_blob_v1(camera, &camera_error)) {
        app_log(LogLevel::Error, "GLModelView: invalid camera blob: " + camera_error);
        mat4_identity(mvp);
        float identity4[16];
        mat4_identity(identity4);
        mat3_normal_from_mat4(normal_mat, identity4);
        return;
    }

    mat4_multiply(mvp, camera.projection, camera.view);
    mat3_normal_from_mat4(normal_mat, camera.view);
}

bool GLModelView::on_render_gl(const Glib::RefPtr<Gdk::GLContext>&) {
    glClearColor(bg_color_[0], bg_color_[1], bg_color_[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float mvp[16], normal_mat[9];
    build_matrices(mvp, normal_mat);

    // Draw grid and axis before the model
    draw_grid_and_axis(mvp);

    if (!prog_solid_) return true;

    if (has_geometry_) {
        if (debug_group_report_pending_) {
            size_t missing_texture = 0;
            size_t missing_material = 0;
            size_t empty_key = 0;
            size_t logged = 0;
            for (const auto& g : groups_) {
                if (g.texture_key.empty()) ++empty_key;
                if (!textures_.contains(g.texture_key)) ++missing_texture;
                if (!material_params_.contains(g.texture_key)) ++missing_material;
                if (logged < 8) {
                    const bool has_tex = textures_.contains(g.texture_key);
                    const bool has_mat = material_params_.contains(g.texture_key);
                    app_log(LogLevel::Debug,
                            "GLModelView: group key='" + g.texture_key
                            + "' verts=" + std::to_string(g.vertex_count)
                            + " has_tex=" + std::string(has_tex ? "yes" : "no")
                            + " has_mat=" + std::string(has_mat ? "yes" : "no"));
                    ++logged;
                }
            }
            app_log(LogLevel::Debug,
                    "GLModelView: group_bind_summary groups=" + std::to_string(groups_.size())
                    + " missing_tex=" + std::to_string(missing_texture)
                    + " missing_mat=" + std::to_string(missing_material)
                    + " empty_keys=" + std::to_string(empty_key)
                    + " textures_loaded=" + std::to_string(textures_.size())
                    + " materials_loaded=" + std::to_string(material_params_.size()));
            debug_group_report_pending_ = false;
        }

        // Light direction (normalized, world space — from upper-right-front)
        float light_dir[3] = {0.4f, 0.7f, 0.5f};
        vec3_normalize(light_dir);

        // Common solid shader setup
        glUseProgram(prog_solid_);
        glUniformMatrix4fv(loc_mvp_solid_, 1, GL_FALSE, mvp);
        glUniformMatrix3fv(loc_normal_mat_, 1, GL_FALSE, normal_mat);
        glUniform3fv(loc_light_dir_, 1, light_dir);
        glUniform1i(loc_texture_, 0);
        glUniform1i(loc_normal_map_, 1);
        glUniform1i(loc_specular_map_, 2);

        if (is_desktop_gl_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Helper: check if a group's texture has alpha
        auto group_has_alpha = [&](const MeshGroup& g) -> bool {
            if (!textured_) return false;
            auto it = texture_has_alpha_.find(g.texture_key);
            return it != texture_has_alpha_.end() && it->second;
        };

        // Helper: bind texture for a group and draw
        auto draw_group = [&](const MeshGroup& g) {
            bool has_tex = false;
            bool has_normal = false;
            bool has_spec = false;
            if (textured_) {
                auto it = textures_.find(g.texture_key);
                if (it != textures_.end()) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, it->second);
                    has_tex = true;
                }
                auto nit = normal_maps_.find(g.texture_key);
                if (nit != normal_maps_.end()) {
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, nit->second);
                    has_normal = true;
                }
                auto sit = specular_maps_.find(g.texture_key);
                if (sit != specular_maps_.end()) {
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, sit->second);
                    has_spec = true;
                }
            }
            glUniform1i(loc_has_texture_, has_tex ? 1 : 0);
            glUniform1i(loc_has_normal_map_, has_normal ? 1 : 0);
            glUniform1i(loc_has_specular_map_, has_spec ? 1 : 0);
            auto mit = material_params_.find(g.texture_key);
            if (mit != material_params_.end()) {
                const auto& mp = mit->second;
                glUniform1i(loc_has_material_, 1);
                glUniform3fv(loc_mat_ambient_, 1, mp.ambient);
                glUniform3fv(loc_mat_diffuse_, 1, mp.diffuse);
                glUniform3fv(loc_mat_emissive_, 1, mp.emissive);
                glUniform3fv(loc_mat_specular_, 1, mp.specular);
                glUniform1f(loc_mat_spec_power_, mp.specular_power);
                glUniform1i(loc_shader_mode_, mp.shader_mode);
            } else {
                static const float ka[3] = {0.18f, 0.18f, 0.18f};
                static const float kd[3] = {1.0f, 1.0f, 1.0f};
                static const float ke[3] = {0.0f, 0.0f, 0.0f};
                static const float ks[3] = {0.08f, 0.08f, 0.08f};
                glUniform1i(loc_has_material_, 0);
                glUniform3fv(loc_mat_ambient_, 1, ka);
                glUniform3fv(loc_mat_diffuse_, 1, kd);
                glUniform3fv(loc_mat_emissive_, 1, ke);
                glUniform3fv(loc_mat_specular_, 1, ks);
                glUniform1f(loc_mat_spec_power_, 32.0f);
                glUniform1i(loc_shader_mode_, 0);
            }
            glBindVertexArray(g.vao);
            glDrawArrays(GL_TRIANGLES, 0, g.vertex_count);
        };

        // Pass 1: Opaque groups — no blending, depth write ON
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        for (const auto& g : groups_) {
            if (group_has_alpha(g)) continue;
            draw_group(g);
        }

        // Pass 2: Transparent groups — blending ON, depth write OFF
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const auto& g : groups_) {
            if (!group_has_alpha(g)) continue;
            draw_group(g);
        }

        // Restore depth write and disable blending before wireframe pass
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Wireframe pass
    if (wireframe_ && prog_wire_) {
        glUseProgram(prog_wire_);
        glUniformMatrix4fv(loc_mvp_wire_, 1, GL_FALSE, mvp);
        glUniform3f(loc_color_wire_, 0.0f, 0.0f, 0.0f);

        if (is_desktop_gl_) {
            // Desktop GL: use glPolygonMode for wireframe
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glEnable(GL_POLYGON_OFFSET_LINE);
            glPolygonOffset(-1.0f, -1.0f);

            for (const auto& g : groups_) {
                glBindVertexArray(g.vao);
                glDrawArrays(GL_TRIANGLES, 0, g.vertex_count);
            }

            glDisable(GL_POLYGON_OFFSET_LINE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        } else if (wire_vao_ && wire_index_count_ > 0) {
            // GLES: draw wireframe using GL_LINES from the line index buffer
            glBindVertexArray(wire_vao_);
            glDrawElements(GL_LINES, wire_index_count_, GL_UNSIGNED_INT, nullptr);
        }
    }

    if (highlight_vao_ && highlight_vertex_count_ > 0 && prog_wire_) {
        glUseProgram(prog_wire_);
        glUniformMatrix4fv(loc_mvp_wire_, 1, GL_FALSE, mvp);
        glUniform3f(loc_color_wire_, 1.0f, 0.9f, 0.1f);
        glBindVertexArray(highlight_vao_);
        glDisable(GL_DEPTH_TEST);
        if (is_desktop_gl_) glPointSize(6.0f);
        GLenum draw_mode = (highlight_mode_ == HighlightMode::Lines) ? GL_LINES : GL_POINTS;
        glDrawArrays(draw_mode, 0, highlight_vertex_count_);
        if (is_desktop_gl_) glPointSize(1.0f);
        glEnable(GL_DEPTH_TEST);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (const auto& bridge = render_domain::runtime_state().ui_render_bridge) {
        bridge->render_in_current_context(get_width(), get_height());
    }

    log_gl_errors("GLModelView::on_render_gl");
    return true;
}
