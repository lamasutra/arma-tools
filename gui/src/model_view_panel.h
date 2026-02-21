#pragma once

#include <gtkmm.h>
#include "gl_model_view.h"

#include <armatools/p3d.h>

#include <memory>
#include <string>
#include <unordered_set>

struct Config;

namespace armatools::pboindex {
class Index;
class DB;
}

class ModelViewPanel : public Gtk::Box {
public:
    ModelViewPanel();
    ~ModelViewPanel() override;

    // Configuration (call once after construction)
    void set_config(Config* cfg);
    void set_pboindex(armatools::pboindex::DB* db,
                      armatools::pboindex::Index* index);

    // Loading (call per model/LOD).
    // Safe to call even if the GL view is not yet realized (e.g. widget hidden);
    // the LOD data is stored and applied once the GL context is ready.
    void show_lod(const armatools::p3d::LOD& lod,
                  const std::string& model_path);
    void clear();

    // Relay: set background color on the GL view
    void set_background_color(float r, float g, float b);

    // Access to underlying GL widget
    GLModelView& gl_view();

private:
    Config* cfg_ = nullptr;
    armatools::pboindex::DB* db_ = nullptr;
    armatools::pboindex::Index* index_ = nullptr;

    // Toolbar
    Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::ToggleButton wireframe_btn_;
    Gtk::ToggleButton texture_btn_;
    Gtk::ToggleButton grid_btn_;
    Gtk::Button reset_cam_btn_;
    Gtk::Button screenshot_btn_;
    Gtk::MenuButton bg_color_btn_;
    Gtk::Popover bg_color_popover_;
    Gtk::Box bg_color_box_{Gtk::Orientation::VERTICAL, 2};

    // GL view
    GLModelView gl_view_;

    // Texture cache: tracks which keys have been uploaded to GL
    std::unordered_set<std::string> loaded_textures_;

    // Pending LOD data (for deferred loading when GL is not yet realized)
    struct PendingLod {
        armatools::p3d::LOD lod;
        std::string model_path;
    };
    std::unique_ptr<PendingLod> pending_lod_;
    sigc::connection realize_connection_;

    std::string current_model_path_;

    void apply_lod(const armatools::p3d::LOD& lod, const std::string& model_path);
    void on_gl_realized();
    void load_textures_for_lod(const armatools::p3d::LOD& lod,
                               const std::string& model_path);
    void setup_bg_color_popover();
    void on_screenshot();
};
