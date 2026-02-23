#include "tab_wrp_info.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/objcat.h>
#include <armatools/p3d.h>
#include <armatools/pboindex.h>
#include <armatools/wrp.h>
#include <armatools/armapath.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

TabWrpInfo::TabWrpInfo() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    // Left panel: file scanner
    list_box_.set_margin(8);
    list_box_.set_size_request(180, -1);

    filter_entry_.set_hexpand(true);
    filter_entry_.set_placeholder_text("Filter...");
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
    filter_entry_.signal_changed().connect(sigc::mem_fun(*this, &TabWrpInfo::on_filter_changed));
    file_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabWrpInfo::on_file_selected));
    class_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabWrpInfo::on_class_selected));
    hm_export_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpInfo::on_hm_export));
}

TabWrpInfo::~TabWrpInfo() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    ++scan_generation_;
    if (scan_thread_.joinable()) scan_thread_.join();
    loading_ = false;
    if (worker_.joinable()) worker_.join();
}

void TabWrpInfo::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabWrpInfo::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_panel_.set_model_loader_service(service);
}

void TabWrpInfo::set_texture_loader_service(
    const std::shared_ptr<LodTexturesLoaderService>& service) {
    model_panel_.set_texture_loader_service(service);
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
            } catch (...) {}
        });
}

void TabWrpInfo::on_scan() {
    if (scan_dir_.empty()) return;
    const auto dir = scan_dir_;
    const unsigned gen = ++scan_generation_;
    class_status_label_.set_text("Scanning WRP files...");
    if (scan_thread_.joinable()) scan_thread_.join();
    scan_thread_ = std::thread([this, dir, gen]() {
        std::vector<std::string> files;
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wrp") files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        Glib::signal_idle().connect_once([this, files = std::move(files), gen]() mutable {
            if (gen != scan_generation_.load()) return;
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
                wrp_files_.push_back(entry.path().string());
            }
        }
    }
}

void TabWrpInfo::on_filter_changed() {
    auto filter = std::string(filter_entry_.get_text());
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    filtered_files_.clear();
    for (const auto& f : wrp_files_) {
        if (filter.empty()) {
            filtered_files_.push_back(f);
        } else {
            auto lower = f;
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
        auto* label = Gtk::make_managed<Gtk::Label>(fs::path(f).filename().string());
        label->set_halign(Gtk::Align::START);
        label->set_tooltip_text(f);
        file_list_.append(*label);
    }
}

void TabWrpInfo::on_file_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    int idx = row->get_index();
    if (idx < 0 || idx >= static_cast<int>(filtered_files_.size())) return;
    load_wrp(filtered_files_[static_cast<size_t>(idx)]);
}

void TabWrpInfo::load_wrp(const std::string& path) {
    if (loading_) return;
    loading_ = true;

    info_view_.get_buffer()->set_text("Loading " + path + "...");
    hm_picture_.set_paintable({});
    class_status_label_.set_text("Loading objects...");

    if (worker_.joinable()) worker_.join();

    worker_ = std::thread([this, path]() {
        std::string info_text;
        std::shared_ptr<armatools::wrp::WorldData> wd_ptr;

        try {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) {
                info_text = "Error: Cannot open file";
            } else {
                armatools::wrp::Options opts;
                // Load with objects (no_objects = false)
                auto wd = armatools::wrp::read(f, opts);

                std::ostringstream ss;
                ss << "File: " << path << "\n\n";
                ss << "Format: " << wd.format.signature << " v" << wd.format.version << "\n";
                ss << "Grid: " << wd.grid.cells_x << " x " << wd.grid.cells_y
                   << " (cell size: " << wd.grid.cell_size << ")\n";
                ss << "Terrain: " << wd.grid.terrain_x << " x " << wd.grid.terrain_y << "\n";
                ss << "World size: " << wd.bounds.world_size_x << " x " << wd.bounds.world_size_y << "\n";
                ss << "Elevation: " << wd.bounds.min_elevation << " to " << wd.bounds.max_elevation << "\n\n";
                ss << "Textures: " << wd.stats.texture_count << "\n";
                ss << "Models: " << wd.stats.model_count << "\n";
                ss << "Objects: " << wd.stats.object_count << "\n";
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

        Glib::signal_idle().connect_once([this, info_text = std::move(info_text),
                                          wd_ptr = std::move(wd_ptr), path]() {
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
                loaded_wrp_path_ = path;

                // Populate class list
                populate_class_list();
            }

            loading_ = false;
        });
    });
}

void TabWrpInfo::populate_class_list() {
    // Clear existing rows
    while (auto* row = class_list_.get_row_at_index(0)) {
        class_list_.remove(*row);
    }
    class_entries_.clear();
    model_panel_.clear();

    if (!world_data_ || world_data_->objects.empty()) {
        class_status_label_.set_text("No objects in this WRP");
        return;
    }

    // Build map<category, map<model_name, count>>
    std::map<std::string, std::map<std::string, int>> cat_models;
    for (const auto& obj : world_data_->objects) {
        auto model = armatools::armapath::to_slash_lower(obj.model_name);
        auto cat = armatools::objcat::category(model);
        cat_models[cat][model]++;
    }

    // Flatten into class_entries_, sorted by category then count desc
    int total_objects = 0;
    std::set<std::string> categories;
    for (const auto& [cat, models] : cat_models) {
        categories.insert(cat);
        // Sort models by count descending
        std::vector<std::pair<std::string, int>> sorted_models(models.begin(), models.end());
        std::sort(sorted_models.begin(), sorted_models.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        for (const auto& [model, count] : sorted_models) {
            class_entries_.push_back({cat, model, count});
            total_objects += count;
        }
    }

    // Populate the ListBox
    std::string current_cat;
    for (size_t i = 0; i < class_entries_.size(); ++i) {
        const auto& entry = class_entries_[i];

        // Section header for new category
        if (entry.category != current_cat) {
            current_cat = entry.category;
            auto* header_label = Gtk::make_managed<Gtk::Label>();
            header_label->set_markup("<b>" + Glib::Markup::escape_text(current_cat) + "</b>");
            header_label->set_halign(Gtk::Align::START);
            header_label->set_margin(4);
            header_label->set_margin_top(8);
            auto* header_row = Gtk::make_managed<Gtk::ListBoxRow>();
            header_row->set_child(*header_label);
            header_row->set_activatable(false);
            header_row->set_selectable(false);
            class_list_.append(*header_row);
        }

        // Model entry row
        auto basename = fs::path(entry.model_name).filename().string();
        auto row_text = "  " + basename + "  (" + std::to_string(entry.count) + ")";

        auto* label = Gtk::make_managed<Gtk::Label>(row_text);
        label->set_halign(Gtk::Align::START);
        label->set_tooltip_text(entry.model_name);
        class_list_.append(*label);
    }

    class_status_label_.set_text(
        std::to_string(class_entries_.size()) + " unique models, "
        + std::to_string(total_objects) + " objects, "
        + std::to_string(categories.size()) + " categories");
}

void TabWrpInfo::on_class_selected(Gtk::ListBoxRow* row) {
    if (!row) return;

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
    auto stem = fs::path(loaded_wrp_path_).stem().string();
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
