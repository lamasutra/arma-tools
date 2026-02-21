#include "model_view_panel.h"
#include "config.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/paa.h>
#include <armatools/pboindex.h>
#include <armatools/armapath.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

ModelViewPanel::ModelViewPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
    // Toolbar
    wireframe_btn_.set_icon_name("applications-engineering-symbolic");
    wireframe_btn_.set_tooltip_text("Wireframe");
    wireframe_btn_.set_has_frame(false);
    wireframe_btn_.add_css_class("p3d-toggle-icon");
    wireframe_btn_.set_size_request(26, 26);

    texture_btn_.set_icon_name("image-x-generic-symbolic");
    texture_btn_.set_tooltip_text("Textured");
    texture_btn_.set_has_frame(false);
    texture_btn_.add_css_class("p3d-toggle-icon");
    texture_btn_.set_size_request(26, 26);
    texture_btn_.set_active(true);

    grid_btn_.set_icon_name("view-grid-symbolic");
    grid_btn_.set_tooltip_text("Grid");
    grid_btn_.set_has_frame(false);
    grid_btn_.add_css_class("p3d-toggle-icon");
    grid_btn_.set_size_request(26, 26);
    grid_btn_.set_active(true);

    reset_cam_btn_.set_icon_name("view-refresh-symbolic");
    reset_cam_btn_.set_tooltip_text("Reset Camera");
    reset_cam_btn_.set_has_frame(false);

    screenshot_btn_.set_icon_name("camera-photo-symbolic");
    screenshot_btn_.set_tooltip_text("Screenshot");
    screenshot_btn_.set_has_frame(false);

    toolbar_.set_margin_top(4);
    toolbar_.set_margin_start(4);
    toolbar_.append(wireframe_btn_);
    toolbar_.append(texture_btn_);
    toolbar_.append(grid_btn_);
    toolbar_.append(reset_cam_btn_);
    toolbar_.append(screenshot_btn_);

    // Background color menu button
    bg_color_btn_.set_label("BG");
    bg_color_btn_.set_tooltip_text("Background color");
    setup_bg_color_popover();
    toolbar_.append(bg_color_btn_);

    append(toolbar_);

    // GL view expands to fill
    gl_view_.set_vexpand(true);
    gl_view_.set_hexpand(true);
    gl_view_.set_size_request(-1, 200);
    append(gl_view_);

    // Signals
    wireframe_btn_.signal_toggled().connect([this]() {
        gl_view_.set_wireframe(wireframe_btn_.get_active());
    });
    texture_btn_.signal_toggled().connect([this]() {
        gl_view_.set_textured(texture_btn_.get_active());
    });
    grid_btn_.signal_toggled().connect([this]() {
        gl_view_.set_show_grid(grid_btn_.get_active());
    });
    reset_cam_btn_.signal_clicked().connect([this]() {
        gl_view_.reset_camera();
    });
    screenshot_btn_.signal_clicked().connect(
        sigc::mem_fun(*this, &ModelViewPanel::on_screenshot));
}

ModelViewPanel::~ModelViewPanel() {
    realize_connection_.disconnect();
}

void ModelViewPanel::set_config(Config* cfg) {
    cfg_ = cfg;
}

void ModelViewPanel::set_pboindex(armatools::pboindex::DB* db,
                                   armatools::pboindex::Index* index) {
    db_ = db;
    index_ = index;
}

void ModelViewPanel::show_lod(const armatools::p3d::LOD& lod,
                               const std::string& model_path) {
    // If the GL view is realized, apply immediately.
    // Otherwise, store a copy and defer until the GL context is ready.
    if (gl_view_.get_realized()) {
        pending_lod_.reset();
        apply_lod(lod, model_path);
    } else {
        pending_lod_ = std::make_unique<PendingLod>(PendingLod{lod, model_path});
        if (!realize_connection_.connected()) {
            realize_connection_ = gl_view_.signal_realize().connect(
                sigc::mem_fun(*this, &ModelViewPanel::on_gl_realized));
        }
    }
}

void ModelViewPanel::on_gl_realized() {
    realize_connection_.disconnect();
    if (pending_lod_) {
        auto pending = std::move(pending_lod_);
        apply_lod(pending->lod, pending->model_path);
    }
}

void ModelViewPanel::apply_lod(const armatools::p3d::LOD& lod,
                                const std::string& model_path) {
    current_model_path_ = model_path;
    gl_view_.set_lod(lod);
    gl_view_.set_camera_from_bounds(
        lod.bounding_center[0], lod.bounding_center[1], lod.bounding_center[2],
        lod.bounding_radius);
    load_textures_for_lod(lod, model_path);
}

void ModelViewPanel::clear() {
    loaded_textures_.clear();
    pending_lod_.reset();
}

GLModelView& ModelViewPanel::gl_view() {
    return gl_view_;
}

void ModelViewPanel::set_background_color(float r, float g, float b) {
    gl_view_.set_background_color(r, g, b);
}

void ModelViewPanel::setup_bg_color_popover() {
    struct ColorPreset {
        const char* label;
        float r, g, b;
    };
    static const ColorPreset presets[] = {
        {"Black",      0.0f, 0.0f, 0.0f},
        {"Dark Gray",  0.3f, 0.3f, 0.3f},
        {"Light Gray", 0.7f, 0.7f, 0.7f},
        {"White",      1.0f, 1.0f, 1.0f},
    };

    for (const auto& p : presets) {
        auto* btn = Gtk::make_managed<Gtk::Button>(p.label);
        float r = p.r, g = p.g, b = p.b;
        btn->signal_clicked().connect([this, r, g, b]() {
            gl_view_.set_background_color(r, g, b);
            bg_color_popover_.popdown();
        });
        bg_color_box_.append(*btn);
    }

    bg_color_popover_.set_child(bg_color_box_);
    bg_color_btn_.set_popover(bg_color_popover_);
}

void ModelViewPanel::load_textures_for_lod(const armatools::p3d::LOD& lod,
                                            const std::string& model_path) {
    if (!index_) return;

    for (const auto& tex_path : lod.textures) {
        if (tex_path.empty()) continue;
        if (armatools::armapath::is_procedural_texture(tex_path)) continue;

        auto normalized = armatools::armapath::to_slash_lower(tex_path);

        // Already loaded?
        if (loaded_textures_.count(normalized)) continue;

        // Try loading from disk first (drive_root or relative to model)
        bool loaded_from_disk = false;
        if (cfg_ && !cfg_->drive_root.empty()) {
            auto on_disk = armatools::armapath::to_os(tex_path);
            namespace fs = std::filesystem;

            std::vector<fs::path> candidates;
            auto base_dir = fs::path(model_path).parent_path();
            candidates.push_back(base_dir / on_disk);
            candidates.push_back(base_dir / on_disk.filename());
            candidates.push_back(fs::path(cfg_->drive_root) / on_disk);

            for (const auto& cand : candidates) {
                std::error_code ec;
                if (!fs::exists(cand, ec)) continue;

                std::ifstream f(cand, std::ios::binary);
                if (!f.is_open()) continue;

                try {
                    auto [img, hdr] = armatools::paa::decode(f);
                    if (img.width > 0 && img.height > 0) {
                        gl_view_.set_texture(tex_path, img.width, img.height,
                                             img.pixels.data());
                        loaded_textures_.insert(normalized);
                        loaded_from_disk = true;
                        break;
                    }
                } catch (...) {}
            }
        }

        if (loaded_from_disk) continue;

        // Resolve via pboindex
        armatools::pboindex::ResolveResult rr;
        if (!index_->resolve(normalized, rr)) continue;

        // Extract from PBO
        auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
        if (data.empty()) continue;

        // Decode PAA
        try {
            std::string str(data.begin(), data.end());
            std::istringstream stream(str);
            auto [img, hdr] = armatools::paa::decode(stream);
            if (img.width > 0 && img.height > 0) {
                gl_view_.set_texture(tex_path, img.width, img.height,
                                     img.pixels.data());
                loaded_textures_.insert(normalized);
            }
        } catch (...) {
            // Texture decode failed -- render flat gray for this texture
        }
    }
}

void ModelViewPanel::on_screenshot() {
    auto pixbuf = gl_view_.snapshot();
    if (!pixbuf) return;

    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PNG files");
    filter->add_pattern("*.png");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    // Suggest filename from model path
    if (!current_model_path_.empty()) {
        auto stem = std::filesystem::path(current_model_path_).stem().string();
        dialog->set_initial_name(stem + ".png");
    } else {
        dialog->set_initial_name("screenshot.png");
    }

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog, pixbuf](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (file) {
                    auto path = file->get_path();
                    pixbuf->save(path, "png");
                    app_log(LogLevel::Info, "Saved screenshot: " + path);
                }
            } catch (const std::exception& e) {
                app_log(LogLevel::Error,
                        std::string("Screenshot save error: ") + e.what());
            } catch (...) {}
        });
}
