#include "gl_rvmat_preview.h"

#include "gl_error_log.h"
#include "log_panel.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

static constexpr const char* kVertSrc = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aTangent;
layout(location=4) in vec2 aUV1;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec2 vUV1;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    vTangent = normalize(mat3(uModel) * aTangent);
    vUV = aUV;
    vUV1 = aUV1;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static constexpr const char* kFragSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in vec2 vUV1;
uniform sampler2D uTexDiffuse;
uniform sampler2D uTexNormal;
uniform sampler2D uTexSpec;
uniform sampler2D uTexAO;
uniform bool uHasDiffuse;
uniform bool uHasNormal;
uniform bool uHasSpec;
uniform bool uHasAO;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform vec3 uMatAmbient;
uniform vec3 uMatDiffuse;
uniform vec3 uMatEmissive;
uniform vec3 uMatSpecular;
uniform float uMatSpecPower;
uniform mat3 uUvDiffuse;
uniform mat3 uUvNormal;
uniform mat3 uUvSpec;
uniform mat3 uUvAO;
uniform int uUvSourceDiffuse;
uniform int uUvSourceNormal;
uniform int uUvSourceSpec;
uniform int uUvSourceAO;
uniform int uViewMode;
uniform bool uDiffuseIsSRGB;
out vec4 FragColor;
void main() {
    vec2 uvBaseDiff = (uUvSourceDiffuse == 1) ? vUV1 : vUV;
    vec2 uvBaseNrm = (uUvSourceNormal == 1) ? vUV1 : vUV;
    vec2 uvBaseSpec = (uUvSourceSpec == 1) ? vUV1 : vUV;
    vec2 uvBaseAO = (uUvSourceAO == 1) ? vUV1 : vUV;
    vec2 uvD = (uUvDiffuse * vec3(uvBaseDiff, 1.0)).xy;
    vec2 uvN = (uUvNormal * vec3(uvBaseNrm, 1.0)).xy;
    vec2 uvS = (uUvSpec * vec3(uvBaseSpec, 1.0)).xy;
    vec2 uvA = (uUvAO * vec3(uvBaseAO, 1.0)).xy;
    vec3 baseN = normalize(vNormal);
    vec3 t = normalize(vTangent - dot(vTangent, baseN) * baseN);
    vec3 b = normalize(cross(baseN, t));
    if (!gl_FrontFacing) {
        baseN = -baseN;
        t = -t;
        b = -b;
    }
    vec3 n = baseN;
    if (uHasNormal) {
        vec3 nTex = texture(uTexNormal, uvN).xyz * 2.0 - 1.0;
        n = normalize(mat3(t, b, baseN) * nTex);
    }

    vec3 baseColor = uHasDiffuse ? texture(uTexDiffuse, uvD).rgb : vec3(0.7);
    if (uDiffuseIsSRGB) baseColor = pow(baseColor, vec3(2.2));
    vec3 ambient = clamp(uMatAmbient, 0.0, 1.0);
    vec3 diffuseC = clamp(uMatDiffuse, 0.0, 1.0);
    vec3 emissive = clamp(uMatEmissive, 0.0, 1.0);
    vec3 specC = clamp(uMatSpecular, 0.0, 1.0);
    float sp = max(2.0, uMatSpecPower);

    float diff = max(dot(n, uLightDir), 0.0);
    float backFill = max(dot(n, -uLightDir), 0.0) * 0.20;
    vec3 v = normalize(uCamPos - vWorldPos);
    vec3 h = normalize(uLightDir + v);
    float spec = pow(max(dot(n, h), 0.0), sp);
    float specMask = 1.0;
    if (uHasSpec) specMask = dot(texture(uTexSpec, uvS).rgb, vec3(0.3333));
    vec3 aoColor = uHasAO ? texture(uTexAO, uvA).rgb : vec3(1.0);

    vec3 lit = baseColor * (ambient * 0.25 + diffuseC * min(1.0, diff + backFill))
             + specC * spec * specMask * 0.35
             + emissive;
    vec3 outColor = lit;
    if (uViewMode == 1) {
        outColor = baseColor;
        outColor = pow(clamp(outColor, 0.0, 1.0), vec3(1.0 / 2.2));
    } else if (uViewMode == 2) {
        outColor = n * 0.5 + 0.5;
    } else if (uViewMode == 3) {
        outColor = uHasSpec ? texture(uTexSpec, uvS).rgb : vec3(0.5);
    } else if (uViewMode == 4) {
        outColor = aoColor;
    } else {
        outColor = pow(clamp(outColor, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    FragColor = vec4(clamp(outColor, 0.0, 1.0), 1.0);
}
)";

static void mat4_identity(float* m) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float* out, const float* a, const float* b) {
    float t[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            t[j * 4 + i] = 0.0f;
            for (int k = 0; k < 4; ++k) t[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    }
    std::memcpy(out, t, sizeof(t));
}

static void mat4_perspective(float* m, float fov, float aspect, float znear, float zfar) {
    std::memset(m, 0, sizeof(float) * 16);
    float f = 1.0f / std::tan(fov * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static void vec3_normalize(float* v) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-8f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

static void vec3_cross(float* out, const float* a, const float* b) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
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

} // namespace

GLRvmatPreview::GLRvmatPreview() {
    set_has_depth_buffer(true);
    set_auto_render(true);
    set_hexpand(true);
    set_vexpand(true);
    set_size_request(320, 320);
    set_focusable(true);

    signal_realize().connect(sigc::mem_fun(*this, &GLRvmatPreview::on_realize_gl), false);
    signal_unrealize().connect(sigc::mem_fun(*this, &GLRvmatPreview::on_unrealize_gl), false);
    signal_render().connect(sigc::mem_fun(*this, &GLRvmatPreview::on_render_gl), false);

    drag_orbit_ = Gtk::GestureDrag::create();
    drag_orbit_->set_button(GDK_BUTTON_PRIMARY);
    drag_orbit_->signal_drag_begin().connect([this](double x, double y) {
        drag_start_x_ = x;
        drag_start_y_ = y;
        drag_start_azimuth_ = azimuth_;
        drag_start_elevation_ = elevation_;
    });
    drag_orbit_->signal_drag_update().connect([this](double dx, double dy) {
        azimuth_ = drag_start_azimuth_ - static_cast<float>(dx) * 0.004f;
        elevation_ = drag_start_elevation_ + static_cast<float>(dy) * 0.004f;
        elevation_ = std::clamp(elevation_, -1.5f, 1.5f);
        queue_render();
    });
    add_controller(drag_orbit_);

    drag_pan_ = Gtk::GestureDrag::create();
    drag_pan_->set_button(GDK_BUTTON_MIDDLE);
    drag_pan_->signal_drag_begin().connect([this](double, double) {
        std::memcpy(drag_start_pivot_, pivot_, sizeof(pivot_));
    });
    drag_pan_->signal_drag_update().connect([this](double dx, double dy) {
        float scale = distance_ * 0.002f;
        float ca = std::cos(azimuth_), sa = std::sin(azimuth_);
        float rx = ca, rz = -sa;
        float uy = 1.0f;
        pivot_[0] = drag_start_pivot_[0] - static_cast<float>(dx) * scale * rx;
        pivot_[1] = drag_start_pivot_[1] + static_cast<float>(dy) * scale * uy;
        pivot_[2] = drag_start_pivot_[2] - static_cast<float>(dx) * scale * rz;
        queue_render();
    });
    add_controller(drag_pan_);

    scroll_zoom_ = Gtk::EventControllerScroll::create();
    scroll_zoom_->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_zoom_->signal_scroll().connect([this](double, double dy) -> bool {
        distance_ *= (dy > 0) ? 1.1f : 0.9f;
        distance_ = std::max(distance_, 0.25f);
        queue_render();
        return true;
    }, false);
    add_controller(scroll_zoom_);
}

GLRvmatPreview::~GLRvmatPreview() = default;

void GLRvmatPreview::clear_material() {
    make_current();
    if (has_error()) return;
    if (tex_diff_) glDeleteTextures(1, &tex_diff_);
    if (tex_nrm_) glDeleteTextures(1, &tex_nrm_);
    if (tex_spec_) glDeleteTextures(1, &tex_spec_);
    if (tex_ao_) glDeleteTextures(1, &tex_ao_);
    tex_diff_ = tex_nrm_ = tex_spec_ = 0;
    tex_ao_ = 0;
    has_diff_ = has_nrm_ = has_spec_ = false;
    has_ao_ = false;
    uv_diff_ = {1.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 1.0f};
    uv_nrm_ = uv_diff_;
    uv_spec_ = uv_diff_;
    uv_ao_ = uv_diff_;
    uv_src_diff_ = static_cast<int>(UVSource::Tex0);
    uv_src_nrm_ = static_cast<int>(UVSource::Tex0);
    uv_src_spec_ = static_cast<int>(UVSource::Tex0);
    uv_src_ao_ = static_cast<int>(UVSource::Tex0);
    queue_render();
}

void GLRvmatPreview::set_material_params(const MaterialParams& params) {
    mat_ = params;
    queue_render();
}

void GLRvmatPreview::set_diffuse_texture(int width, int height, const uint8_t* rgba_data) {
    upload_texture(tex_diff_, width, height, rgba_data);
    has_diff_ = true;
    queue_render();
}

void GLRvmatPreview::set_normal_texture(int width, int height, const uint8_t* rgba_data) {
    upload_texture(tex_nrm_, width, height, rgba_data);
    has_nrm_ = true;
    queue_render();
}

void GLRvmatPreview::set_specular_texture(int width, int height, const uint8_t* rgba_data) {
    upload_texture(tex_spec_, width, height, rgba_data);
    has_spec_ = true;
    queue_render();
}

void GLRvmatPreview::set_ao_texture(int width, int height, const uint8_t* rgba_data) {
    upload_texture(tex_ao_, width, height, rgba_data);
    has_ao_ = true;
    queue_render();
}

void GLRvmatPreview::set_diffuse_uv_matrix(const std::array<float, 9>& m) {
    uv_diff_ = m;
    queue_render();
}

void GLRvmatPreview::set_normal_uv_matrix(const std::array<float, 9>& m) {
    uv_nrm_ = m;
    queue_render();
}

void GLRvmatPreview::set_specular_uv_matrix(const std::array<float, 9>& m) {
    uv_spec_ = m;
    queue_render();
}

void GLRvmatPreview::set_ao_uv_matrix(const std::array<float, 9>& m) {
    uv_ao_ = m;
    queue_render();
}

void GLRvmatPreview::set_diffuse_uv_source(UVSource source) {
    uv_src_diff_ = static_cast<int>(source);
    queue_render();
}

void GLRvmatPreview::set_normal_uv_source(UVSource source) {
    uv_src_nrm_ = static_cast<int>(source);
    queue_render();
}

void GLRvmatPreview::set_specular_uv_source(UVSource source) {
    uv_src_spec_ = static_cast<int>(source);
    queue_render();
}

void GLRvmatPreview::set_ao_uv_source(UVSource source) {
    uv_src_ao_ = static_cast<int>(source);
    queue_render();
}

void GLRvmatPreview::set_shape(Shape shape) {
    shape_ = shape;
    queue_render();
}

void GLRvmatPreview::set_view_mode(ViewMode mode) {
    view_mode_ = mode;
    queue_render();
}

void GLRvmatPreview::on_realize_gl() {
    make_current();
    if (has_error()) return;

    try {
        auto vs = compile_shader(GL_VERTEX_SHADER, kVertSrc);
        auto fs = compile_shader(GL_FRAGMENT_SHADER, kFragSrc);
        prog_ = link_program(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    } catch (const std::exception& e) {
        app_log(LogLevel::Error, std::string("GLRvmatPreview: ") + e.what());
        set_error(Glib::Error(GDK_GL_ERROR, 0, e.what()));
        return;
    }

    loc_mvp_ = glGetUniformLocation(prog_, "uMVP");
    loc_model_ = glGetUniformLocation(prog_, "uModel");
    loc_normal_mat_ = glGetUniformLocation(prog_, "uNormalMat");
    loc_light_dir_ = glGetUniformLocation(prog_, "uLightDir");
    loc_cam_pos_ = glGetUniformLocation(prog_, "uCamPos");
    loc_tex_diff_ = glGetUniformLocation(prog_, "uTexDiffuse");
    loc_tex_nrm_ = glGetUniformLocation(prog_, "uTexNormal");
    loc_tex_spec_ = glGetUniformLocation(prog_, "uTexSpec");
    loc_tex_ao_ = glGetUniformLocation(prog_, "uTexAO");
    loc_has_diff_ = glGetUniformLocation(prog_, "uHasDiffuse");
    loc_has_nrm_ = glGetUniformLocation(prog_, "uHasNormal");
    loc_has_spec_ = glGetUniformLocation(prog_, "uHasSpec");
    loc_has_ao_ = glGetUniformLocation(prog_, "uHasAO");
    loc_mat_ambient_ = glGetUniformLocation(prog_, "uMatAmbient");
    loc_mat_diffuse_ = glGetUniformLocation(prog_, "uMatDiffuse");
    loc_mat_emissive_ = glGetUniformLocation(prog_, "uMatEmissive");
    loc_mat_specular_ = glGetUniformLocation(prog_, "uMatSpecular");
    loc_mat_spec_power_ = glGetUniformLocation(prog_, "uMatSpecPower");
    loc_uv_diff_ = glGetUniformLocation(prog_, "uUvDiffuse");
    loc_uv_nrm_ = glGetUniformLocation(prog_, "uUvNormal");
    loc_uv_spec_ = glGetUniformLocation(prog_, "uUvSpec");
    loc_uv_ao_ = glGetUniformLocation(prog_, "uUvAO");
    loc_uv_src_diff_ = glGetUniformLocation(prog_, "uUvSourceDiffuse");
    loc_uv_src_nrm_ = glGetUniformLocation(prog_, "uUvSourceNormal");
    loc_uv_src_spec_ = glGetUniformLocation(prog_, "uUvSourceSpec");
    loc_uv_src_ao_ = glGetUniformLocation(prog_, "uUvSourceAO");
    loc_view_mode_ = glGetUniformLocation(prog_, "uViewMode");
    loc_diffuse_srgb_ = glGetUniformLocation(prog_, "uDiffuseIsSRGB");

    build_sphere_mesh();
    build_tile_mesh();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    log_gl_errors("GLRvmatPreview::on_realize_gl");
}

void GLRvmatPreview::on_unrealize_gl() {
    make_current();
    if (has_error()) return;
    cleanup_gl();
    log_gl_errors("GLRvmatPreview::on_unrealize_gl");
}

bool GLRvmatPreview::on_render_gl(const Glib::RefPtr<Gdk::GLContext>&) {
    glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool draw_tile = (shape_ == Shape::Tile);
    const uint32_t vao = draw_tile ? vao_tile_ : vao_sphere_;
    const int index_count = draw_tile ? index_count_tile_ : index_count_sphere_;
    if (!prog_ || !vao || index_count <= 0) return true;

    const float aspect = static_cast<float>(std::max(1, get_width()))
                       / static_cast<float>(std::max(1, get_height()));
    float proj[16], view[16], model[16], vp[16], mvp[16];
    mat4_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
    float ce = std::cos(elevation_), se = std::sin(elevation_);
    float ca = std::cos(azimuth_), sa = std::sin(azimuth_);
    const float eye[3] = {
        pivot_[0] + distance_ * ce * sa,
        pivot_[1] + distance_ * se,
        pivot_[2] + distance_ * ce * ca
    };
    const float center[3] = {pivot_[0], pivot_[1], pivot_[2]};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    mat4_look_at(view, eye, center, up);

    mat4_identity(model);

    mat4_mul(vp, proj, view);
    mat4_mul(mvp, vp, model);
    float normal_mat[9] = {
        model[0], model[1], model[2],
        model[4], model[5], model[6],
        model[8], model[9], model[10],
    };

    float light[3] = {0.45f, 0.7f, 0.52f};
    vec3_normalize(light);

    glUseProgram(prog_);
    glUniformMatrix4fv(loc_mvp_, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(loc_model_, 1, GL_FALSE, model);
    glUniformMatrix3fv(loc_normal_mat_, 1, GL_FALSE, normal_mat);
    glUniform3fv(loc_light_dir_, 1, light);
    glUniform3f(loc_cam_pos_, eye[0], eye[1], eye[2]);
    glUniform3fv(loc_mat_ambient_, 1, mat_.ambient);
    glUniform3fv(loc_mat_diffuse_, 1, mat_.diffuse);
    glUniform3fv(loc_mat_emissive_, 1, mat_.emissive);
    glUniform3fv(loc_mat_specular_, 1, mat_.specular);
    glUniform1f(loc_mat_spec_power_, mat_.specular_power);

    glUniform1i(loc_tex_diff_, 0);
    glUniform1i(loc_tex_nrm_, 1);
    glUniform1i(loc_tex_spec_, 2);
    glUniform1i(loc_tex_ao_, 3);
    glUniform1i(loc_has_diff_, has_diff_ ? 1 : 0);
    glUniform1i(loc_has_nrm_, has_nrm_ ? 1 : 0);
    glUniform1i(loc_has_spec_, has_spec_ ? 1 : 0);
    glUniform1i(loc_has_ao_, has_ao_ ? 1 : 0);
    glUniformMatrix3fv(loc_uv_diff_, 1, GL_FALSE, uv_diff_.data());
    glUniformMatrix3fv(loc_uv_nrm_, 1, GL_FALSE, uv_nrm_.data());
    glUniformMatrix3fv(loc_uv_spec_, 1, GL_FALSE, uv_spec_.data());
    glUniformMatrix3fv(loc_uv_ao_, 1, GL_FALSE, uv_ao_.data());
    glUniform1i(loc_uv_src_diff_, uv_src_diff_);
    glUniform1i(loc_uv_src_nrm_, uv_src_nrm_);
    glUniform1i(loc_uv_src_spec_, uv_src_spec_);
    glUniform1i(loc_uv_src_ao_, uv_src_ao_);
    glUniform1i(loc_view_mode_, static_cast<int>(view_mode_));
    glUniform1i(loc_diffuse_srgb_, diffuse_is_srgb_ ? 1 : 0);

    if (has_diff_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_diff_);
    }
    if (has_nrm_) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_nrm_);
    }
    if (has_spec_) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex_spec_);
    }
    if (has_ao_) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, tex_ao_);
    }

    if (draw_tile) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
    }

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    log_gl_errors("GLRvmatPreview::on_render_gl");
    return true;
}

void GLRvmatPreview::cleanup_gl() {
    if (vao_sphere_) glDeleteVertexArrays(1, &vao_sphere_);
    if (vbo_sphere_) glDeleteBuffers(1, &vbo_sphere_);
    if (ebo_sphere_) glDeleteBuffers(1, &ebo_sphere_);
    if (vao_tile_) glDeleteVertexArrays(1, &vao_tile_);
    if (vbo_tile_) glDeleteBuffers(1, &vbo_tile_);
    if (ebo_tile_) glDeleteBuffers(1, &ebo_tile_);
    if (tex_diff_) glDeleteTextures(1, &tex_diff_);
    if (tex_nrm_) glDeleteTextures(1, &tex_nrm_);
    if (tex_spec_) glDeleteTextures(1, &tex_spec_);
    if (tex_ao_) glDeleteTextures(1, &tex_ao_);
    if (prog_) glDeleteProgram(prog_);
    vao_sphere_ = vbo_sphere_ = ebo_sphere_ = 0;
    vao_tile_ = vbo_tile_ = ebo_tile_ = 0;
    tex_diff_ = tex_nrm_ = tex_spec_ = tex_ao_ = 0;
    prog_ = 0;
    index_count_sphere_ = 0;
    index_count_tile_ = 0;
    has_diff_ = has_nrm_ = has_spec_ = has_ao_ = false;
}

uint32_t GLRvmatPreview::compile_shader(uint32_t type, const char* src) {
    uint32_t s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(s, sizeof(log), &len, log);
        glDeleteShader(s);
        app_log(LogLevel::Error, std::string("GLRvmatPreview shader compile failed: ") + log);
        throw std::runtime_error(std::string("RVMat preview shader compile failed: ") + log);
    }
    return s;
}

uint32_t GLRvmatPreview::link_program(uint32_t vs, uint32_t fs) {
    uint32_t p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, sizeof(log), &len, log);
        glDeleteProgram(p);
        app_log(LogLevel::Error, std::string("GLRvmatPreview program link failed: ") + log);
        throw std::runtime_error(std::string("RVMat preview program link failed: ") + log);
    }
    return p;
}

void GLRvmatPreview::upload_texture(uint32_t& tex, int width, int height, const uint8_t* rgba_data) {
    make_current();
    if (has_error() || width <= 0 || height <= 0 || !rgba_data) return;
    if (tex) glDeleteTextures(1, &tex);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    log_gl_errors("GLRvmatPreview::upload_texture");
}

void GLRvmatPreview::build_sphere_mesh() {
    constexpr int seg_u = 48;
    constexpr int seg_v = 24;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve((seg_u + 1) * (seg_v + 1));
    idx.reserve(seg_u * seg_v * 6);

    for (int y = 0; y <= seg_v; ++y) {
        float v = static_cast<float>(y) / seg_v;
        float theta = v * 3.14159265f;
        float st = std::sin(theta);
        float ct = std::cos(theta);
        for (int x = 0; x <= seg_u; ++x) {
            float u = static_cast<float>(x) / seg_u;
            float phi = u * 2.0f * 3.14159265f;
            float sp = std::sin(phi);
            float cp = std::cos(phi);

            Vertex vt{};
            vt.p[0] = st * cp;
            vt.p[1] = ct;
            vt.p[2] = st * sp;
            vt.n[0] = vt.p[0];
            vt.n[1] = vt.p[1];
            vt.n[2] = vt.p[2];
            vt.uv[0] = u;
            vt.uv[1] = v;
            vt.uv1[0] = u;
            vt.uv1[1] = v;
            vt.t[0] = -sp;
            vt.t[1] = 0.0f;
            vt.t[2] = cp;
            verts.push_back(vt);
        }
    }

    for (int y = 0; y < seg_v; ++y) {
        for (int x = 0; x < seg_u; ++x) {
            uint32_t a = static_cast<uint32_t>(y * (seg_u + 1) + x);
            uint32_t b = static_cast<uint32_t>(a + seg_u + 1);
            uint32_t c = static_cast<uint32_t>(a + 1);
            uint32_t d = static_cast<uint32_t>(b + 1);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(c); idx.push_back(b); idx.push_back(d);
        }
    }

    glGenVertexArrays(1, &vao_sphere_);
    glGenBuffers(1, &vbo_sphere_);
    glGenBuffers(1, &ebo_sphere_);
    glBindVertexArray(vao_sphere_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_sphere_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_sphere_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, p)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, n)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, t)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv1)));
    glBindVertexArray(0);

    index_count_sphere_ = static_cast<int>(idx.size());
}

void GLRvmatPreview::build_tile_mesh() {
    std::array<Vertex, 4> verts{};
    verts[0] = {{-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    verts[1] = {{1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f},
                {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    verts[2] = {{-1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f},
                {0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    verts[3] = {{1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f},
                {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    std::array<uint32_t, 6> idx = {0, 1, 2, 2, 1, 3};

    glGenVertexArrays(1, &vao_tile_);
    glGenBuffers(1, &vbo_tile_);
    glGenBuffers(1, &ebo_tile_);
    glBindVertexArray(vao_tile_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_tile_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_tile_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, p)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, n)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, t)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv1)));
    glBindVertexArray(0);

    index_count_tile_ = static_cast<int>(idx.size());
}
