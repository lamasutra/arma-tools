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
#include <thread>

#include "cli_logger.h"

namespace {

render_domain::ModelViewWidget::HighlightMode to_gl_highlight_mode(modelview::HighlightMode mode) {
    return mode == modelview::HighlightMode::Lines
        ? render_domain::ModelViewWidget::HighlightMode::Lines
        : render_domain::ModelViewWidget::HighlightMode::Points;
}

}  // namespace

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

    camera_mode_btn_.set_has_frame(false);
    camera_mode_btn_.add_css_class("p3d-toggle-icon");
    camera_mode_btn_.set_size_request(26, 26);
    camera_mode_btn_.set_active(true);

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
    toolbar_right_.append(camera_mode_btn_);
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
    named_selections_btn_.set_label("SEL: -");
    named_selections_btn_.set_tooltip_text("Toggle named selections");
    toolbar_right_.append(named_selections_btn_);

    lods_scroll_.set_child(lods_box_);
    lods_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    lods_scroll_.set_max_content_height(260);
    lods_scroll_.set_propagate_natural_height(true);
    lod_popover_.set_child(lods_scroll_);
    lod_popover_.add_css_class("p3d-lod-popover");
    lods_btn_.set_popover(lod_popover_);

    named_selections_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    named_selections_scroll_.set_max_content_height(260);
    named_selections_scroll_.set_propagate_natural_height(true);
    named_selections_scroll_.set_child(named_selections_box_);
    named_selections_popover_.set_child(named_selections_scroll_);
    named_selections_popover_.add_css_class("p3d-lod-popover");
    named_selections_btn_.set_popover(named_selections_popover_);

    toolbar_row_.append(toolbar_left_);
    toolbar_row_.append(toolbar_right_);
    append(toolbar_row_);

    // GL view expands to fill
    gl_view_.set_vexpand(true);
    gl_view_.set_hexpand(true);
    gl_view_.set_size_request(-1, 200);
    gl_overlay_.set_vexpand(true);
    gl_overlay_.set_hexpand(true);
    gl_overlay_.set_child(gl_view_);
    loading_overlay_box_.set_halign(Gtk::Align::CENTER);
    loading_overlay_box_.set_valign(Gtk::Align::CENTER);
    loading_overlay_box_.set_margin(10);
    loading_overlay_box_.add_css_class("card");
    loading_spinner_.set_halign(Gtk::Align::CENTER);
    loading_spinner_.set_valign(Gtk::Align::CENTER);
    loading_spinner_.set_size_request(48, 48);
    loading_label_.set_halign(Gtk::Align::CENTER);
    loading_overlay_box_.append(loading_spinner_);
    loading_overlay_box_.append(loading_label_);
    loading_overlay_box_.set_visible(false);
    gl_overlay_.add_overlay(loading_overlay_box_);
    append(gl_overlay_);

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
    auto update_camera_mode_button = [this]() {
        if (camera_mode_btn_.get_active()) {
            camera_mode_btn_.set_icon_name("object-rotate-right-symbolic");
            camera_mode_btn_.set_tooltip_text(
                "Orbit camera (click to switch to first person)");
        } else {
            camera_mode_btn_.set_icon_name("input-keyboard-symbolic");
            camera_mode_btn_.set_tooltip_text(
                "First-person camera (click to switch to orbit)");
        }
    };
    camera_mode_btn_.signal_toggled().connect([this, update_camera_mode_button]() {
        gl_view_.set_camera_mode(camera_mode_btn_.get_active()
                                     ? render_domain::ModelViewWidget::CameraMode::Orbit
                                     : render_domain::ModelViewWidget::CameraMode::FirstPerson);
        update_camera_mode_button();
    });
    update_camera_mode_button();
    reset_cam_btn_.signal_clicked().connect([this]() {
        gl_view_.reset_camera();
    });
    screenshot_btn_.signal_clicked().connect(
        sigc::mem_fun(*this, &ModelViewPanel::on_screenshot));
}

ModelViewPanel::~ModelViewPanel() {
    realize_connection_.disconnect();
    load_poll_conn_.disconnect();
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
    const std::shared_ptr<TexturesLoaderService>& service) {
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
    const int default_idx = presenter_.choose_default_lod_index(p3d_file_->lods);
    presenter_.set_single_active_lod(default_idx);
    setup_lods_menu();
    render_active_lods(true);
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

    ++current_load_request_id_;
    const uint64_t request_id = current_load_request_id_;
    set_loading_state(true);
    set_info_line("Loading model...");

    auto queue = async_load_queue_;
    auto loader = model_loader_shared_;
    std::thread([queue, loader, model_path, request_id]() {
        AsyncLoadResult result;
        result.request_id = request_id;
        result.model_path = model_path;
        try {
            result.model = std::make_shared<armatools::p3d::P3DFile>(loader->load_p3d(model_path));
            std::ostringstream info;
            info << "Format: " << result.model->format << " v" << result.model->version
                 << " | LODs: " << result.model->lods.size();
            auto size_result = armatools::p3d::calculate_size(*result.model);
            if (size_result.info) {
                const auto& s = *size_result.info;
                info << " | Size: " << s.dimensions[0] << "x"
                     << s.dimensions[1] << "x" << s.dimensions[2] << "m";
            }
            result.info_line = info.str();
        } catch (const std::exception& e) {
            result.error = e.what();
        } catch (...) {
            result.error = "Unknown error";
        }
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            queue->results.push_back(std::move(result));
        }
    }).detach();

    if (!load_poll_conn_.connected()) {
        load_poll_conn_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &ModelViewPanel::on_load_poll), 16);
    }
}

void ModelViewPanel::on_gl_realized() {
    realize_connection_.disconnect();
    if (p3d_file_
        && !presenter_.sorted_active_lod_indices(p3d_file_->lods.size()).empty()) {
        render_active_lods(true);
        return;
    }
    if (pending_lod_) {
        auto pending = std::move(pending_lod_);
        apply_lod(pending->lod, pending->model_path);
    }
}

void ModelViewPanel::apply_lod(const armatools::p3d::LOD& lod,
                                const std::string& model_path) {
    current_model_path_ = model_path;
    presenter_.set_named_selection_source(lod);
    lods_btn_.set_label("LOD: " + lod.resolution_name);
    setup_named_selections_menu();
    gl_view_.set_lod(lod);
    update_named_selection_highlight();
    gl_view_.set_camera_from_bounds(
        lod.bounding_center[0], lod.bounding_center[1], lod.bounding_center[2],
        lod.bounding_radius);
    load_textures_for_lod(lod, model_path);
}

void ModelViewPanel::clear() {
    loaded_textures_.clear();
    pending_lod_.reset();
    while (auto* child = lods_box_.get_first_child())
        lods_box_.remove(*child);
    lods_btn_.set_label("LOD: -");
    named_selections_btn_.set_label("SEL: -");
    presenter_.clear();
    while (auto* child = named_selections_box_.get_first_child())
        named_selections_box_.remove(*child);
    gl_view_.set_highlight_geometry({}, render_domain::ModelViewWidget::HighlightMode::Points);
    p3d_file_.reset();
}

void ModelViewPanel::set_loading_state(bool loading) {
    loading_model_ = loading;
    loading_overlay_box_.set_visible(loading);
    if (loading) {
        loading_spinner_.start();
    } else {
        loading_spinner_.stop();
    }
}

bool ModelViewPanel::on_load_poll() {
    std::deque<AsyncLoadResult> results;
    {
        std::lock_guard<std::mutex> lock(async_load_queue_->mutex);
        if (!async_load_queue_->results.empty()) {
            results.swap(async_load_queue_->results);
        }
    }

    for (auto& result : results) {
        if (result.request_id != current_load_request_id_) {
            continue;
        }
        if (!result.error.empty()) {
            set_info_line("Error: " + result.error);
            armatools::cli::log_error("Error loading P3D `" + result.model_path + "`: " + result.error);
            set_loading_state(false);
            continue;
        }
        set_info_line(result.info_line);
        set_model_data(result.model, result.model_path);
        set_loading_state(false);
    }

    if (!loading_model_) {
        load_poll_conn_.disconnect();
        return false;
    }
    return true;
}

render_domain::ModelViewWidget& ModelViewPanel::gl_view() {
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

    while (auto* child = lods_box_.get_first_child())
        lods_box_.remove(*child);

    for (size_t i = 0; i < p3d_file_->lods.size(); ++i) {
        const auto& lod = p3d_file_->lods[i];
        auto text = lod.resolution_name + "  (V:" + std::to_string(lod.vertex_count)
            + " F:" + std::to_string(lod.face_count) + ")";
        auto* check = Gtk::make_managed<Gtk::CheckButton>(text);
        check->set_halign(Gtk::Align::START);
        check->set_active(presenter_.is_lod_active(static_cast<int>(i)));
        check->signal_toggled().connect([this, idx = static_cast<int>(i), check]() {
            if (!presenter_.set_lod_active(idx, check->get_active())) {
                check->set_active(true);
                return;
            }
            render_active_lods(false);
        });
        lods_box_.append(*check);
    }
    armatools::cli::log_debug("Setting up LODs done");
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
        if (tex.has_normal_map && tex.normal_map.width > 0 && tex.normal_map.height > 0) {
            gl_view_.set_normal_map(tex.path, tex.normal_map.width, tex.normal_map.height,
                                    tex.normal_map.pixels.data());
        }
        if (tex.has_specular_map && tex.specular_map.width > 0 && tex.specular_map.height > 0) {
            gl_view_.set_specular_map(tex.path, tex.specular_map.width, tex.specular_map.height,
                                      tex.specular_map.pixels.data());
        }
        if (tex.has_material) {
            render_domain::ModelViewWidget::MaterialParams mp;
            mp.ambient[0] = tex.material.ambient[0];
            mp.ambient[1] = tex.material.ambient[1];
            mp.ambient[2] = tex.material.ambient[2];
            mp.diffuse[0] = tex.material.diffuse[0];
            mp.diffuse[1] = tex.material.diffuse[1];
            mp.diffuse[2] = tex.material.diffuse[2];
            mp.emissive[0] = tex.material.emissive[0];
            mp.emissive[1] = tex.material.emissive[1];
            mp.emissive[2] = tex.material.emissive[2];
            mp.specular[0] = tex.material.specular[0];
            mp.specular[1] = tex.material.specular[1];
            mp.specular[2] = tex.material.specular[2];
            mp.specular_power = tex.material.specular_power;
            mp.shader_mode = tex.material.shader_mode;
            gl_view_.set_material_params(tex.path, mp);
        }
    }
}

void ModelViewPanel::load_textures_for_lods(const std::vector<armatools::p3d::LOD>& lods,
                                            const std::string& model_path) {
    for (const auto& lod : lods)
        load_textures_for_lod(lod, model_path);
}

void ModelViewPanel::render_active_lods(bool reset_camera) {
    if (!p3d_file_ || p3d_file_->lods.empty()) return;
    if (!gl_view_.get_realized()) {
        if (!realize_connection_.connected()) {
            realize_connection_ = gl_view_.signal_realize().connect(
                sigc::mem_fun(*this, &ModelViewPanel::on_gl_realized));
        }
        return;
    }

    std::vector<int> indices = presenter_.sorted_active_lod_indices(p3d_file_->lods.size());
    std::vector<armatools::p3d::LOD> selected;
    selected.reserve(indices.size());
    for (int idx : indices) {
        if (idx < 0 || static_cast<size_t>(idx) >= p3d_file_->lods.size()) continue;
        selected.push_back(p3d_file_->lods[static_cast<size_t>(idx)]);
    }
    if (selected.empty()) return;

    const auto& primary = p3d_file_->lods[static_cast<size_t>(indices.front())];
    presenter_.set_named_selection_source(primary);
    setup_named_selections_menu();
    update_named_selection_highlight();

    gl_view_.set_lods(selected);
    if (reset_camera) {
        gl_view_.set_camera_from_bounds(
            primary.bounding_center[0], primary.bounding_center[1], primary.bounding_center[2],
            primary.bounding_radius);
    }
    load_textures_for_lods(selected, current_model_path_);

    lods_btn_.set_label("LOD: " + std::to_string(selected.size()));
    if (on_lod_changed_) on_lod_changed_(primary, indices.front());
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

void ModelViewPanel::setup_named_selections_menu() {
    while (auto* child = named_selections_box_.get_first_child())
        named_selections_box_.remove(*child);

    const auto& items = presenter_.named_selection_items();
    if (items.empty()) {
        named_selections_btn_.set_label("SEL: 0");
        auto* label = Gtk::make_managed<Gtk::Label>("No named selections");
        label->set_halign(Gtk::Align::START);
        label->set_margin(6);
        named_selections_box_.append(*label);
        return;
    }

    named_selections_btn_.set_label("SEL: " + std::to_string(items.size()));

    for (const auto& item : items) {
        auto* check = Gtk::make_managed<Gtk::CheckButton>(item.label);
        check->set_halign(Gtk::Align::START);
        check->signal_toggled().connect([this, name = item.name, check]() {
            presenter_.set_named_selection_active(name, check->get_active());
            update_named_selection_highlight();
        });
        named_selections_box_.append(*check);
    }
}

void ModelViewPanel::update_named_selection_highlight() {
    const auto highlight = presenter_.build_highlight_geometry();
    armatools::cli::log_debug(highlight.debug_message);
    gl_view_.set_highlight_geometry(
        highlight.positions,
        to_gl_highlight_mode(highlight.mode));
}
