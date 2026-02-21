#include "gl_model_view.h"

#include "log_panel.h"

#include <armatools/armapath.h>

#include <epoxy/gl.h>
#include <cmath>
#include <cstring>
#include <algorithm>

// ---- Shaders ----

static constexpr const char* VERT_SRC = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(uNormalMat * aNormal);
    vUV = aUV;
}
)";

static constexpr const char* FRAG_SOLID_SRC = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform vec3 uLightDir;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, uLightDir), 0.0);
    float light = 0.3 + 0.7 * diff;
    vec4 baseColor = uHasTexture ? texture(uTexture, vUV) : vec4(0.7, 0.7, 0.7, 1.0);
    FragColor = vec4(baseColor.rgb * light, baseColor.a);
    if (FragColor.a < 0.01) discard;
}
)";

static constexpr const char* FRAG_WIRE_SRC = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)";

// ---- GLES 3.2 shader variants ----

static constexpr const char* VERT_ES_SRC = R"(
#version 320 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(uNormalMat * aNormal);
    vUV = aUV;
}
)";

static constexpr const char* FRAG_SOLID_ES_SRC = R"(
#version 320 es
precision mediump float;
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform vec3 uLightDir;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, uLightDir), 0.0);
    float light = 0.3 + 0.7 * diff;
    vec4 baseColor = uHasTexture ? texture(uTexture, vUV) : vec4(0.7, 0.7, 0.7, 1.0);
    FragColor = vec4(baseColor.rgb * light, baseColor.a);
    if (FragColor.a < 0.01) discard;
}
)";

static constexpr const char* FRAG_WIRE_ES_SRC = R"(
#version 320 es
precision mediump float;
uniform vec3 uColor;
out vec4 FragColor;
void main() { FragColor = vec4(uColor, 1.0); }
)";

// ---- Wireframe vertex shader (positions only, for GLES line buffer) ----

static constexpr const char* VERT_WIRE_SRC = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static constexpr const char* VERT_WIRE_ES_SRC = R"(
#version 320 es
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

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

    signal_realize().connect(sigc::mem_fun(*this, &GLModelView::on_realize_gl), false);
    signal_unrealize().connect(sigc::mem_fun(*this, &GLModelView::on_unrealize_gl), false);
    signal_render().connect(sigc::mem_fun(*this, &GLModelView::on_render_gl), false);

    // Orbit drag (button 1)
    drag_orbit_ = Gtk::GestureDrag::create();
    drag_orbit_->set_button(GDK_BUTTON_PRIMARY);
    drag_orbit_->signal_drag_begin().connect([this](double x, double y) {
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_start_azimuth_ = azimuth_;
        drag_start_elevation_ = elevation_;
    });
    drag_orbit_->signal_drag_update().connect([this](double dx, double dy) {
        azimuth_ = drag_start_azimuth_ - static_cast<float>(dx) * 0.01f;
        elevation_ = drag_start_elevation_ + static_cast<float>(dy) * 0.01f;
        elevation_ = std::clamp(elevation_, -1.5f, 1.5f);
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
    });
    add_controller(drag_orbit_);

    // Pan drag (button 2 / middle)
    drag_pan_ = Gtk::GestureDrag::create();
    drag_pan_->set_button(GDK_BUTTON_MIDDLE);
    drag_pan_->signal_drag_begin().connect([this](double, double) {
        std::memcpy(drag_start_pivot_, pivot_, sizeof(pivot_));
    });
    drag_pan_->signal_drag_update().connect([this](double dx, double dy) {
        float scale = distance_ * 0.002f;
        // Pan in screen-space: right and up
        float ca = std::cos(azimuth_), sa = std::sin(azimuth_);
        // Right vector (horizontal)
        float rx = ca, rz = -sa;
        // Up vector (approximation for small elevation)
        float uy = 1.0f;
        pivot_[0] = drag_start_pivot_[0] - static_cast<float>(dx) * scale * rx;
        pivot_[1] = drag_start_pivot_[1] + static_cast<float>(dy) * scale * uy;
        pivot_[2] = drag_start_pivot_[2] - static_cast<float>(dx) * scale * rz;
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
    });
    add_controller(drag_pan_);

    // Scroll zoom
    scroll_zoom_ = Gtk::EventControllerScroll::create();
    scroll_zoom_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_zoom_->signal_scroll().connect([this](double, double dy) -> bool {
        distance_ *= (dy > 0) ? 1.1f : 0.9f;
        distance_ = std::max(distance_, 0.01f);
        queue_render();
        if (!suppress_camera_signal_) signal_camera_changed_.emit();
        return true;
    }, false);
    add_controller(scroll_zoom_);
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
    const char* vert = is_desktop_gl_ ? VERT_SRC : VERT_ES_SRC;
    const char* frag_solid = is_desktop_gl_ ? FRAG_SOLID_SRC : FRAG_SOLID_ES_SRC;
    const char* frag_wire = is_desktop_gl_ ? FRAG_WIRE_SRC : FRAG_WIRE_ES_SRC;
    const char* vert_wire = is_desktop_gl_ ? VERT_WIRE_SRC : VERT_WIRE_ES_SRC;

    // Compile shaders
    auto vs = compile_shader(GL_VERTEX_SHADER, vert);
    auto fs_solid = compile_shader(GL_FRAGMENT_SHADER, frag_solid);
    auto fs_wire = compile_shader(GL_FRAGMENT_SHADER, frag_wire);
    auto vs_wire = compile_shader(GL_VERTEX_SHADER, vert_wire);

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
    loc_has_texture_ = glGetUniformLocation(prog_solid_, "uHasTexture");
    loc_light_dir_ = glGetUniformLocation(prog_solid_, "uLightDir");
    loc_mvp_wire_ = glGetUniformLocation(prog_wire_, "uMVP");
    loc_color_wire_ = glGetUniformLocation(prog_wire_, "uColor");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);  // X is negated to fix P3D left-handed mirror, which flips winding

    // Build grid and axis geometry
    build_grid_and_axis();
}

void GLModelView::on_unrealize_gl() {
    make_current();
    if (has_error()) return;
    cleanup_gl();
}

void GLModelView::cleanup_gl() {
    for (auto& g : groups_) {
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
    }
    groups_.clear();

    for (auto& [key, tex] : textures_)
        glDeleteTextures(1, &tex);
    textures_.clear();
    texture_has_alpha_.clear();

    if (wire_vao_) { glDeleteVertexArrays(1, &wire_vao_); wire_vao_ = 0; }
    if (wire_vbo_) { glDeleteBuffers(1, &wire_vbo_); wire_vbo_ = 0; }
    if (wire_ebo_) { glDeleteBuffers(1, &wire_ebo_); wire_ebo_ = 0; }
    wire_index_count_ = 0;

    if (grid_vao_) { glDeleteVertexArrays(1, &grid_vao_); grid_vao_ = 0; }
    if (grid_vbo_) { glDeleteBuffers(1, &grid_vbo_); grid_vbo_ = 0; }
    grid_line_count_ = 0;

    if (axis_vao_) { glDeleteVertexArrays(1, &axis_vao_); axis_vao_ = 0; }
    if (axis_vbo_) { glDeleteBuffers(1, &axis_vbo_); axis_vbo_ = 0; }

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

void GLModelView::set_lod(const armatools::p3d::LOD& lod) {
    make_current();
    if (has_error()) return;

    // Clear old mesh data
    for (auto& g : groups_) {
        if (g.vao) glDeleteVertexArrays(1, &g.vao);
        if (g.vbo) glDeleteBuffers(1, &g.vbo);
    }
    groups_.clear();

    // Group faces by texture (normalized key for case-insensitive matching)
    std::unordered_map<std::string, std::vector<float>> grouped_verts;

    for (const auto& face : lod.face_data) {
        const auto& fvs = face.vertices;
        if (fvs.size() < 3) continue;

        auto tex_key = armatools::armapath::to_slash_lower(face.texture);
        auto& buf = grouped_verts[tex_key];

        // Triangulate: fan from vertex 0
        for (size_t i = 1; i + 1 < fvs.size(); i++) {
            const size_t tri[3] = {0, i, i + 1};
            for (auto vi : tri) {
                const auto& fv = fvs[vi];

                // Position (negate X to convert from P3D left-handed to GL right-handed)
                if (fv.point_index < lod.vertices.size()) {
                    const auto& p = lod.vertices[fv.point_index];
                    buf.push_back(-p[0]);
                    buf.push_back(p[1]);
                    buf.push_back(p[2]);
                } else {
                    buf.push_back(0); buf.push_back(0); buf.push_back(0);
                }

                // Normal (negate X to match the coordinate flip)
                if (fv.normal_index >= 0 &&
                    static_cast<size_t>(fv.normal_index) < lod.normals.size()) {
                    const auto& n = lod.normals[static_cast<size_t>(fv.normal_index)];
                    buf.push_back(-n[0]);
                    buf.push_back(n[1]);
                    buf.push_back(n[2]);
                } else {
                    buf.push_back(0); buf.push_back(1); buf.push_back(0);
                }

                // UV (pass through raw; GL's bottom-up convention cancels P3D's top-down UVs)
                float u = fv.uv[0];
                float v = fv.uv[1];

                // Sanitize NaN / infinity
                if (!std::isfinite(u)) u = 0.0f;
                if (!std::isfinite(v)) v = 0.0f;

                buf.push_back(u);
                buf.push_back(v);
            }
        }
    }

    // Upload each group
    for (auto& [tex_key, verts] : grouped_verts) {
        MeshGroup g;
        g.texture_key = tex_key;
        g.vertex_count = static_cast<int>(verts.size()) / 8;

        glGenVertexArrays(1, &g.vao);
        glGenBuffers(1, &g.vbo);

        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);

        // aPos (location 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(0));
        // aNormal (location 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        // aUV (location 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(6 * sizeof(float)));

        glBindVertexArray(0);
        groups_.push_back(std::move(g));
    }

    // Build wireframe line buffer for GLES path
    if (wire_vao_) { glDeleteVertexArrays(1, &wire_vao_); wire_vao_ = 0; }
    if (wire_vbo_) { glDeleteBuffers(1, &wire_vbo_); wire_vbo_ = 0; }
    if (wire_ebo_) { glDeleteBuffers(1, &wire_ebo_); wire_ebo_ = 0; }
    wire_index_count_ = 0;

    if (!is_desktop_gl_) {
        // Collect all triangle positions into a flat buffer
        std::vector<float> all_positions;
        for (const auto& [tex_key, verts] : grouped_verts) {
            size_t vert_count = verts.size() / 8;
            for (size_t i = 0; i < vert_count; i++) {
                all_positions.push_back(verts[i * 8 + 0]);
                all_positions.push_back(verts[i * 8 + 1]);
                all_positions.push_back(verts[i * 8 + 2]);
            }
        }

        size_t total_verts = all_positions.size() / 3;
        size_t total_triangles = total_verts / 3;

        // Build line indices: for each triangle (v0,v1,v2), emit edges v0-v1, v1-v2, v2-v0
        std::vector<uint32_t> line_indices;
        line_indices.reserve(total_triangles * 6);
        for (size_t t = 0; t < total_triangles; t++) {
            uint32_t base = static_cast<uint32_t>(t * 3);
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
    azimuth_ = 0.4f;
    elevation_ = 0.3f;
    queue_render();
}

void GLModelView::set_camera_from_bounds(float cx, float cy, float cz, float radius) {
    pivot_[0] = cx;
    pivot_[1] = cy;
    pivot_[2] = cz;
    distance_ = std::max(radius * 2.0f, 0.5f);
    azimuth_ = 0.4f;
    elevation_ = 0.3f;
    queue_render();
}

GLModelView::CameraState GLModelView::get_camera_state() const {
    CameraState s;
    s.azimuth = azimuth_;
    s.elevation = elevation_;
    s.distance = distance_;
    std::memcpy(s.pivot, pivot_, sizeof(pivot_));
    return s;
}

void GLModelView::set_camera_state(const CameraState& state) {
    suppress_camera_signal_ = true;
    azimuth_ = state.azimuth;
    elevation_ = state.elevation;
    distance_ = state.distance;
    std::memcpy(pivot_, state.pivot, sizeof(pivot_));
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

void GLModelView::set_highlight_faces(const std::vector<uint32_t>& face_indices) {
    highlighted_faces_ = face_indices;
    queue_render();
}

void GLModelView::build_matrices(float* mvp, float* normal_mat) {
    float eye[3];
    float ce = std::cos(elevation_), se = std::sin(elevation_);
    float ca = std::cos(azimuth_), sa = std::sin(azimuth_);
    eye[0] = pivot_[0] + distance_ * ce * sa;
    eye[1] = pivot_[1] + distance_ * se;
    eye[2] = pivot_[2] + distance_ * ce * ca;

    float up[3] = {0, 1, 0};

    float view[16];
    mat4_look_at(view, eye, pivot_, up);

    int w = get_width();
    int h = get_height();
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    float far_plane = std::max(distance_ * 10.0f, 100.0f);

    float proj[16];
    mat4_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, far_plane);

    mat4_multiply(mvp, proj, view);
    mat3_normal_from_mat4(normal_mat, view);
}

bool GLModelView::on_render_gl(const Glib::RefPtr<Gdk::GLContext>&) {
    glClearColor(bg_color_[0], bg_color_[1], bg_color_[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float mvp[16], normal_mat[9];
    build_matrices(mvp, normal_mat);

    // Draw grid and axis before the model
    draw_grid_and_axis(mvp);

    if (!has_geometry_ || !prog_solid_) return true;

    // Light direction (normalized, world space — from upper-right-front)
    float light_dir[3] = {0.4f, 0.7f, 0.5f};
    vec3_normalize(light_dir);

    // Common solid shader setup
    glUseProgram(prog_solid_);
    glUniformMatrix4fv(loc_mvp_solid_, 1, GL_FALSE, mvp);
    glUniformMatrix3fv(loc_normal_mat_, 1, GL_FALSE, normal_mat);
    glUniform3fv(loc_light_dir_, 1, light_dir);
    glUniform1i(loc_texture_, 0);

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
        if (textured_) {
            auto it = textures_.find(g.texture_key);
            if (it != textures_.end()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, it->second);
                has_tex = true;
            }
        }
        glUniform1i(loc_has_texture_, has_tex ? 1 : 0);
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

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}
