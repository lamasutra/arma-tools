#include "model_view_panel.h"
#include "config.h"
#include "log_panel.h"
#include "p3d_model_loader.h"
#include "pbo_util.h"

#include <armatools/paa.h>
#include <armatools/pboindex.h>
#include <armatools/armapath.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cli_logger.h"

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

    info_line_label_.set_halign(Gtk::Align::START);
    info_line_label_.set_hexpand(true);
    info_line_label_.set_ellipsize(Pango::EllipsizeMode::END);
    info_line_label_.set_text("");

    toolbar_row_.set_margin(2);
    toolbar_row_.set_spacing(4);
    toolbar_left_.set_spacing(2);
    toolbar_right_.set_spacing(2);
    toolbar_left_.set_hexpand(true);
    toolbar_right_.set_halign(Gtk::Align::END);

    toolbar_left_.append(info_line_label_);
    toolbar_right_.append(wireframe_btn_);
    toolbar_right_.append(texture_btn_);
    toolbar_right_.append(grid_btn_);
    toolbar_right_.append(reset_cam_btn_);
    toolbar_right_.append(screenshot_btn_);

    // Background color menu button
    bg_color_btn_.set_label("BG");
    bg_color_btn_.set_tooltip_text("Background color");
    setup_bg_color_popover();
    toolbar_right_.append(bg_color_btn_);

    lods_btn_.set_label("LOD: -");
    lods_btn_.set_tooltip_text("Select LOD to display");
    toolbar_right_.append(lods_btn_);

    lod_list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    lod_list_.signal_row_selected().connect(
        sigc::mem_fun(*this, &ModelViewPanel::on_lod_selected));
    lod_scroll_.set_child(lod_list_);
    lod_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    lod_scroll_.set_max_content_height(220);
    lod_scroll_.set_propagate_natural_height(true);
    lod_popover_.set_child(lod_scroll_);
    lod_popover_.add_css_class("p3d-lod-popover");
    lods_btn_.set_popover(lod_popover_);

    toolbar_row_.append(toolbar_left_);
    toolbar_row_.append(toolbar_right_);
    append(toolbar_row_);

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

void ModelViewPanel::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_loader_shared_ = service;
}

void ModelViewPanel::set_texture_loader_service(
    const std::shared_ptr<LodTexturesLoaderService>& service) {
    texture_loader_shared_ = service;
}

void ModelViewPanel::set_info_line(const std::string& text) {
    info_line_label_.set_text(text);
}

void ModelViewPanel::set_model_data(
    const std::shared_ptr<armatools::p3d::P3DFile>& model,
    const std::string& model_path) {
    clear();
    current_model_path_ = model_path;
    p3d_file_ = model;
    if (!p3d_file_ || p3d_file_->lods.empty()) return;
    setup_lods_menu();
    if (auto* row = lod_list_.get_row_at_index(0)) {
        lod_list_.select_row(*row);
    } else {
        show_lod(p3d_file_->lods[0], model_path);
    }
}

void ModelViewPanel::set_on_lod_changed(
    std::function<void(const armatools::p3d::LOD&, int)> cb) {
    on_lod_changed_ = std::move(cb);
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

void ModelViewPanel::load_p3d(const std::string& model_path) {
    clear();
    if (model_path.empty()) return;
    if (!model_loader_shared_) {
        armatools::cli::log_warning("Model loader service not configured");
        return;
    }

    try {
        auto model = std::make_shared<armatools::p3d::P3DFile>(
            model_loader_shared_->load_p3d(model_path));

        std::ostringstream info;
        info << "Format: " << model->format << " v" << model->version
             << " | LODs: " << model->lods.size();
        auto size_result = armatools::p3d::calculate_size(*model);
        if (size_result.info) {
            const auto& s = *size_result.info;
            info << " | Size: " << s.dimensions[0] << "x"
                 << s.dimensions[1] << "x" << s.dimensions[2] << "m";
        }
        set_info_line(info.str());

        set_model_data(model, model_path);
    } catch (const std::exception& e) {
        armatools::cli::log_error("Error loading P3D: " + std::string(e.what()));
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
    lods_btn_.set_label("LOD: " + lod.resolution_name);
    gl_view_.set_lod(lod);
    gl_view_.set_camera_from_bounds(
        lod.bounding_center[0], lod.bounding_center[1], lod.bounding_center[2],
        lod.bounding_radius);
    load_textures_for_lod(lod, model_path);
}

void ModelViewPanel::clear() {
    loaded_textures_.clear();
    pending_lod_.reset();
    while (auto* row = lod_list_.get_row_at_index(0))
        lod_list_.remove(*row);
    lods_btn_.set_label("LOD: -");
    current_lod_index_ = -1;
    p3d_file_.reset();
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

void ModelViewPanel::setup_lods_menu() {
    armatools::cli::log_debug("Setting up LODs menu");
    if (!p3d_file_) {
        armatools::cli::log_warning("No p3d file yet");
        return;
    }

    // Clear existing LOD rows
    while (auto* row = lod_list_.get_row_at_index(0))
        lod_list_.remove(*row);

    for (const auto& lod : p3d_file_->lods) {
        auto text = lod.resolution_name + "  (V:" + std::to_string(lod.vertex_count)
            + " F:" + std::to_string(lod.face_count) + ")";
        auto* label = Gtk::make_managed<Gtk::Label>(text);
        label->set_halign(Gtk::Align::START);
        lod_list_.append(*label);
    }
    armatools::cli::log_debug("Setting up LODs done");
}

void ModelViewPanel::on_lod_selected(Gtk::ListBoxRow* row) {
    if (!row || !p3d_file_) return;
    const int idx = row->get_index();
    if (idx < 0 || static_cast<size_t>(idx) >= p3d_file_->lods.size()) return;
    if (idx == current_lod_index_) return;
    current_lod_index_ = idx;
    const auto& lod = p3d_file_->lods[static_cast<size_t>(idx)];
    show_lod(lod, current_model_path_);
    if (on_lod_changed_) on_lod_changed_(lod, idx);
    lod_popover_.popdown();
}

void ModelViewPanel::load_textures_for_lod(const armatools::p3d::LOD& lod,
                                            const std::string& model_path) {
    if (!texture_loader_shared_) return;

    // Load all textures from the LOD
    auto textures = texture_loader_shared_->load_textures(
        const_cast<armatools::p3d::LOD&>(lod), model_path);
    
    // Apply each texture to the GL view
    for (const auto& tex : textures) {
        auto normalized = armatools::armapath::to_slash_lower(tex.path);
        if (!loaded_textures_.count(normalized)) {
            gl_view_.set_texture(tex.path, tex.image.width, tex.image.height,
                                 tex.image.pixels.data());
            loaded_textures_.insert(normalized);
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
