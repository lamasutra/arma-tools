#include "render_domain/rd_ui_render_bridge.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace render_domain {

namespace {

class BaseUiRenderBridge : public IUiRenderBridge {
public:
    explicit BaseUiRenderBridge(UiRenderBridgeInfo info)
        : info_(std::move(info)) {
        bridge_.struct_size = sizeof(ui_render_bridge_v1);
        bridge_.abi_version = UI_RENDER_BRIDGE_ABI_VERSION;
        bridge_.userdata = this;
        bridge_.begin_frame = &BaseUiRenderBridge::bridge_begin_frame;
        bridge_.submit_draw_data = &BaseUiRenderBridge::bridge_submit_draw_data;
        bridge_.draw_overlay = &BaseUiRenderBridge::bridge_draw_overlay;
        bridge_.end_frame = &BaseUiRenderBridge::bridge_end_frame;
        bridge_.is_available = &BaseUiRenderBridge::bridge_is_available;
        bridge_.bridge_name = &BaseUiRenderBridge::bridge_name;
        bridge_.renderer_backend = &BaseUiRenderBridge::renderer_backend;
        bridge_.reason = &BaseUiRenderBridge::reason;
    }

    [[nodiscard]] UiRenderBridgeInfo info() const override {
        return info_;
    }

    [[nodiscard]] const ui_render_bridge_v1* bridge_abi() const override {
        return &bridge_;
    }

protected:
    static int bridge_begin_frame(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->begin_frame();
    }

    static int bridge_draw_overlay(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->draw_overlay();
    }

    static int bridge_submit_draw_data(void* userdata, const ui_draw_data_v1* draw_data) {
        return static_cast<BaseUiRenderBridge*>(userdata)->submit_draw_data(draw_data);
    }

    static int bridge_end_frame(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->end_frame();
    }

    static uint8_t bridge_is_available(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->info().available ? 1 : 0;
    }

    static const char* bridge_name(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->info().bridge_name.c_str();
    }

    static const char* renderer_backend(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->info().renderer_backend.c_str();
    }

    static const char* reason(void* userdata) {
        return static_cast<BaseUiRenderBridge*>(userdata)->info().reason.c_str();
    }

    UiRenderBridgeInfo info_;
    ui_render_bridge_v1 bridge_{};
};

class UnavailableUiRenderBridge final : public BaseUiRenderBridge {
public:
    explicit UnavailableUiRenderBridge(UiRenderBridgeInfo info)
        : BaseUiRenderBridge(std::move(info)) {}

    int begin_frame() override { return RD_STATUS_NOT_IMPLEMENTED; }
    int submit_draw_data(const ui_draw_data_v1*) override { return RD_STATUS_NOT_IMPLEMENTED; }
    int draw_overlay() override { return RD_STATUS_NOT_IMPLEMENTED; }
    int end_frame() override { return RD_STATUS_NOT_IMPLEMENTED; }
    int render_in_current_context(int, int) override { return RD_STATUS_NOT_IMPLEMENTED; }
};

class GlesUiRenderBridge final : public BaseUiRenderBridge {
public:
    explicit GlesUiRenderBridge(UiRenderBridgeInfo info)
        : BaseUiRenderBridge(std::move(info)) {}

    int begin_frame() override {
        frame_open_ = true;
        return RD_STATUS_OK;
    }

    int submit_draw_data(const ui_draw_data_v1* draw_data) override {
        if (!draw_data || draw_data->struct_size < sizeof(ui_draw_data_v1)) {
            return RD_STATUS_INVALID_ARGUMENT;
        }

        vertices_.clear();
        indices_.clear();
        commands_.clear();

        if (draw_data->vertex_count > 0) {
            if (!draw_data->vertices) return RD_STATUS_INVALID_ARGUMENT;
            vertices_.assign(draw_data->vertices, draw_data->vertices + draw_data->vertex_count);
        }
        if (draw_data->index_count > 0) {
            if (!draw_data->indices) return RD_STATUS_INVALID_ARGUMENT;
            indices_.assign(draw_data->indices, draw_data->indices + draw_data->index_count);
        }
        if (draw_data->command_count > 0) {
            if (!draw_data->commands) return RD_STATUS_INVALID_ARGUMENT;
            commands_.assign(draw_data->commands, draw_data->commands + draw_data->command_count);
        }

        if (!commands_.empty() && indices_.empty()) {
            return RD_STATUS_INVALID_ARGUMENT;
        }
        for (auto& cmd : commands_) {
            if (cmd.idx_offset > indices_.size()) {
                return RD_STATUS_INVALID_ARGUMENT;
            }
            const size_t cmd_end = static_cast<size_t>(cmd.idx_offset) + cmd.elem_count;
            if (cmd_end > indices_.size()) {
                return RD_STATUS_INVALID_ARGUMENT;
            }
            if (cmd.vtx_offset != 0) {
                for (size_t idx = static_cast<size_t>(cmd.idx_offset); idx < cmd_end; ++idx) {
                    const uint32_t resolved = static_cast<uint32_t>(indices_[idx]) + cmd.vtx_offset;
                    if (resolved >= vertices_.size()) {
                        return RD_STATUS_INVALID_ARGUMENT;
                    }
                    indices_[idx] = static_cast<uint16_t>(resolved);
                }
                cmd.vtx_offset = 0;
            }
        }

        has_draw_data_ = !commands_.empty();
        return RD_STATUS_OK;
    }

    int draw_overlay() override {
        if (!frame_open_) return RD_STATUS_OK;
        overlay_requested_ = true;
        return RD_STATUS_OK;
    }

    int end_frame() override {
        frame_open_ = false;
        return RD_STATUS_OK;
    }

    int render_in_current_context(int viewport_width, int viewport_height) override {
        if (!overlay_requested_) return RD_STATUS_OK;
        if (viewport_width <= 0 || viewport_height <= 0) return RD_STATUS_INVALID_ARGUMENT;

        const int margin = 12;
        const int outer_w = std::clamp(viewport_width / 3, 140, 280);
        const int outer_h = std::clamp(viewport_height / 6, 52, 120);
        const int outer_x = margin;
        const int outer_y = std::max(0, viewport_height - margin - outer_h);

        GLint prev_scissor_box[4] = {0, 0, 0, 0};
        GLfloat prev_clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        GLint prev_program = 0;
        GLint prev_array_buffer = 0;
        GLint prev_element_array_buffer = 0;
        GLint prev_active_texture = 0;
        GLint prev_texture_2d = 0;
        GLint prev_blend_src_rgb = 0;
        GLint prev_blend_dst_rgb = 0;
        GLint prev_blend_src_alpha = 0;
        GLint prev_blend_dst_alpha = 0;
        GLint prev_blend_eq_rgb = 0;
        GLint prev_blend_eq_alpha = 0;
        GLint prev_pos_attrib_enabled = 0;
        GLint prev_color_attrib_enabled = 0;
        const GLboolean had_scissor = glIsEnabled(GL_SCISSOR_TEST);
        const GLboolean had_blend = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_SCISSOR_BOX, prev_scissor_box);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear_color);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_array_buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_texture_2d);
        glGetIntegerv(GL_BLEND_SRC_RGB, &prev_blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &prev_blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &prev_blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &prev_blend_dst_alpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &prev_blend_eq_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prev_blend_eq_alpha);

        bool pipeline_used = false;
        if (has_draw_data_) {
            const bool pipeline_ready = ensure_pipeline();
            if (pipeline_ready) {
                pipeline_used = true;
                glEnable(GL_BLEND);
                glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                    GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_SCISSOR_TEST);

                glUseProgram(program_);
                glUniform2f(inv_viewport_loc_,
                            1.0f / static_cast<float>(viewport_width),
                            1.0f / static_cast<float>(viewport_height));
                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(vertices_.size() * sizeof(ui_vertex_v1)),
                             vertices_.data(),
                             GL_STREAM_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(indices_.size() * sizeof(uint16_t)),
                             indices_.data(),
                             GL_STREAM_DRAW);

                glGetVertexAttribiv(static_cast<GLuint>(pos_loc_),
                                    GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                                    &prev_pos_attrib_enabled);
                glGetVertexAttribiv(static_cast<GLuint>(color_loc_),
                                    GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                                    &prev_color_attrib_enabled);
                glEnableVertexAttribArray(static_cast<GLuint>(pos_loc_));
                glEnableVertexAttribArray(static_cast<GLuint>(color_loc_));
                glVertexAttribPointer(static_cast<GLuint>(pos_loc_),
                                      2, GL_FLOAT, GL_FALSE,
                                      sizeof(ui_vertex_v1),
                                      reinterpret_cast<const void*>(offsetof(ui_vertex_v1, x)));
                glVertexAttribPointer(static_cast<GLuint>(color_loc_),
                                      4, GL_UNSIGNED_BYTE, GL_TRUE,
                                      sizeof(ui_vertex_v1),
                                      reinterpret_cast<const void*>(offsetof(ui_vertex_v1, color_rgba8)));

                for (const auto& cmd : commands_) {
                    if (cmd.elem_count == 0) continue;
                    if (static_cast<size_t>(cmd.idx_offset) + cmd.elem_count > indices_.size()) {
                        continue;
                    }

                    const int clip_x1 = static_cast<int>(std::floor(cmd.clip_rect_x1));
                    const int clip_y1 = static_cast<int>(std::floor(cmd.clip_rect_y1));
                    const int clip_x2 = static_cast<int>(std::ceil(cmd.clip_rect_x2));
                    const int clip_y2 = static_cast<int>(std::ceil(cmd.clip_rect_y2));
                    const int w = std::max(0, std::min(viewport_width, clip_x2) - std::max(0, clip_x1));
                    const int h_top = std::max(0, std::min(viewport_height, clip_y2) - std::max(0, clip_y1));
                    if (w <= 0 || h_top <= 0) continue;
                    const int x = std::max(0, clip_x1);
                    const int y = std::max(0, viewport_height - std::max(0, clip_y2));

                    glScissor(x, y, w, h_top);
                    glDrawElements(
                        GL_TRIANGLES,
                        static_cast<GLsizei>(cmd.elem_count),
                        GL_UNSIGNED_SHORT,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(cmd.idx_offset * sizeof(uint16_t))));
                }
            } else {
                glEnable(GL_SCISSOR_TEST);
                glScissor(outer_x, outer_y, outer_w, outer_h);
                glClearColor(0.56f, 0.12f, 0.08f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        } else {
            // Fallback debug panel.
            glEnable(GL_SCISSOR_TEST);
            glScissor(outer_x, outer_y, outer_w, outer_h);
            glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            const int inset = 2;
            glScissor(outer_x + inset, outer_y + inset,
                      std::max(1, outer_w - inset * 2),
                      std::max(1, outer_h - inset * 2));
            glClearColor(0.96f, 0.34f, 0.16f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        if (pipeline_used) {
            if (prev_pos_attrib_enabled == 0) {
                glDisableVertexAttribArray(static_cast<GLuint>(pos_loc_));
            }
            if (prev_color_attrib_enabled == 0) {
                glDisableVertexAttribArray(static_cast<GLuint>(color_loc_));
            }
        }
        const GLuint restore_program = static_cast<GLuint>(std::max(prev_program, 0));
        const GLuint restore_array_buffer = static_cast<GLuint>(std::max(prev_array_buffer, 0));
        const GLuint restore_element_buffer = static_cast<GLuint>(std::max(prev_element_array_buffer, 0));
        const GLuint restore_texture_2d = static_cast<GLuint>(std::max(prev_texture_2d, 0));
        auto to_glenum = [](GLint value, GLenum fallback) -> GLenum {
            return value > 0 ? static_cast<GLenum>(value) : fallback;
        };

        glUseProgram(restore_program);
        glBindBuffer(GL_ARRAY_BUFFER, restore_array_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, restore_element_buffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, restore_texture_2d);
        glActiveTexture(to_glenum(prev_active_texture, GL_TEXTURE0));
        glBlendEquationSeparate(
            to_glenum(prev_blend_eq_rgb, GL_FUNC_ADD),
            to_glenum(prev_blend_eq_alpha, GL_FUNC_ADD));
        glBlendFuncSeparate(
            to_glenum(prev_blend_src_rgb, GL_SRC_ALPHA),
            to_glenum(prev_blend_dst_rgb, GL_ONE_MINUS_SRC_ALPHA),
            to_glenum(prev_blend_src_alpha, GL_ONE),
            to_glenum(prev_blend_dst_alpha, GL_ONE_MINUS_SRC_ALPHA));
        if (!had_blend) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
        }
        glClearColor(prev_clear_color[0], prev_clear_color[1],
                     prev_clear_color[2], prev_clear_color[3]);
        glScissor(prev_scissor_box[0], prev_scissor_box[1],
                  prev_scissor_box[2], prev_scissor_box[3]);
        if (!had_scissor) {
            glDisable(GL_SCISSOR_TEST);
        }

        overlay_requested_ = false;
        return RD_STATUS_OK;
    }

private:
    bool frame_open_ = false;
    bool overlay_requested_ = false;
    bool has_draw_data_ = false;
    std::vector<ui_vertex_v1> vertices_;
    std::vector<uint16_t> indices_;
    std::vector<ui_draw_cmd_v1> commands_;
    GLuint program_ = 0;
    GLuint vertex_shader_ = 0;
    GLuint fragment_shader_ = 0;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLint pos_loc_ = -1;
    GLint color_loc_ = -1;
    GLint inv_viewport_loc_ = -1;

    bool ensure_pipeline() {
        if (program_ != 0) return true;

        static const char* k_vs = R"(
            #ifdef GL_ES
            precision mediump float;
            #endif
            attribute vec2 a_pos;
            attribute vec4 a_color;
            uniform vec2 u_inv_viewport;
            varying vec4 v_color;
            void main() {
                vec2 ndc = (a_pos * u_inv_viewport) * 2.0 - 1.0;
                gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
                v_color = a_color;
            }
        )";
        static const char* k_fs = R"(
            #ifdef GL_ES
            precision mediump float;
            #endif
            varying vec4 v_color;
            void main() {
                gl_FragColor = v_color;
            }
        )";

        vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
        fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
        if (!vertex_shader_ || !fragment_shader_) return false;

        glShaderSource(vertex_shader_, 1, &k_vs, nullptr);
        glCompileShader(vertex_shader_);
        GLint vs_ok = 0;
        glGetShaderiv(vertex_shader_, GL_COMPILE_STATUS, &vs_ok);
        if (!vs_ok) return false;

        glShaderSource(fragment_shader_, 1, &k_fs, nullptr);
        glCompileShader(fragment_shader_);
        GLint fs_ok = 0;
        glGetShaderiv(fragment_shader_, GL_COMPILE_STATUS, &fs_ok);
        if (!fs_ok) return false;

        program_ = glCreateProgram();
        if (!program_) return false;
        glAttachShader(program_, vertex_shader_);
        glAttachShader(program_, fragment_shader_);
        glLinkProgram(program_);
        GLint link_ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &link_ok);
        if (!link_ok) return false;

        pos_loc_ = glGetAttribLocation(program_, "a_pos");
        color_loc_ = glGetAttribLocation(program_, "a_color");
        inv_viewport_loc_ = glGetUniformLocation(program_, "u_inv_viewport");
        if (pos_loc_ < 0 || color_loc_ < 0 || inv_viewport_loc_ < 0) return false;

        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ibo_);
        if (!vbo_ || !ibo_) return false;
        return true;
    }
};

}  // namespace

std::shared_ptr<IUiRenderBridge> make_ui_render_bridge_for_backend(const std::string& backend_id) {
    UiRenderBridgeInfo info;
    info.renderer_backend = backend_id;

    if (backend_id == "gles") {
        info.bridge_name = "gles-ui-bridge";
        info.available = true;
        info.reason = "GLES bridge active";
        return std::make_shared<GlesUiRenderBridge>(std::move(info));
    }

    if (backend_id == "null") {
        info.bridge_name = "null-ui-bridge";
        info.available = false;
        info.reason = "Renderer backend is null";
        return std::make_shared<UnavailableUiRenderBridge>(std::move(info));
    }

    info.bridge_name = "unsupported-ui-bridge";
    info.available = false;
    info.reason = "No UI render bridge implementation for selected renderer backend";
    return std::make_shared<UnavailableUiRenderBridge>(std::move(info));
}

}  // namespace render_domain
