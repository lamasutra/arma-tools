#pragma once

#include "domain/gl_model_camera_types.h"
#include "render_domain/rd_backend_abi.h"

#include <gtkmm.h>
#include <armatools/p3d.h>
#include <sigc++/signal.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace render_domain {

// ModelViewWidget is the GTK wrapper for the 3D renderer backend.
//
// It serves as a bridge between the frontend UI (ModelViewPanel) and the
// raw C-ABI renderer (`rd_scene_blob_v1`).
//
// How it works:
//   - When `set_lods` or `set_scene_blob` is called, it takes the Arma 3 model data and
//     translates it into the abstract `rd_scene_blob_v1` format.
//   - It then passes this blob across the ABI to the active renderer (e.g. GLES).
//   - If no renderer backend is available, it gracefully handles the failure
//     by showing a "No Renderer Available" label instead of crashing.
class ModelViewWidget : public Gtk::Box {
public:
    struct MaterialParams {
        float ambient[3]{0.18f, 0.18f, 0.18f};
        float diffuse[3]{1.0f, 1.0f, 1.0f};
        float emissive[3]{0.0f, 0.0f, 0.0f};
        float specular[3]{0.08f, 0.08f, 0.08f};
        float specular_power = 32.0f;
        int shader_mode = 0;
    };

    using CameraMode = glmodel::CameraMode;
    using CameraState = glmodel::CameraState;

    enum class HighlightMode {
        Points,
        Lines,
    };

    ModelViewWidget();
    ~ModelViewWidget() override;

    void set_lod(const armatools::p3d::LOD& lod);
    void set_lods(const std::vector<armatools::p3d::LOD>& lods);
    void set_scene_blob(const rd_scene_blob_v1& blob,
                        const std::vector<std::string>& material_texture_keys = {});
    void set_texture(const std::string& key, int width, int height, const uint8_t* rgba_data);
    void set_normal_map(const std::string& key, int width, int height, const uint8_t* rgba_data);
    void set_specular_map(const std::string& key, int width, int height, const uint8_t* rgba_data);
    void set_material_params(const std::string& key, const MaterialParams& params);
    void reset_camera();
    void set_camera_from_bounds(float cx, float cy, float cz, float radius);
    void set_wireframe(bool on);
    void set_textured(bool on);
    Glib::RefPtr<Gdk::Pixbuf> snapshot() const;
    void set_show_grid(bool on);
    void set_background_color(float r, float g, float b);
    void set_camera_mode(CameraMode mode);
    CameraMode camera_mode() const;
    void set_highlight_geometry(const std::vector<float>& positions, HighlightMode mode);
    CameraState get_camera_state() const;
    void set_camera_state(const CameraState& state);
    sigc::signal<void()>& signal_camera_changed();

    bool get_realized() const;
    Glib::SignalProxy<void()> signal_realize();

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    Gtk::Box fallback_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label fallback_label_;
    sigc::signal<void()> fallback_camera_changed_;

    bool has_gles() const;
};

}  // namespace render_domain
