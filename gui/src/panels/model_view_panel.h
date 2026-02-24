#pragma once

#include <gtkmm.h>
#include "gl_model_view.h"
#include "p3d_model_loader.h"
#include "lod_textures_loader.h"

#include <armatools/p3d.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <functional>

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
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<LodTexturesLoaderService>& service);
    void set_info_line(const std::string& text);
    void set_model_data(const std::shared_ptr<armatools::p3d::P3DFile>& model,
                        const std::string& model_path);
    void set_on_lod_changed(std::function<void(const armatools::p3d::LOD&, int)> cb);

    void load_p3d(const std::string& model_path);
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
    std::shared_ptr<armatools::p3d::P3DFile> p3d_file_;
    std::shared_ptr<P3dModelLoaderService> model_loader_shared_;
    std::shared_ptr<LodTexturesLoaderService> texture_loader_shared_;

    // Toolbars
    Gtk::Box toolbar_row_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Box toolbar_left_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Box toolbar_right_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label info_line_label_;
    Gtk::ToggleButton wireframe_btn_;
    Gtk::ToggleButton texture_btn_;
    Gtk::ToggleButton grid_btn_;
    Gtk::Button reset_cam_btn_;
    Gtk::Button screenshot_btn_;
    Gtk::MenuButton bg_color_btn_;
    Gtk::MenuButton lods_btn_;
    Gtk::MenuButton named_selections_btn_;
    Gtk::Popover bg_color_popover_;
    Gtk::Popover lod_popover_;
    Gtk::Popover named_selections_popover_;
    Gtk::Box bg_color_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::ScrolledWindow lods_scroll_;
    Gtk::Box lods_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::ScrolledWindow named_selections_scroll_;
    Gtk::Box named_selections_box_{Gtk::Orientation::VERTICAL, 2};

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
    std::unordered_set<int> active_lod_indices_;
    std::unordered_map<std::string, std::vector<uint32_t>> current_named_selection_vertices_;
    std::unordered_set<std::string> active_named_selections_;
    std::function<void(const armatools::p3d::LOD&, int)> on_lod_changed_;

    void apply_lod(const armatools::p3d::LOD& lod, const std::string& model_path);
    void on_gl_realized();
    void load_textures_for_lod(const armatools::p3d::LOD& lod,
                               const std::string& model_path);
    void load_textures_for_lods(const std::vector<armatools::p3d::LOD>& lods,
                                const std::string& model_path);
    void render_active_lods(bool reset_camera);
    void setup_bg_color_popover();
    void on_screenshot();
    void setup_lods_menu();
    void setup_named_selections_menu(const armatools::p3d::LOD& lod);
    void update_named_selection_highlight();
};
