#include "tab_wrp_info.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/objcat.h>
#include <armatools/paa.h>
#include <armatools/p3d.h>
#include <armatools/pboindex.h>
#include <armatools/wrp.h>
#include <armatools/armapath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

TabWrpInfo::TabWrpInfo() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    auto make_icon_button = [](Gtk::Button& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
    };
    auto make_icon_toggle = [](Gtk::ToggleButton& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
        b.add_css_class("p3d-toggle-icon");
        b.set_size_request(26, 26);
    };
    make_icon_button(scan_button_, "system-search-symbolic", "Scan/search WRP files");
    make_icon_button(folder_button_, "document-open-symbolic", "Browse folder with WRP files");

    // Left panel: file scanner
    list_box_.set_margin(8);
    list_box_.set_size_request(180, -1);

    source_combo_.set_tooltip_text("Filter by A3DB source");
    source_combo_.append("", "All");
    source_combo_.set_active_id("");
    filter_entry_.set_hexpand(true);
    filter_entry_.set_placeholder_text("Filter...");
    filter_box_.append(source_label_);
    filter_box_.append(source_combo_);
    filter_box_.append(filter_entry_);
    filter_box_.append(scan_button_);
    filter_box_.append(folder_button_);
    list_box_.append(filter_box_);

    list_scroll_.set_vexpand(true);
    list_scroll_.set_child(file_list_);
    list_box_.append(list_scroll_);

    set_start_child(list_box_);
    set_position(320);

    // === Right panel: Notebook with 3 pages ===

    // Page 1: Info
    info_view_.set_editable(false);
    info_view_.set_monospace(true);
    info_view_.set_wrap_mode(Gtk::WrapMode::WORD);
    info_scroll_.set_child(info_view_);
    info_scroll_.set_vexpand(true);
    right_notebook_.append_page(info_scroll_, "Info");

    // Page 2: Objects
    class_status_label_.set_halign(Gtk::Align::START);
    class_status_label_.set_margin(4);
    class_top_box_.append(class_status_label_);

    class_scroll_.set_vexpand(true);
    class_list_.set_activate_on_single_click(false);
    class_scroll_.set_child(class_list_);
    class_top_box_.append(class_scroll_);

    objects_paned_.set_start_child(class_top_box_);
    objects_paned_.set_resize_start_child(true);
    objects_paned_.set_shrink_start_child(false);

    model_panel_.set_vexpand(true);
    model_panel_.set_hexpand(true);
    objects_paned_.set_end_child(model_panel_);
    objects_paned_.set_resize_end_child(true);
    objects_paned_.set_shrink_end_child(false);

    right_notebook_.append_page(objects_paned_, "Objects");

    // Page 3: Heightmap
    hm_toolbar_.set_margin(4);
    hm_toolbar_.append(hm_scale_label_);
    hm_scale_combo_.append("1", "1x (native)");
    hm_scale_combo_.append("2", "2x");
    hm_scale_combo_.append("4", "4x");
    hm_scale_combo_.append("8", "8x");
    hm_scale_combo_.append("16", "16x");
    hm_scale_combo_.set_active_id("1");
    hm_toolbar_.append(hm_scale_combo_);
    hm_toolbar_.append(hm_export_button_);
    hm_box_.append(hm_toolbar_);

    hm_picture_.set_can_shrink(true);
    hm_picture_.set_content_fit(Gtk::ContentFit::CONTAIN);
    hm_scroll_.set_child(hm_picture_);
    hm_scroll_.set_vexpand(true);
    hm_box_.append(hm_scroll_);

    right_notebook_.append_page(hm_box_, "Heightmap");

    // Page 4: Terrain 3D
    terrain3d_toolbar_.set_margin(4);
    update_terrain3d_mode_options(true, true);
    terrain3d_seam_debug_combo_.append("final", "Final");
    terrain3d_seam_debug_combo_.append("depth", "Depth");
    terrain3d_seam_debug_combo_.append("normals", "Normals");
    terrain3d_seam_debug_combo_.set_active_id("final");
    make_icon_toggle(terrain3d_camera_mode_btn_, "object-rotate-right-symbolic",
                     "Orbit camera (click to switch to first person)");
    terrain3d_camera_mode_btn_.set_active(true);
    make_icon_toggle(terrain3d_wireframe_btn_, "applications-engineering-symbolic", "Wireframe");
    make_icon_toggle(terrain3d_objects_btn_, "image-x-generic-symbolic", "Objects");
    make_icon_toggle(terrain3d_patch_bounds_btn_, "view-fullscreen-symbolic", "Patch bounds");
    make_icon_toggle(terrain3d_lod_tint_btn_, "dialog-information-symbolic", "LOD colors");
    make_icon_toggle(terrain3d_tile_bounds_btn_, "view-grid-symbolic", "Tile grid");
    terrain3d_wireframe_btn_.set_active(false);
    terrain3d_objects_btn_.set_active(true);
    terrain3d_patch_bounds_btn_.set_active(false);
    terrain3d_lod_tint_btn_.set_active(false);
    terrain3d_tile_bounds_btn_.set_active(false);
    terrain3d_far_scale_.set_range(1000.0, 60000.0);
    terrain3d_far_scale_.set_value(25000.0);
    terrain3d_far_scale_.set_digits(0);
    terrain3d_far_scale_.set_size_request(130, -1);
    terrain3d_mid_scale_.set_range(300.0, 20000.0);
    terrain3d_mid_scale_.set_value(1800.0);
    terrain3d_mid_scale_.set_digits(0);
    terrain3d_mid_scale_.set_size_request(110, -1);
    terrain3d_far_mat_scale_.set_range(600.0, 60000.0);
    terrain3d_far_mat_scale_.set_value(5200.0);
    terrain3d_far_mat_scale_.set_digits(0);
    terrain3d_far_mat_scale_.set_size_request(110, -1);
    terrain3d_status_label_.set_halign(Gtk::Align::START);
    terrain3d_base_status_ = "Load a WRP to preview terrain";
    terrain3d_status_label_.set_text(terrain3d_base_status_);
    terrain3d_toolbar_.append(terrain3d_mode_label_);
    terrain3d_toolbar_.append(terrain3d_mode_combo_);
    terrain3d_toolbar_.append(terrain3d_seam_debug_label_);
    terrain3d_toolbar_.append(terrain3d_seam_debug_combo_);
    terrain3d_toolbar_.append(terrain3d_camera_mode_btn_);
    terrain3d_toolbar_.append(terrain3d_wireframe_btn_);
    terrain3d_toolbar_.append(terrain3d_objects_btn_);
    terrain3d_toolbar_.append(terrain3d_patch_bounds_btn_);
    terrain3d_toolbar_.append(terrain3d_lod_tint_btn_);
    terrain3d_toolbar_.append(terrain3d_tile_bounds_btn_);
    terrain3d_toolbar_.append(terrain3d_far_label_);
    terrain3d_toolbar_.append(terrain3d_far_scale_);
    terrain3d_toolbar_.append(terrain3d_mid_label_);
    terrain3d_toolbar_.append(terrain3d_mid_scale_);
    terrain3d_toolbar_.append(terrain3d_far_mat_label_);
    terrain3d_toolbar_.append(terrain3d_far_mat_scale_);
    terrain3d_box_.append(terrain3d_toolbar_);
    terrain3d_view_.set_hexpand(true);
    terrain3d_view_.set_vexpand(true);
    terrain3d_overlay_.set_child(terrain3d_view_);
    terrain3d_status_box_.set_halign(Gtk::Align::START);
    terrain3d_status_box_.set_valign(Gtk::Align::END);
    terrain3d_status_box_.set_margin(8);
    terrain3d_status_box_.add_css_class("terrain3d-status");
    terrain3d_status_box_.append(terrain3d_status_label_);
    terrain3d_overlay_.add_overlay(terrain3d_status_box_);
    terrain3d_debug_overlay_.set_halign(Gtk::Align::START);
    terrain3d_debug_overlay_.set_valign(Gtk::Align::START);
    terrain3d_debug_overlay_.set_margin(8);
    terrain3d_debug_overlay_.set_text("");
    terrain3d_debug_overlay_.set_visible(false);
    terrain3d_debug_overlay_.add_css_class("caption");
    terrain3d_overlay_.add_overlay(terrain3d_debug_overlay_);
    terrain3d_compass_overlay_.set_halign(Gtk::Align::END);
    terrain3d_compass_overlay_.set_valign(Gtk::Align::START);
    terrain3d_compass_overlay_.set_margin(8);
    terrain3d_compass_overlay_.set_text("N: --");
    terrain3d_compass_overlay_.add_css_class("terrain3d-status");
    terrain3d_overlay_.add_overlay(terrain3d_compass_overlay_);
    terrain3d_box_.append(terrain3d_overlay_);
    right_notebook_.append_page(terrain3d_box_, "Terrain 3D");

    set_end_child(right_notebook_);

    // Set initial paned position for objects page after realization
    objects_paned_.signal_realize().connect([this]() {
        Glib::signal_idle().connect_once([this]() {
            objects_paned_.set_position(objects_paned_.get_height() / 2);
        });
    });

    // Signals
    scan_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpInfo::on_scan));
    folder_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpInfo::on_folder_browse));
    source_combo_.signal_changed().connect(sigc::mem_fun(*this, &TabWrpInfo::on_source_changed));
    filter_entry_.signal_changed().connect(sigc::mem_fun(*this, &TabWrpInfo::on_filter_changed));
    file_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabWrpInfo::on_file_selected));
    class_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabWrpInfo::on_class_selected));
    class_list_.signal_row_activated().connect(sigc::mem_fun(*this, &TabWrpInfo::on_class_activated));
    hm_export_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpInfo::on_hm_export));
    terrain3d_wireframe_btn_.signal_toggled().connect([this]() {
        terrain3d_view_.set_wireframe(terrain3d_wireframe_btn_.get_active());
    });
    auto update_camera_mode_button = [this]() {
        if (terrain3d_camera_mode_btn_.get_active()) {
            terrain3d_camera_mode_btn_.set_icon_name("object-rotate-right-symbolic");
            terrain3d_camera_mode_btn_.set_tooltip_text(
                "Orbit camera (click to switch to first person)");
        } else {
            terrain3d_camera_mode_btn_.set_icon_name("input-keyboard-symbolic");
            terrain3d_camera_mode_btn_.set_tooltip_text(
                "First-person camera (click to switch to orbit)");
        }
    };
    terrain3d_camera_mode_btn_.signal_toggled().connect([this, update_camera_mode_button]() {
        terrain3d_view_.set_camera_mode(
            terrain3d_camera_mode_btn_.get_active()
                ? wrpterrain::CameraMode::Orbit
                : wrpterrain::CameraMode::FirstPerson);
        update_camera_mode_button();
    });
    update_camera_mode_button();
    terrain3d_objects_btn_.signal_toggled().connect([this]() {
        terrain3d_view_.set_show_objects(terrain3d_objects_btn_.get_active());
        if (terrain3d_objects_btn_.get_active()) ensure_objects_loaded();
    });
    terrain3d_patch_bounds_btn_.signal_toggled().connect([this]() {
        terrain3d_view_.set_show_patch_boundaries(terrain3d_patch_bounds_btn_.get_active());
    });
    terrain3d_lod_tint_btn_.signal_toggled().connect([this]() {
        terrain3d_view_.set_show_patch_lod_colors(terrain3d_lod_tint_btn_.get_active());
    });
    terrain3d_tile_bounds_btn_.signal_toggled().connect([this]() {
        terrain3d_view_.set_show_tile_boundaries(terrain3d_tile_bounds_btn_.get_active());
    });
    terrain3d_far_scale_.signal_value_changed().connect([this]() {
        terrain3d_view_.set_terrain_far_distance(
            static_cast<float>(terrain3d_far_scale_.get_value()));
    });
    auto update_material_distances = [this]() {
        terrain3d_view_.set_material_quality_distances(
            static_cast<float>(terrain3d_mid_scale_.get_value()),
            static_cast<float>(terrain3d_far_mat_scale_.get_value()));
    };
    terrain3d_mid_scale_.signal_value_changed().connect(update_material_distances);
    terrain3d_far_mat_scale_.signal_value_changed().connect(update_material_distances);
    terrain3d_seam_debug_combo_.signal_changed().connect([this]() {
        const auto id = std::string(terrain3d_seam_debug_combo_.get_active_id());
        if (id == "depth") terrain3d_view_.set_seam_debug_mode(1);
        else if (id == "normals") terrain3d_view_.set_seam_debug_mode(2);
        else terrain3d_view_.set_seam_debug_mode(0);
    });
    terrain3d_mode_combo_.signal_changed().connect([this]() {
        auto id = std::string(terrain3d_mode_combo_.get_active_id());
        if (id == "texture" && !allow_texture_mode_) {
            terrain3d_mode_combo_.set_active_id("elevation");
            return;
        }
        if (id == "satellite" && !allow_satellite_mode_) {
            terrain3d_mode_combo_.set_active_id("elevation");
            return;
        }
        if (id == "surface") terrain3d_view_.set_color_mode(1);
        else if (id == "texture") terrain3d_view_.set_color_mode(2);
        else if (id == "satellite") {
            terrain3d_view_.set_color_mode(3);
            ensure_satellite_palette_loaded();
        }
        else terrain3d_view_.set_color_mode(0);
        if (id != "texture") {
            terrain3d_debug_overlay_.set_text("");
            terrain3d_debug_overlay_.set_visible(false);
        }
    });
    terrain3d_view_.set_on_object_picked([this](size_t idx) {
        if (!world_data_ || idx >= world_data_->objects.size()) return;
        const auto& obj = world_data_->objects[idx];
        std::ostringstream ss;
        ss << "Object #" << idx << ": " << obj.model_name
           << " @ [" << obj.position[0] << ", " << obj.position[1] << ", " << obj.position[2] << "]";
        terrain3d_base_status_ = ss.str();
        terrain3d_status_label_.set_text(terrain3d_base_status_);
    });
    terrain3d_view_.set_on_terrain_stats([this](const std::string& text) {
        if (terrain3d_base_status_.empty()) {
            terrain3d_status_label_.set_text(text);
        } else if (text.empty()) {
            terrain3d_status_label_.set_text(terrain3d_base_status_);
        } else {
            terrain3d_status_label_.set_text(terrain3d_base_status_ + " | " + text);
        }
    });
    terrain3d_view_.set_on_texture_debug_info([this](const std::string& text) {
        if (text.empty() || terrain3d_mode_combo_.get_active_id() != "texture") {
            terrain3d_debug_overlay_.set_text("");
            terrain3d_debug_overlay_.set_visible(false);
            return;
        }
        terrain3d_debug_overlay_.set_text(text);
        terrain3d_debug_overlay_.set_visible(true);
    });
    terrain3d_view_.set_on_compass_info([this](const std::string& text) {
        terrain3d_compass_overlay_.set_text(text.empty() ? "N: --" : text);
    });
    right_notebook_.signal_switch_page().connect([this](Gtk::Widget*, guint page_num) {
        if (page_num == 1) ensure_objects_loaded();
    });
}

void TabWrpInfo::update_terrain3d_mode_options(bool allow_texture, bool allow_satellite) {
    allow_texture_mode_ = allow_texture;
    allow_satellite_mode_ = allow_satellite;
    const auto prev = std::string(terrain3d_mode_combo_.get_active_id());

    terrain3d_mode_combo_.remove_all();
    terrain3d_mode_combo_.append("elevation", "Elevation");
    terrain3d_mode_combo_.append("surface", "Surface Mask");
    if (allow_texture_mode_) terrain3d_mode_combo_.append("texture", "Texture Index");
    if (allow_satellite_mode_) terrain3d_mode_combo_.append("satellite", "Satellite");

    std::string next = "elevation";
    if (!prev.empty()) {
        if (prev == "surface" || prev == "elevation") next = prev;
        else if (prev == "texture" && allow_texture_mode_) next = prev;
        else if (prev == "satellite" && allow_satellite_mode_) next = prev;
    }
    terrain3d_mode_combo_.set_active_id(next);
}

TabWrpInfo::~TabWrpInfo() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    ++scan_generation_;
    ++load_generation_;
    if (scan_thread_.joinable()) {
        scan_thread_.request_stop();
        scan_thread_.join();
    }
    loading_ = false;
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    objects_loading_ = false;
    if (objects_worker_.joinable()) {
        objects_worker_.request_stop();
        objects_worker_.join();
    }
    satellite_loading_ = false;
    if (satellite_worker_.joinable()) {
        satellite_worker_.request_stop();
        satellite_worker_.join();
    }
}

void TabWrpInfo::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabWrpInfo::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_panel_.set_model_loader_service(service);
    terrain3d_view_.set_model_loader_service(service);
}

void TabWrpInfo::set_texture_loader_service(
    const std::shared_ptr<TexturesLoaderService>& service) {
    texture_loader_service_ = service;
    model_panel_.set_texture_loader_service(service);
    terrain3d_view_.set_texture_loader_service(service);
}

void TabWrpInfo::set_on_open_p3d_info(std::function<void(const std::string&)> cb) {
    on_open_p3d_info_ = std::move(cb);
}

void TabWrpInfo::set_config(Config* cfg) {
    cfg_ = cfg;
    if (cfg_ && !cfg_->worlds_dir.empty()) {
        scan_dir_ = cfg_->worlds_dir;
        on_scan();
    }

    // Load PBO index in background (same pattern as TabObjReplace)
    db_.reset();
    index_.reset();
    model_panel_.set_config(cfg_);
    model_panel_.set_pboindex(nullptr, nullptr);

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
        index_ = snap.index;
        model_panel_.set_pboindex(db_.get(), index_.get());
        refresh_source_combo();
        if (!snap.error.empty()) {
            app_log(LogLevel::Warning, "WrpInfo: Failed to open PBO index: " + snap.error);
        } else if (db_ && index_) {
            app_log(LogLevel::Info, "WrpInfo: PBO index loaded ("
                    + std::to_string(snap.prefix_count) + " prefixes)");
        }
    });
}

void TabWrpInfo::on_folder_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->select_folder_finish(result);
                if (file) {
                    scan_dir_ = file->get_path();
                    on_scan();
                }
            } catch (const std::exception& e) {
                app_log(LogLevel::Warning, "WrpInfo: folder dialog failed: " + std::string(e.what()));
            } catch (...) {
                app_log(LogLevel::Warning, "WrpInfo: folder dialog failed");
            }
        });
}

void TabWrpInfo::on_scan() {
    if (!db_ && scan_dir_.empty()) return;
    const auto dir = scan_dir_;
    const auto db = db_;
    const auto source = current_source_;
    const unsigned gen = ++scan_generation_;
    class_status_label_.set_text("Scanning WRP files...");
    if (scan_thread_.joinable()) {
        scan_thread_.request_stop();
        scan_thread_.join();
    }
    scan_thread_ = std::jthread([this, dir, db, source, gen](std::stop_token st) {
        if (st.stop_requested()) return;
        std::vector<WrpFileEntry> files;
        std::string err;
        try {
            if (db) {
                auto results = db->find_files("*.wrp", source);
                files.reserve(results.size());
                for (const auto& r : results) {
                    WrpFileEntry e;
                    e.from_pbo = true;
                    e.pbo_path = r.pbo_path;
                    e.entry_name = r.file_path;
                    e.full_path = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
                    e.display = fs::path(r.file_path).filename().string();
                    if (e.display.empty()) e.display = fs::path(e.full_path).filename().string();
                    e.source = source;
                    files.push_back(std::move(e));
                }
            } else {
                std::error_code ec;
                for (auto& entry : fs::recursive_directory_iterator(
                         dir, fs::directory_options::skip_permission_denied, ec)) {
                    if (!entry.is_regular_file()) continue;
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext != ".wrp") continue;
                    WrpFileEntry e;
                    e.from_pbo = false;
                    e.full_path = entry.path().string();
                    e.display = entry.path().filename().string();
                    files.push_back(std::move(e));
                }
            }
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "unknown error";
        }
        if (st.stop_requested()) return;
        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) { return a.full_path < b.full_path; });
        Glib::signal_idle().connect_once([this, files = std::move(files),
                                          gen, err = std::move(err)]() mutable {
            if (gen != scan_generation_.load()) return;
            if (!err.empty()) {
                class_status_label_.set_text("Scan failed: " + err);
                app_log(LogLevel::Warning, "WrpInfo scan failed: " + err);
                return;
            }
            wrp_files_ = std::move(files);
            on_filter_changed();
            class_status_label_.set_text("Ready");
        });
    });
}

void TabWrpInfo::scan_wrp_files(const std::string& dir) {
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wrp") {
                WrpFileEntry e;
                e.from_pbo = false;
                e.full_path = entry.path().string();
                e.display = entry.path().filename().string();
                wrp_files_.push_back(std::move(e));
            }
        }
    }
}

void TabWrpInfo::on_filter_changed() {
    auto filter = std::string(filter_entry_.get_text());
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    filtered_files_.clear();
    for (const auto& f : wrp_files_) {
        const std::string haystack = f.full_path + " " + f.display;
        if (filter.empty()) {
            filtered_files_.push_back(f);
        } else {
            auto lower = haystack;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(filter) != std::string::npos) {
                filtered_files_.push_back(f);
            }
        }
    }
    update_file_list();
}

void TabWrpInfo::update_file_list() {
    while (auto* row = file_list_.get_row_at_index(0)) {
        file_list_.remove(*row);
    }

    for (const auto& f : filtered_files_) {
        auto* label = Gtk::make_managed<Gtk::Label>(f.display);
        label->set_halign(Gtk::Align::START);
        label->set_tooltip_text(f.from_pbo
            ? (f.full_path + " [" + f.pbo_path + "]")
            : f.full_path);
        file_list_.append(*label);
    }
}

void TabWrpInfo::on_file_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    int idx = row->get_index();
    if (idx < 0 || idx >= static_cast<int>(filtered_files_.size())) return;
    load_wrp(filtered_files_[static_cast<size_t>(idx)]);
}

void TabWrpInfo::load_wrp(const WrpFileEntry& entry) {
    if (loading_) return;
    loading_ = true;
    const unsigned gen = ++load_generation_;
    objects_loaded_ = false;
    objects_loading_ = false;
    satellite_loaded_ = false;
    satellite_loading_ = false;
    satellite_palette_.clear();

    info_view_.get_buffer()->set_text("Loading " + entry.full_path + "...");
    hm_picture_.set_paintable({});
    class_status_label_.set_text("Objects deferred (open Objects tab to load)");
    terrain3d_status_label_.set_text("Loading terrain...");
    terrain3d_compass_overlay_.set_text("N: --");
    terrain3d_camera_mode_btn_.set_active(true);
    terrain3d_view_.set_camera_mode(wrpterrain::CameraMode::Orbit);
    terrain3d_view_.clear_world();
    terrain3d_view_.set_satellite_palette({});

    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    if (objects_worker_.joinable()) {
        objects_worker_.request_stop();
        objects_worker_.join();
    }
    if (satellite_worker_.joinable()) {
        satellite_worker_.request_stop();
        satellite_worker_.join();
    }

    worker_ = std::jthread([this, entry, gen](std::stop_token st) {
        std::string info_text;
        std::shared_ptr<armatools::wrp::WorldData> wd_ptr;

        try {
            if (st.stop_requested()) return;
            std::unique_ptr<std::istream> in;
            std::istringstream mem_stream;
            std::ifstream file_stream;
            if (entry.from_pbo) {
                auto bytes = extract_from_pbo(entry.pbo_path, entry.entry_name);
                if (!bytes.empty()) {
                    std::string buf(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    mem_stream = std::istringstream(std::move(buf), std::ios::binary);
                    in = std::make_unique<std::istringstream>(std::move(mem_stream));
                }
            } else {
                file_stream = std::ifstream(entry.full_path, std::ios::binary);
                if (file_stream.is_open()) in = std::make_unique<std::ifstream>(std::move(file_stream));
            }

            if (!in || !*in) {
                info_text = entry.from_pbo ? "Error: Cannot extract WRP from PBO"
                                           : "Error: Cannot open file";
            } else {
                // Fast path: load terrain first, defer objects for very large worlds.
                armatools::wrp::Options opts;
                opts.no_objects = true;
                auto wd = armatools::wrp::read(*in, opts);

                std::ostringstream ss;
                ss << "File: " << entry.full_path << "\n";
                if (entry.from_pbo) ss << "PBO: " << entry.pbo_path << "\n";
                ss << "\n";
                ss << "Format: " << wd.format.signature << " v" << wd.format.version << "\n";
                ss << "Grid: " << wd.grid.cells_x << " x " << wd.grid.cells_y
                   << " (cell size: " << wd.grid.cell_size << ")\n";
                ss << "Terrain: " << wd.grid.terrain_x << " x " << wd.grid.terrain_y << "\n";
                ss << "World size: " << wd.bounds.world_size_x << " x " << wd.bounds.world_size_y << "\n";
                ss << "Elevation: " << wd.bounds.min_elevation << " to " << wd.bounds.max_elevation << "\n\n";
                ss << "Textures: " << wd.stats.texture_count << "\n";
                ss << "Models: " << wd.stats.model_count << "\n";
                ss << "Objects: deferred (fast load mode)\n";
                ss << "Peaks: " << wd.stats.peak_count << "\n";
                ss << "Road nets: " << wd.stats.road_net_count << "\n";

                if (wd.stats.has_cell_flags) {
                    ss << "\nCell flags:\n";
                    ss << "  Forest: " << wd.stats.cell_flags.forest_cells << "\n";
                    ss << "  Roadway: " << wd.stats.cell_flags.roadway_cells << "\n";
                    ss << "  Total: " << wd.stats.cell_flags.total_cells << "\n";
                    ss << "  Surface - ground: " << wd.stats.cell_flags.surface.ground
                       << ", tidal: " << wd.stats.cell_flags.surface.tidal
                       << ", coastline: " << wd.stats.cell_flags.surface.coastline
                       << ", sea: " << wd.stats.cell_flags.surface.sea << "\n";
                }

                if (!wd.warnings.empty()) {
                    ss << "\nWarnings:\n";
                    for (const auto& w : wd.warnings) {
                        ss << "  [" << w.code << "] " << w.message << "\n";
                    }
                }

                info_text = ss.str();
                wd_ptr = std::make_shared<armatools::wrp::WorldData>(std::move(wd));
            }
        } catch (const std::exception& e) {
            info_text = std::string("Error: ") + e.what();
        }

        if (st.stop_requested()) return;
        Glib::signal_idle().connect_once([this, info_text = std::move(info_text),
                                          wd_ptr = std::move(wd_ptr), entry, gen]() {
            if (gen != load_generation_.load()) return;
            info_view_.get_buffer()->set_text(info_text);

            if (wd_ptr) {
                // Render heightmap
                if (!wd_ptr->elevations.empty() && wd_ptr->grid.terrain_x > 0 && wd_ptr->grid.terrain_y > 0) {
                    auto texture = render_heightmap(wd_ptr->elevations,
                                                    wd_ptr->grid.terrain_x, wd_ptr->grid.terrain_y);
                    if (texture) hm_picture_.set_paintable(texture);
                }

                // Store world data for later use
                world_data_ = std::make_unique<armatools::wrp::WorldData>(std::move(*wd_ptr));
                loaded_wrp_path_ = entry.from_pbo ? std::string() : entry.full_path;
                loaded_wrp_entry_ = entry;
                loaded_wrp_entry_valid_ = true;

                update_terrain3d_mode_options(true, true);
                terrain3d_view_.set_world_data(*world_data_);
                terrain3d_camera_mode_btn_.set_active(true);
                terrain3d_view_.set_camera_mode(wrpterrain::CameraMode::Orbit);
                if (std::string(terrain3d_mode_combo_.get_active_id()) == "satellite")
                    ensure_satellite_palette_loaded();
                std::ostringstream terrain_status;
                terrain_status << world_data_->grid.terrain_x << "x" << world_data_->grid.terrain_y
                               << " cells, objects: deferred"
                               << " (LMB look, MMB pan, wheel zoom, camera toggle on toolbar)";
                terrain3d_base_status_ = terrain_status.str();
                terrain3d_status_label_.set_text(terrain3d_base_status_);
                while (auto* row = class_list_.get_row_at_index(0))
                    class_list_.remove(*row);
                class_entries_.clear();
                model_panel_.clear();
            } else {
                terrain3d_base_status_ = "Failed to load terrain";
                terrain3d_status_label_.set_text(terrain3d_base_status_);
            }

            loading_ = false;
        });
    });
}

void TabWrpInfo::refresh_source_combo() {
    source_combo_updating_ = true;
    source_combo_.remove_all();
    source_combo_.append("", "All");

    if (db_) {
        static const std::unordered_map<std::string, std::string> source_labels = {
            {"arma3", "Arma 3"},
            {"workshop", "Workshop"},
            {"ofp", "OFP/CWA"},
            {"arma1", "Arma 1"},
            {"arma2", "Arma 2"},
            {"custom", "Custom"},
        };
        auto sources = db_->query_sources();
        for (const auto& src : sources) {
            auto it = source_labels.find(src);
            source_combo_.append(src, it != source_labels.end() ? it->second : src);
        }
    }

    source_combo_.set_active_id(current_source_);
    source_combo_updating_ = false;
}

void TabWrpInfo::on_source_changed() {
    if (source_combo_updating_) return;
    current_source_ = std::string(source_combo_.get_active_id());
    on_scan();
}

namespace {
TabWrpInfo::ClassListSnapshot build_class_list_snapshot(
    const std::vector<armatools::wrp::ObjectRecord>& objects) {
    TabWrpInfo::ClassListSnapshot snapshot;
    snapshot.total_objects = static_cast<int>(objects.size());
    if (objects.empty()) return snapshot;

    std::map<std::string, std::map<std::string, int>> cat_models;
    for (const auto& obj : objects) {
        auto model = armatools::armapath::to_slash_lower(obj.model_name);
        auto category = armatools::objcat::category(model);
        cat_models[category][model]++;
    }

    snapshot.groups.reserve(cat_models.size());
    for (auto& [category, models] : cat_models) {
        TabWrpInfo::ClassListSnapshot::CategoryGroup group;
        group.name = category;
        std::vector<std::pair<std::string, int>> sorted_models(models.begin(), models.end());
        std::sort(sorted_models.begin(), sorted_models.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second != b.second) return a.second > b.second;
                      return a.first < b.first;
                  });
        group.entries.reserve(sorted_models.size());
        for (const auto& model_pair : sorted_models) {
            group.entries.push_back(TabWrpInfo::ClassEntry{category,
                                                          model_pair.first,
                                                          model_pair.second});
        }
        snapshot.groups.push_back(std::move(group));
    }
    return snapshot;
}
} // namespace

void TabWrpInfo::populate_class_list(const ClassListSnapshot& snapshot) {
    while (auto* row = class_list_.get_row_at_index(0)) {
        class_list_.remove(*row);
    }
    class_entries_.clear();
    model_panel_.clear();

    if (!world_data_ || snapshot.total_objects == 0) {
        class_status_label_.set_text("No objects in this WRP");
        return;
    }

    for (const auto& group : snapshot.groups) {
        auto* header_label = Gtk::make_managed<Gtk::Label>();
        header_label->set_markup("<b>" + Glib::Markup::escape_text(group.name) + "</b>");
        header_label->set_halign(Gtk::Align::START);
        header_label->set_margin(4);
        header_label->set_margin_top(8);
        auto* header_row = Gtk::make_managed<Gtk::ListBoxRow>();
        header_row->set_child(*header_label);
        header_row->set_activatable(false);
        header_row->set_selectable(false);
        class_list_.append(*header_row);

        for (const auto& entry : group.entries) {
            auto basename = fs::path(entry.model_name).filename().string();
            auto row_text = "  " + basename + "  (" + std::to_string(entry.count) + ")";

            auto* label = Gtk::make_managed<Gtk::Label>(row_text);
            label->set_halign(Gtk::Align::START);
            label->set_tooltip_text(entry.model_name);
            class_list_.append(*label);
            class_entries_.push_back(entry);
        }
    }

    class_status_label_.set_text(
        std::to_string(class_entries_.size()) + " unique models, "
        + std::to_string(snapshot.total_objects) + " objects, "
        + std::to_string(static_cast<int>(snapshot.groups.size())) + " categories");
}

void TabWrpInfo::ensure_objects_loaded() {
    if (!world_data_ || !loaded_wrp_entry_valid_) return;
    if (objects_loaded_ || objects_loading_) return;

    const unsigned gen = load_generation_.load();
    const auto entry = loaded_wrp_entry_;
    objects_loading_ = true;
    class_status_label_.set_text("Loading objects...");

    if (objects_worker_.joinable()) {
        objects_worker_.request_stop();
        objects_worker_.join();
    }
    objects_worker_ = std::jthread([this, entry, gen](std::stop_token st) {
        std::shared_ptr<armatools::wrp::WorldData> wd_ptr;
        std::string err;
        try {
            if (st.stop_requested()) return;
            std::unique_ptr<std::istream> in;
            std::istringstream mem_stream;
            std::ifstream file_stream;
            if (entry.from_pbo) {
                auto bytes = extract_from_pbo(entry.pbo_path, entry.entry_name);
                if (!bytes.empty()) {
                    std::string buf(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    mem_stream = std::istringstream(std::move(buf), std::ios::binary);
                    in = std::make_unique<std::istringstream>(std::move(mem_stream));
                }
            } else {
                file_stream = std::ifstream(entry.full_path, std::ios::binary);
                if (file_stream.is_open()) in = std::make_unique<std::ifstream>(std::move(file_stream));
            }

            if (!in || !*in) {
                err = entry.from_pbo ? "Cannot extract file from PBO" : "Cannot open file";
            } else {
                armatools::wrp::Options opts;
                opts.no_objects = false;
                auto wd = armatools::wrp::read(*in, opts);
                wd_ptr = std::make_shared<armatools::wrp::WorldData>(std::move(wd));
            }
        } catch (const std::exception& e) {
            err = e.what();
        }

        if (st.stop_requested()) return;
        TabWrpInfo::ClassListSnapshot class_snapshot;
        if (wd_ptr && err.empty())
            class_snapshot = build_class_list_snapshot(wd_ptr->objects);

        Glib::signal_idle().connect_once([this, wd_ptr = std::move(wd_ptr),
                                          err = std::move(err), gen,
                                          snapshot = std::move(class_snapshot)]() {
            if (gen != load_generation_.load()) return;
            objects_loading_ = false;
            if (!wd_ptr || !err.empty()) {
                class_status_label_.set_text("Objects load failed: " + err);
                return;
            }
            if (!world_data_) {
                class_status_label_.set_text("Objects loaded, world not active");
                return;
            }

            world_data_->objects = std::move(wd_ptr->objects);
            world_data_->models = std::move(wd_ptr->models);
            world_data_->stats.object_count = static_cast<int>(world_data_->objects.size());
            world_data_->stats.model_count = static_cast<int>(world_data_->models.size());
            objects_loaded_ = true;

            terrain3d_view_.set_objects(world_data_->objects);
            populate_class_list(snapshot);
        });
    });
}

void TabWrpInfo::ensure_satellite_palette_loaded() {
    if (!world_data_ || world_data_->textures.empty()) return;
    if (satellite_loaded_ || satellite_loading_) return;

    const unsigned gen = load_generation_.load();
    const auto index = index_;
    const auto db = db_;
    const auto cfg = cfg_;
    const auto wrp_path = loaded_wrp_path_;
    const auto textures = world_data_->textures;

    satellite_loading_ = true;
    if (satellite_worker_.joinable()) {
        satellite_worker_.request_stop();
        satellite_worker_.join();
    }
    satellite_worker_ = std::jthread([this, gen, index, db, cfg, wrp_path, textures](std::stop_token st) {
        auto fallback_color = [](size_t idx) -> std::array<float, 3> {
            const float n = static_cast<float>(idx + 1);
            const float x = std::fmod(std::sin(n * 12.9898f) * 43758.5453f, 1.0f);
            const float y = std::fmod(std::sin((n + 17.0f) * 78.233f) * 12345.6789f, 1.0f);
            const float z = std::fmod(std::sin((n + 37.0f) * 45.164f) * 24680.1357f, 1.0f);
            auto fix = [](float v) { return v < 0.0f ? v + 1.0f : v; };
            return {0.20f + 0.75f * fix(x),
                    0.20f + 0.75f * fix(y),
                    0.20f + 0.75f * fix(z)};
        };

        auto try_decode = [](const std::vector<uint8_t>& data,
                             std::array<float, 3>& out) -> bool {
            if (data.empty()) return false;
            try {
                std::string buf(reinterpret_cast<const char*>(data.data()), data.size());
                std::istringstream iss(buf, std::ios::binary);
                auto [img, _] = armatools::paa::decode(iss);
                if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) return false;
                const int step = std::max(1, std::max(img.width, img.height) / 64);
                uint64_t rs = 0, gs = 0, bs = 0, n = 0;
                for (int y = 0; y < img.height; y += step) {
                    for (int x = 0; x < img.width; x += step) {
                        const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(img.width)
                                          + static_cast<size_t>(x)) * 4;
                        rs += img.pixels[off + 0];
                        gs += img.pixels[off + 1];
                        bs += img.pixels[off + 2];
                        n++;
                    }
                }
                if (n == 0) return false;
                out = {static_cast<float>(rs) / (255.0f * static_cast<float>(n)),
                       static_cast<float>(gs) / (255.0f * static_cast<float>(n)),
                       static_cast<float>(bs) / (255.0f * static_cast<float>(n))};
                return true;
            } catch (...) {
                return false;
            }
        };

        auto normalize_asset_path = [](const std::string& raw) -> std::string {
            auto p = armatools::armapath::to_slash_lower(raw);
            while (!p.empty() && (p[0] == '/' || p[0] == '\\')) p.erase(p.begin());
            while (!p.empty() && (p[0] == '.' || p[0] == ' ')) {
                if (p.size() >= 2 && p[0] == '.' && (p[1] == '/' || p[1] == '\\')) {
                    p.erase(0, 2);
                } else if (p[0] == ' ') {
                    p.erase(p.begin());
                } else {
                    break;
                }
            }
            return p;
        };

        auto resolve_relative = [&](const std::string& base, const std::string& rel) -> std::string {
            auto nrel = normalize_asset_path(rel);
            if (nrel.empty()) return {};
            if (nrel.find(':') != std::string::npos) return normalize_asset_path(nrel);
            if (nrel.starts_with("ca/") || nrel.starts_with("a3/")
                || nrel.starts_with("cup/") || nrel.starts_with("dz/")) {
                return nrel;
            }
            fs::path bp = fs::path(normalize_asset_path(base)).parent_path() / fs::path(nrel);
            return normalize_asset_path(bp.generic_string());
        };

        std::vector<fs::path> disk_roots;
        if (cfg && !cfg->drive_root.empty())
            disk_roots.push_back(fs::path(cfg->drive_root));
        if (!wrp_path.empty()) {
            fs::path p = fs::path(wrp_path).parent_path();
            while (!p.empty()) {
                auto n = armatools::armapath::to_slash_lower(p.filename().string());
                if (n == "worlds" || n == "p") {
                    disk_roots.push_back(p);
                    break;
                }
                p = p.parent_path();
            }
            disk_roots.push_back(fs::path(wrp_path).parent_path());
        }

        auto load_asset_bytes = [&](const std::string& raw_path) -> std::vector<uint8_t> {
            const auto normalized = normalize_asset_path(raw_path);
            if (normalized.empty()) return {};

            if (index) {
                armatools::pboindex::ResolveResult rr;
                if (index->resolve(normalized, rr)) {
                    auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
                    if (!data.empty()) return data;
                }
            }
            if (db) {
                auto filename = fs::path(normalized).filename().string();
                auto results = db->find_files("*" + filename);
                for (const auto& r : results) {
                    auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
                    if (full == normalized || full.ends_with("/" + normalized)) {
                        auto data = extract_from_pbo(r.pbo_path, r.file_path);
                        if (!data.empty()) return data;
                    }
                }
            }
            for (const auto& root : disk_roots) {
                auto resolved = armatools::armapath::find_file_ci(root, normalized);
                if (!resolved) continue;
                std::ifstream tf(resolved->string(), std::ios::binary);
                if (tf.is_open()) {
                    return std::vector<uint8_t>((std::istreambuf_iterator<char>(tf)),
                                                 std::istreambuf_iterator<char>());
                }
            }
            return {};
        };

        auto extract_material_texture = [&](const std::string& material_path,
                                            const std::vector<uint8_t>& rvmat_data)
            -> std::optional<std::string> {
            if (rvmat_data.empty()) return std::nullopt;
            std::string text(reinterpret_cast<const char*>(rvmat_data.data()), rvmat_data.size());
            static const std::regex rx(
                "\"([^\"]+\\.(?:paa|pac))\"",
                std::regex_constants::icase);

            std::vector<std::string> candidates;
            for (auto it = std::sregex_iterator(text.begin(), text.end(), rx);
                 it != std::sregex_iterator(); ++it) {
                if (it->size() < 2) continue;
                auto p = (*it)[1].str();
                if (!p.empty()) candidates.push_back(p);
            }
            if (candidates.empty()) return std::nullopt;

            auto score = [](const std::string& p) {
                auto s = armatools::armapath::to_slash_lower(p);
                int v = 0;
                if (s.find("_mco.") != std::string::npos) v += 40;
                else if (s.find("_co.") != std::string::npos) v += 30;
                else if (s.find("_ca.") != std::string::npos) v += 20;
                if (s.find("_smdi.") != std::string::npos) v -= 25;
                if (s.find("_nohq.") != std::string::npos) v -= 25;
                if (s.find("_as.") != std::string::npos) v -= 20;
                return v;
            };

            std::string best = candidates.front();
            int best_score = score(best);
            for (const auto& c : candidates) {
                int sc = score(c);
                if (sc > best_score) {
                    best_score = sc;
                    best = c;
                }
            }
            auto resolved = resolve_relative(material_path, best);
            if (resolved.empty()) return std::nullopt;
            return resolved;
        };

        if (st.stop_requested()) return;
        std::vector<std::array<float, 3>> palette(textures.size());
        for (size_t i = 0; i < palette.size(); ++i) palette[i] = fallback_color(i);

        size_t decoded_count = 0;
        std::string err;
        try {
            for (size_t i = 0; i < textures.size(); ++i) {
                if (st.stop_requested()) return;
                auto tex = textures[i].filename;
                if (tex.empty()) continue;
                const auto normalized = normalize_asset_path(tex);
                std::array<float, 3> rgb{};

                auto try_paths = [&](const std::vector<std::string>& paths) -> bool {
                    for (const auto& p : paths) {
                        auto data = load_asset_bytes(p);
                        if (!data.empty() && try_decode(data, rgb)) return true;
                    }
                    return false;
                };

                bool ok = false;
                const auto ext = fs::path(normalized).extension().string();
                if (ext == ".paa" || ext == ".pac") {
                    ok = try_paths({normalized});
                } else {
                    auto mat_paths = std::vector<std::string>{normalized};
                    if (ext.empty()) mat_paths.push_back(normalized + ".rvmat");
                    for (const auto& mat : mat_paths) {
                        auto mat_data = load_asset_bytes(mat);
                        if (mat_data.empty()) continue;
                        if (auto tex_path = extract_material_texture(mat, mat_data)) {
                            auto ntex = normalize_asset_path(*tex_path);
                            ok = try_paths({ntex, ntex + ".paa", ntex + ".pac"});
                            if (ok) break;
                        }
                    }
                }
                if (ok) {
                    palette[i] = rgb;
                    decoded_count++;
                }
            }
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "unknown";
        }

        Glib::signal_idle().connect_once([this, gen, palette = std::move(palette),
                                          decoded_count, err = std::move(err)]() {
            if (gen != load_generation_.load()) return;
            satellite_loading_ = false;
            if (!err.empty()) {
                satellite_loaded_ = false;
                terrain3d_status_label_.set_text(
                    terrain3d_status_label_.get_text()
                    + " | satellite palette failed (" + err + ")");
                app_log(LogLevel::Warning, "WrpInfo: satellite palette build error: " + err);
                return;
            }
            satellite_loaded_ = true;
            satellite_palette_ = palette;
            terrain3d_view_.set_satellite_palette(satellite_palette_);
            auto status = terrain3d_status_label_.get_text();
            terrain3d_status_label_.set_text(
                status + " | satellite palette loaded (" + std::to_string(decoded_count) + ")");
            app_log(LogLevel::Debug,
                    "WrpInfo: satellite palette decoded " + std::to_string(decoded_count)
                    + "/" + std::to_string(satellite_palette_.size()));
        });
    });
}

void TabWrpInfo::on_class_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    if (class_entries_.empty()) {
        ensure_objects_loaded();
        return;
    }

    // Walk through rows to find which class_entry this corresponds to.
    // We need to skip header rows (non-selectable).
    // Count selectable rows up to this one.
    int selectable_idx = -1;
    for (int i = 0; ; ++i) {
        auto* r = class_list_.get_row_at_index(i);
        if (!r) break;
        if (r->get_selectable()) selectable_idx++;
        if (r == row) break;
    }

    if (selectable_idx < 0 || selectable_idx >= static_cast<int>(class_entries_.size())) return;

    const auto& entry = class_entries_[static_cast<size_t>(selectable_idx)];
    load_p3d_preview(entry.model_name);
}

void TabWrpInfo::on_class_activated(Gtk::ListBoxRow* row) {
    if (!row) return;
    if (class_entries_.empty()) return;

    int selectable_idx = -1;
    for (int i = 0; ; ++i) {
        auto* r = class_list_.get_row_at_index(i);
        if (!r) break;
        if (r->get_selectable()) selectable_idx++;
        if (r == row) break;
    }
    if (selectable_idx < 0 || selectable_idx >= static_cast<int>(class_entries_.size())) return;

    const auto& entry = class_entries_[static_cast<size_t>(selectable_idx)];
    if (on_open_p3d_info_) on_open_p3d_info_(entry.model_name);
}

void TabWrpInfo::load_p3d_preview(const std::string& model_path) {
    model_panel_.load_p3d(model_path);
    /*
    model_panel_.clear();
    if (model_path.empty()) return;

    // Try pboindex resolve first
    if (index_) {
        armatools::pboindex::ResolveResult rr;
        if (index_->resolve(model_path, rr)) {
            app_log(LogLevel::Debug, "WrpInfo: resolved " + model_path
                    + " -> " + rr.pbo_path + " : " + rr.entry_name);
            auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
            if (!data.empty()) {
                try {
                    std::string buf(reinterpret_cast<const char*>(data.data()), data.size());
                    std::istringstream iss(buf, std::ios::binary);
                    auto p3d = armatools::p3d::read(iss);
                    if (!p3d.lods.empty()) {
                        model_panel_.show_lod(p3d.lods[0], model_path);
                        return;
                    }
                } catch (const std::exception& e) {
                    app_log(LogLevel::Warning, "WrpInfo: P3D parse error (PBO): "
                            + std::string(e.what()));
                }
            }
        }
    }

    // Fallback: DB find_files with filename match
    if (db_) {
        auto normalized = armatools::armapath::to_slash_lower(model_path);
        auto filename = fs::path(normalized).filename().string();
        auto results = db_->find_files("*" + filename);
        for (const auto& r : results) {
            auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
            if (full == normalized || full.ends_with("/" + normalized)) {
                auto data = extract_from_pbo(r.pbo_path, r.file_path);
                if (!data.empty()) {
                    try {
                        std::string buf(reinterpret_cast<const char*>(data.data()), data.size());
                        std::istringstream iss(buf, std::ios::binary);
                        auto p3d = armatools::p3d::read(iss);
                        if (!p3d.lods.empty()) {
                            model_panel_.show_lod(p3d.lods[0], model_path);
                            return;
                        }
                    } catch (const std::exception& e) {
                        app_log(LogLevel::Warning, "WrpInfo: P3D parse error (find_files): "
                                + std::string(e.what()));
                    }
                }
            }
        }
    }

    // Fallback: disk via case-insensitive path resolution
    if (cfg_ && !cfg_->drive_root.empty()) {
        auto resolved = armatools::armapath::find_file_ci(
            fs::path(cfg_->drive_root), model_path);
        if (resolved) {
            try {
                std::ifstream f(resolved->string(), std::ios::binary);
                if (f.is_open()) {
                    auto p3d = armatools::p3d::read(f);
                    if (!p3d.lods.empty()) {
                        model_panel_.show_lod(p3d.lods[0], model_path);
                        return;
                    }
                }
            } catch (const std::exception& e) {
                app_log(LogLevel::Warning, "WrpInfo: P3D parse error (disk): "
                        + std::string(e.what()));
            }
        }
    }

    app_log(LogLevel::Debug, "WrpInfo: model not found: " + model_path);
    */
}

void TabWrpInfo::on_hm_export() {
    if (!world_data_ || world_data_->elevations.empty()) {
        app_log(LogLevel::Warning, "WrpInfo: No heightmap data to export");
        return;
    }

    auto dialog = Gtk::FileDialog::create();

    auto filter_asc = Gtk::FileFilter::create();
    filter_asc->set_name("ASCII Grid (.asc)");
    filter_asc->add_pattern("*.asc");

    auto filter_tif = Gtk::FileFilter::create();
    filter_tif->set_name("GeoTIFF (.tif) — native resolution only");
    filter_tif->add_pattern("*.tif");
    filter_tif->add_pattern("*.tiff");

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter_asc);
    filters->append(filter_tif);
    dialog->set_filters(filters);

    // Suggest a default filename
    auto stem = loaded_wrp_entry_valid_
        ? fs::path(loaded_wrp_entry_.full_path).stem().string()
        : fs::path(loaded_wrp_path_).stem().string();
    if (stem.empty()) stem = "heightmap";
    dialog->set_initial_name(stem + "_heightmap.asc");

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog, filter_tif](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (!file) return;

                auto output_path = file->get_path();
                auto ext = fs::path(output_path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                // Determine grid dimensions
                int width = world_data_->grid.terrain_x;
                int height = world_data_->grid.terrain_y;
                if (static_cast<int>(world_data_->elevations.size()) != width * height) {
                    width = world_data_->grid.cells_x;
                    height = world_data_->grid.cells_y;
                }

                int scale = 1;
                auto scale_id = std::string(hm_scale_combo_.get_active_id());
                if (!scale_id.empty()) scale = std::stoi(scale_id);

                if (ext == ".tif" || ext == ".tiff") {
                    // Export TIFF using wrp_heightmap tool (native resolution only)
                    auto wrp_path = loaded_wrp_path_;
                    if (wrp_path.empty()) {
                        app_log(LogLevel::Warning,
                                "WrpInfo: TIFF export requires a filesystem WRP path");
                        return;
                    }
                    if (!cfg_) {
                        app_log(LogLevel::Error, "WrpInfo: Configuration not available for wrp_heightmap");
                        return;
                    }
                    auto tool = resolve_tool_path(*cfg_, "wrp_heightmap");
                    if (tool.empty()) {
                        app_log(LogLevel::Error, "WrpInfo: wrp_heightmap binary not found");
                        return;
                    }
                    auto args = apply_tool_verbosity(cfg_,
                        {"-offset-x", "200000", "-offset-z", "0", wrp_path, output_path}, false);
                    auto res = run_subprocess(tool, args);
                    if (res.status == 0) {
                        app_log(LogLevel::Info, "WrpInfo: Exported GeoTIFF to " + output_path);
                    } else {
                        app_log(LogLevel::Error, "WrpInfo: wrp_heightmap failed: " + res.output);
                    }
                } else {
                    // Export ASC (with optional rescale)
                    int out_w = width;
                    int out_h = height;
                    double cell_size = world_data_->bounds.world_size_x / static_cast<double>(width);

                    const std::vector<float>* elev = &world_data_->elevations;
                    std::vector<float> resampled;

                    if (scale > 1) {
                        out_w = width * scale;
                        out_h = height * scale;
                        cell_size = world_data_->bounds.world_size_x / static_cast<double>(out_w);

                        // Bilinear resampling
                        resampled.resize(static_cast<size_t>(out_w) * static_cast<size_t>(out_h));
                        for (int dy = 0; dy < out_h; dy++) {
                            double sy = static_cast<double>(dy) * static_cast<double>(height - 1)
                                        / static_cast<double>(out_h - 1);
                            int y0 = static_cast<int>(sy);
                            int y1 = std::min(y0 + 1, height - 1);
                            double fy = sy - y0;

                            for (int dx = 0; dx < out_w; dx++) {
                                double sx = static_cast<double>(dx) * static_cast<double>(width - 1)
                                            / static_cast<double>(out_w - 1);
                                int x0 = static_cast<int>(sx);
                                int x1 = std::min(x0 + 1, width - 1);
                                double fx = sx - x0;

                                double v00 = (*elev)[static_cast<size_t>(y0 * width + x0)];
                                double v10 = (*elev)[static_cast<size_t>(y0 * width + x1)];
                                double v01 = (*elev)[static_cast<size_t>(y1 * width + x0)];
                                double v11 = (*elev)[static_cast<size_t>(y1 * width + x1)];
                                double v = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy)
                                         + v01 * (1 - fx) * fy + v11 * fx * fy;
                                resampled[static_cast<size_t>(dy * out_w + dx)] = static_cast<float>(v);
                            }
                        }
                        elev = &resampled;
                    }

                    // Write ASC file
                    std::ofstream f(output_path);
                    if (!f) {
                        app_log(LogLevel::Error, "WrpInfo: Cannot create " + output_path);
                        return;
                    }

                    double offset_x = 200000.0;
                    double offset_z = 0.0;

                    f << std::format("ncols         {}\n", out_w);
                    f << std::format("nrows         {}\n", out_h);
                    f << std::format("xllcorner     {:.6f}\n", offset_x);
                    f << std::format("yllcorner     {:.6f}\n", offset_z);
                    f << std::format("cellsize      {:.6f}\n", cell_size);
                    f << "NODATA_value  -9999\n";

                    // ESRI ASCII Grid: top-to-bottom; WRP: row 0 = south
                    for (int row = out_h - 1; row >= 0; row--) {
                        int offset = row * out_w;
                        for (int col = 0; col < out_w; col++) {
                            if (col > 0) f << ' ';
                            f << std::format("{:.4f}", (*elev)[static_cast<size_t>(offset + col)]);
                        }
                        f << '\n';
                    }

                    std::string scale_note = scale > 1
                        ? " (scale " + std::to_string(scale) + "x, " + std::to_string(out_w) + "x" + std::to_string(out_h) + ")"
                        : "";
                    app_log(LogLevel::Info, "WrpInfo: Exported ASC to " + output_path + scale_note);
                }
            } catch (...) {}
        });
}

Glib::RefPtr<Gdk::Texture> TabWrpInfo::render_heightmap(
    const std::vector<float>& elevations, int grid_x, int grid_y) {

    if (elevations.empty()) return {};

    float min_e = elevations[0], max_e = elevations[0];
    for (float e : elevations) {
        if (e < min_e) min_e = e;
        if (e > max_e) max_e = e;
    }
    float range = max_e - min_e;
    if (range < 0.001f) range = 1.0f;

    size_t gx = static_cast<size_t>(grid_x);
    size_t gy = static_cast<size_t>(grid_y);
    std::vector<uint8_t> pixels(gx * gy * 4);
    for (size_t y = 0; y < gy; ++y) {
        for (size_t x = 0; x < gx; ++x) {
            size_t src_y = gy - 1 - y;
            size_t src_idx = src_y * gx + x;
            float e = (src_idx < elevations.size()) ? elevations[src_idx] : 0.0f;
            uint8_t v = static_cast<uint8_t>(std::clamp((e - min_e) / range * 255.0f, 0.0f, 255.0f));
            size_t dst = (y * gx + x) * 4;
            pixels[dst] = v;
            pixels[dst + 1] = v;
            pixels[dst + 2] = v;
            pixels[dst + 3] = 255;
        }
    }

    auto pixbuf = Gdk::Pixbuf::create_from_data(
        pixels.data(), Gdk::Colorspace::RGB, true, 8,
        grid_x, grid_y, grid_x * 4);
    auto copy = pixbuf->copy();
    return Gdk::Texture::create_for_pixbuf(copy);
}
