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
    int default_idx = 0;
    for (size_t i = 0; i < p3d_file_->lods.size(); ++i) {
        const auto& lod = p3d_file_->lods[i];
        if (lod.face_count > 0 && !lod.face_data.empty() && !lod.vertices.empty()) {
            default_idx = static_cast<int>(i);
            break;
        }
    }
    active_lod_indices_.clear();
    active_lod_indices_.insert(default_idx);
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
    if (p3d_file_ && !active_lod_indices_.empty()) {
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
    current_named_selection_vertices_ = lod.named_selection_vertices;
    cache_named_selection_geometry(lod);
    lods_btn_.set_label("LOD: " + lod.resolution_name);
    setup_named_selections_menu(lod);
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
    active_lod_indices_.clear();
    active_named_selections_.clear();
    current_named_selection_vertices_.clear();
    named_selection_face_geometry_.clear();
    highlight_lod_vertices_.clear();
    while (auto* child = named_selections_box_.get_first_child())
        named_selections_box_.remove(*child);
    gl_view_.set_highlight_geometry({}, GLModelView::HighlightMode::Points);
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

    while (auto* child = lods_box_.get_first_child())
        lods_box_.remove(*child);

    for (size_t i = 0; i < p3d_file_->lods.size(); ++i) {
        const auto& lod = p3d_file_->lods[i];
        auto text = lod.resolution_name + "  (V:" + std::to_string(lod.vertex_count)
            + " F:" + std::to_string(lod.face_count) + ")";
        auto* check = Gtk::make_managed<Gtk::CheckButton>(text);
        check->set_halign(Gtk::Align::START);
        check->set_active(active_lod_indices_.count(static_cast<int>(i)) > 0);
        check->signal_toggled().connect([this, idx = static_cast<int>(i), check]() {
            if (check->get_active()) {
                active_lod_indices_.insert(idx);
            } else {
                active_lod_indices_.erase(idx);
                if (active_lod_indices_.empty()) {
                    // Keep at least one LOD enabled.
                    active_lod_indices_.insert(idx);
                    check->set_active(true);
                    return;
                }
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
            GLModelView::MaterialParams mp;
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

    std::vector<int> indices(active_lod_indices_.begin(), active_lod_indices_.end());
    std::sort(indices.begin(), indices.end());
    std::vector<armatools::p3d::LOD> selected;
    selected.reserve(indices.size());
    for (int idx : indices) {
        if (idx < 0 || static_cast<size_t>(idx) >= p3d_file_->lods.size()) continue;
        selected.push_back(p3d_file_->lods[static_cast<size_t>(idx)]);
    }
    if (selected.empty()) return;

    const auto& primary = p3d_file_->lods[static_cast<size_t>(indices.front())];
    current_named_selection_vertices_ = primary.named_selection_vertices;
    cache_named_selection_geometry(primary);
    setup_named_selections_menu(primary);
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

void ModelViewPanel::setup_named_selections_menu(const armatools::p3d::LOD& lod) {
    active_named_selections_.clear();
    while (auto* child = named_selections_box_.get_first_child())
        named_selections_box_.remove(*child);

    if (lod.named_selections.empty()) {
        named_selections_btn_.set_label("SEL: 0");
        auto* label = Gtk::make_managed<Gtk::Label>("No named selections");
        label->set_halign(Gtk::Align::START);
        label->set_margin(6);
        named_selections_box_.append(*label);
        return;
    }

    named_selections_btn_.set_label(
        "SEL: " + std::to_string(lod.named_selections.size()));

    for (const auto& name : lod.named_selections) {
        size_t vertex_count = 0;
        auto vit = lod.named_selection_vertices.find(name);
        if (vit != lod.named_selection_vertices.end())
            vertex_count = vit->second.size();
        size_t face_count = 0;
        auto fit = lod.named_selection_faces.find(name);
        if (fit != lod.named_selection_faces.end())
            face_count = fit->second.size();
        auto label = name + " (F:" + std::to_string(face_count)
                     + ", V:" + std::to_string(vertex_count) + ")";
        auto* check = Gtk::make_managed<Gtk::CheckButton>(label);
        check->set_halign(Gtk::Align::START);
        check->signal_toggled().connect([this, name, check]() {
            if (check->get_active())
                active_named_selections_.insert(name);
            else
                active_named_selections_.erase(name);
            update_named_selection_highlight();
        });
        named_selections_box_.append(*check);
    }
}

void ModelViewPanel::update_named_selection_highlight() {
    if (active_named_selections_.empty()) {
        armatools::cli::log_debug("Named selection highlight: no active selections");
        gl_view_.set_highlight_geometry({}, GLModelView::HighlightMode::Points);
        return;
    }

    std::vector<float> highlight_lines;
    std::unordered_set<uint32_t> merged_vertices;
    std::ostringstream dbg;
    dbg << "Named selection highlight: ";
    bool first = true;
    for (const auto& name : active_named_selections_) {
        if (!first) dbg << ", ";
        first = false;
        dbg << name;

        auto face_it = named_selection_face_geometry_.find(name);
        if (face_it != named_selection_face_geometry_.end() && !face_it->second.empty()) {
            dbg << "(faces)";
            highlight_lines.insert(highlight_lines.end(),
                                   face_it->second.begin(), face_it->second.end());
            continue;
        }

        auto vert_it = current_named_selection_vertices_.find(name);
        if (vert_it != current_named_selection_vertices_.end() && !vert_it->second.empty()) {
            dbg << "(verts " << vert_it->second.size() << ")";
            merged_vertices.insert(vert_it->second.begin(), vert_it->second.end());
            continue;
        }

        dbg << "(missing)";
    }

    if (!highlight_lines.empty()) {
        dbg << " -> face edges: " << highlight_lines.size() / 6;
        armatools::cli::log_debug(dbg.str());
        gl_view_.set_highlight_geometry(highlight_lines, GLModelView::HighlightMode::Lines);
        return;
    }

    std::vector<float> points;
    if (!merged_vertices.empty() && !highlight_lod_vertices_.empty()) {
        points.reserve(merged_vertices.size() * 3);
        for (auto idx : merged_vertices) {
            if (idx >= highlight_lod_vertices_.size()) continue;
            const auto& p = highlight_lod_vertices_[static_cast<size_t>(idx)];
            points.push_back(-p[0]);
            points.push_back(p[1]);
            points.push_back(p[2]);
        }
    }

    if (!points.empty()) {
        dbg << " -> vertices: " << points.size() / 3;
        armatools::cli::log_debug(dbg.str());
        gl_view_.set_highlight_geometry(points, GLModelView::HighlightMode::Points);
        return;
    }

    dbg << " -> nothing to highlight";
    armatools::cli::log_debug(dbg.str());
    gl_view_.set_highlight_geometry({}, GLModelView::HighlightMode::Points);
}

void ModelViewPanel::cache_named_selection_geometry(const armatools::p3d::LOD& lod) {
    highlight_lod_vertices_ = lod.vertices;
    named_selection_face_geometry_.clear();
    if (highlight_lod_vertices_.empty() || lod.named_selection_faces.empty()) return;

    auto append_edge = [&](uint32_t a, uint32_t b, std::vector<float>& dest) {
        if (a >= highlight_lod_vertices_.size() || b >= highlight_lod_vertices_.size())
            return;
        const auto& pa = highlight_lod_vertices_[static_cast<size_t>(a)];
        const auto& pb = highlight_lod_vertices_[static_cast<size_t>(b)];
        dest.push_back(-pa[0]);
        dest.push_back(pa[1]);
        dest.push_back(pa[2]);
        dest.push_back(-pb[0]);
        dest.push_back(pb[1]);
        dest.push_back(pb[2]);
    };

    for (const auto& [name, face_indices] : lod.named_selection_faces) {
        std::vector<float> geom;
        geom.reserve(face_indices.size() * 6);
        for (auto face_index : face_indices) {
            if (face_index >= lod.faces.size()) continue;
            const auto& face = lod.faces[face_index];
            if (face.size() < 2) continue;
            for (size_t i = 0; i < face.size(); ++i) {
                auto curr = face[i];
                auto next = face[(i + 1) % face.size()];
                append_edge(curr, next, geom);
            }
        }
        if (!geom.empty())
            named_selection_face_geometry_.emplace(name, std::move(geom));
    }
}
