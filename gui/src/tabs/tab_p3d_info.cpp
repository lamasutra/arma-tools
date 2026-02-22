#include "tab_p3d_info.h"
#include "config.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/p3d.h>
#include <armatools/paa.h>
#include <armatools/pbo.h>
#include <armatools/pboindex.h>
#include <armatools/armapath.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

struct TabP3dInfo::ModelData {
    armatools::p3d::P3DFile p3d;
};

TabP3dInfo::TabP3dInfo() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    // Left panel
    left_box_.set_margin(8);
    left_box_.set_size_request(180, -1);

    // PBO mode switch
    pbo_label_.set_margin_end(2);
    path_box_.append(pbo_label_);

    path_box_.append(switch_box_);
    switch_box_.set_valign(Gtk::Align::CENTER);
    switch_box_.set_vexpand(false);
    switch_box_.append(pbo_switch_);

    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("P3D file path...");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    search_button_.set_visible(false);
    path_box_.append(search_button_);
    left_box_.append(path_box_);

    // Search results (PBO mode only)
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    left_box_.append(search_scroll_);

    // Toolbar (tab-specific: auto-extract only)
    toolbar_.set_margin_top(4);
    toolbar_.append(auto_extract_check_);
    left_box_.append(toolbar_);

    model_info_label_.set_halign(Gtk::Align::START);
    model_info_label_.set_wrap(true);
    model_info_label_.set_margin_top(4);
    left_box_.append(model_info_label_);

    lod_header_.set_halign(Gtk::Align::START);
    lod_header_.set_markup("<b>LODs</b>");
    lod_header_.set_margin_top(4);
    left_box_.append(lod_header_);

    lod_scroll_.set_child(lod_list_);
    lod_scroll_.set_size_request(-1, 120);
    left_box_.append(lod_scroll_);

    // Texture header (in left panel, below LOD list)
    texture_header_.set_halign(Gtk::Align::START);
    texture_header_.set_markup("<b>Textures</b>");
    texture_header_.set_visible(false);
    left_box_.append(texture_header_);

    // Texture list in a scrolled window (max height)
    texture_scroll_.set_child(texture_list_);
    texture_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    texture_scroll_.set_max_content_height(150);
    texture_scroll_.set_propagate_natural_height(true);
    texture_scroll_.set_visible(false);
    left_box_.append(texture_scroll_);

    // Extract row: button + spinner + status
    extract_button_.set_visible(false);
    extract_spinner_.set_visible(false);
    extract_spinner_.set_spinning(false);
    extract_status_.set_halign(Gtk::Align::START);
    extract_status_.set_visible(false);
    extract_row_.append(extract_button_);
    extract_row_.append(extract_spinner_);
    extract_row_.append(extract_status_);
    left_box_.append(extract_row_);

    // Detail text
    detail_view_.set_editable(false);
    detail_view_.set_monospace(true);
    detail_view_.set_wrap_mode(Gtk::WrapMode::WORD);
    detail_scroll_.set_child(detail_view_);
    detail_scroll_.set_vexpand(true);
    detail_scroll_.set_size_request(-1, 100);
    left_box_.append(detail_scroll_);

    set_start_child(left_box_);
    set_position(320);

    // Right panel: model view takes full space
    set_end_child(model_panel_);

    // Signals
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dInfo::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_file(path_entry_.get_text());
    });
    lod_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabP3dInfo::on_lod_selected));
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabP3dInfo::on_pbo_mode_changed));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dInfo::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabP3dInfo::on_search_result_selected));

    extract_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabP3dInfo::extract_missing_textures));
}

TabP3dInfo::~TabP3dInfo() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }
    if (extract_thread_.joinable())
        extract_thread_.join();
}

void TabP3dInfo::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabP3dInfo::set_config(Config* cfg) {
    cfg_ = cfg;
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
    });
}

void TabP3dInfo::on_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("P3D files");
    filter->add_pattern("*.p3d");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->open(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->open_finish(result);
                if (file) {
                    path_entry_.set_text(file->get_path());
                    load_file(file->get_path());
                }
            } catch (...) {}
        });
}

void TabP3dInfo::load_file(const std::string& path) {
    if (path.empty()) return;

    // Clear
    while (auto* row = lod_list_.get_row_at_index(0))
        lod_list_.remove(*row);
    detail_view_.get_buffer()->set_text("");
    model_.reset();
    model_path_.clear();
    model_panel_.clear();
    missing_textures_.clear();

    // Hide texture UI
    texture_header_.set_visible(false);
    texture_scroll_.set_visible(false);
    extract_button_.set_visible(false);
    extract_status_.set_visible(false);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }

    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            model_info_label_.set_text("Error: Cannot open file");
            return;
        }

        model_ = std::make_shared<ModelData>();
        model_->p3d = armatools::p3d::read(f);
        model_path_ = path;
        const auto& p = model_->p3d;

        app_log(LogLevel::Info, "Loaded P3D: " + path);

        // Model summary
        std::ostringstream info;
        info << "Format: " << p.format << " v" << p.version << "\n";
        info << "LODs: " << p.lods.size() << "\n";

        if (p.model_info) {
            info << "Mass: " << p.model_info->mass << "\n";
            info << "Armor: " << p.model_info->armor << "\n";
            info << "Bounding sphere: " << p.model_info->bounding_sphere << "\n";
        }

        auto size_result = armatools::p3d::calculate_size(p);
        if (size_result.info) {
            auto& s = *size_result.info;
            info << "Dimensions: " << s.dimensions[0] << " x "
                 << s.dimensions[1] << " x " << s.dimensions[2] << " m\n";
        }

        model_info_label_.set_text(info.str());

        // Populate LOD list
        for (const auto& lod : p.lods) {
            auto text = lod.resolution_name + "  (V:" + std::to_string(lod.vertex_count)
                      + " F:" + std::to_string(lod.face_count) + ")";
            auto* label = Gtk::make_managed<Gtk::Label>(text);
            label->set_halign(Gtk::Align::START);
            lod_list_.append(*label);
        }

        // Auto-select first visual LOD
        if (!p.lods.empty()) {
            lod_list_.select_row(*lod_list_.get_row_at_index(0));
        }

    } catch (const std::exception& e) {
        model_info_label_.set_text(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, "P3D load error: " + std::string(e.what()));
    }
}

void TabP3dInfo::on_lod_selected(Gtk::ListBoxRow* row) {
    if (!row || !model_) return;
    int idx = row->get_index();
    if (idx < 0 || static_cast<size_t>(idx) >= model_->p3d.lods.size()) return;

    const auto& lod = model_->p3d.lods[static_cast<size_t>(idx)];

    // Update 3D view (geometry + textures in one call)
    model_panel_.show_lod(lod, model_path_);

    // Update texture list UI
    update_texture_list(lod);

    // Update text details
    std::ostringstream detail;

    detail << "LOD: " << lod.resolution_name << " (resolution: " << lod.resolution << ")\n";
    detail << "Vertices: " << lod.vertex_count << "\n";
    detail << "Faces: " << lod.face_count << "\n";
    detail << "Bounding radius: " << lod.bounding_radius << "\n\n";

    if (!lod.materials.empty()) {
        detail << "Materials (" << lod.materials.size() << "):\n";
        for (const auto& m : lod.materials)
            detail << "  " << m << "\n";
        detail << "\n";
    }

    if (!lod.named_selections.empty()) {
        detail << "Named selections (" << lod.named_selections.size() << "):\n";
        for (const auto& s : lod.named_selections)
            detail << "  " << s << "\n";
        detail << "\n";
    }

    if (!lod.named_properties.empty()) {
        detail << "Named properties (" << lod.named_properties.size() << "):\n";
        for (const auto& p : lod.named_properties)
            detail << "  " << p.name << " = " << p.value << "\n";
        detail << "\n";
    }

    detail << "Bounding box:\n";
    detail << "  min: [" << lod.bounding_box_min[0] << ", " << lod.bounding_box_min[1]
           << ", " << lod.bounding_box_min[2] << "]\n";
    detail << "  max: [" << lod.bounding_box_max[0] << ", " << lod.bounding_box_max[1]
           << ", " << lod.bounding_box_max[2] << "]\n";

    detail_view_.get_buffer()->set_text(detail.str());

    // Auto-extract if enabled
    if (auto_extract_check_.get_active() && !missing_textures_.empty()) {
        extract_missing_textures();
    }
}

bool TabP3dInfo::resolve_texture_on_disk(const std::string& texture) const {
    return ::resolve_texture_on_disk(texture, model_path_,
                                     cfg_ ? cfg_->drive_root : "");
}

void TabP3dInfo::update_texture_list(const armatools::p3d::LOD& lod) {
    // Clear existing texture rows
    while (auto* child = texture_list_.get_first_child())
        texture_list_.remove(*child);
    missing_textures_.clear();

    if (lod.textures.empty()) {
        texture_header_.set_visible(false);
        texture_scroll_.set_visible(false);
        extract_button_.set_visible(false);
        extract_status_.set_visible(false);
        return;
    }

    texture_header_.set_markup("<b>Textures (" + std::to_string(lod.textures.size()) + "):</b>");
    texture_header_.set_visible(true);
    texture_scroll_.set_visible(true);

    std::unordered_set<std::string> seen;
    for (const auto& tex : lod.textures) {
        if (tex.empty()) continue;

        auto normalized = armatools::armapath::to_slash_lower(tex);
        if (seen.count(normalized)) continue;
        seen.insert(normalized);

        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);

        if (armatools::armapath::is_procedural_texture(tex)) {
            auto* label = Gtk::make_managed<Gtk::Label>("  " + tex);
            label->set_halign(Gtk::Align::START);
            label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            row->append(*label);
        } else {
            bool found = resolve_texture_on_disk(tex);

            auto* icon = Gtk::make_managed<Gtk::Image>();
            if (found) {
                icon->set_from_icon_name("object-select-symbolic");
            } else {
                icon->set_from_icon_name("dialog-warning-symbolic");
                missing_textures_.push_back(tex);
            }
            row->append(*icon);

            auto* btn = Gtk::make_managed<Gtk::Button>(tex);
            btn->set_halign(Gtk::Align::START);
            btn->set_hexpand(true);
            btn->set_has_frame(false);
            btn->set_tooltip_text("Click to preview texture");
            auto tex_copy = tex;
            btn->signal_clicked().connect(
                [this, tex_copy]() { on_texture_clicked(tex_copy); });
            row->append(*btn);
        }

        texture_list_.append(*row);
    }

    // Show/hide extract button
    if (!missing_textures_.empty()) {
        extract_button_.set_label("Extract " + std::to_string(missing_textures_.size())
                                  + " Missing Textures");
        extract_button_.set_visible(true);
    } else {
        extract_button_.set_visible(false);
    }
    extract_status_.set_visible(false);
}

void TabP3dInfo::on_texture_clicked(const std::string& texture_path) {
    if (armatools::armapath::is_procedural_texture(texture_path))
        return;

    namespace fs = std::filesystem;

    // Helper: show decoded image in the floating preview window
    auto show_preview = [&](const armatools::paa::Image& img) {
        auto pixbuf = Gdk::Pixbuf::create_from_data(
            img.pixels.data(), Gdk::Colorspace::RGB, true, 8,
            img.width, img.height, img.width * 4);
        auto copy = pixbuf->copy();
        auto texture = Gdk::Texture::create_for_pixbuf(copy);

        if (!texture_preview_window_) {
            texture_preview_window_ = std::make_unique<Gtk::Window>();
            texture_preview_window_->set_default_size(512, 512);
            texture_preview_window_->set_child(texture_preview_picture_);
            texture_preview_picture_.set_can_shrink(true);
            texture_preview_picture_.set_content_fit(Gtk::ContentFit::CONTAIN);
            auto* root_window = dynamic_cast<Gtk::Window*>(get_root());
            if (root_window)
                texture_preview_window_->set_transient_for(*root_window);
            texture_preview_window_->signal_close_request().connect([this]() {
                texture_preview_window_->set_visible(false);
                return true;
            }, false);
        }

        texture_preview_picture_.set_paintable(texture);
        texture_preview_window_->set_title(texture_path);
        texture_preview_window_->set_visible(true);
        texture_preview_window_->present();
    };

    // Try loading from disk first (drive_root or relative to model)
    auto on_disk = armatools::armapath::to_os(texture_path);

    auto try_decode_file = [&](const fs::path& path) -> bool {
        std::error_code ec;
        if (!fs::exists(path, ec)) return false;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        try {
            auto [img, hdr] = armatools::paa::decode(f);
            if (img.width <= 0 || img.height <= 0) return false;
            show_preview(img);
            return true;
        } catch (...) { return false; }
    };

    if (cfg_ && !cfg_->drive_root.empty()) {
        auto base_dir = fs::path(model_path_).parent_path();
        if (try_decode_file(base_dir / on_disk)) return;
        if (try_decode_file(base_dir / on_disk.filename())) return;
        if (try_decode_file(fs::path(cfg_->drive_root) / on_disk)) return;
    }

    // Resolve via pboindex
    if (!index_) {
        app_log(LogLevel::Warning, "No index available to resolve texture: " + texture_path);
        return;
    }

    auto normalized = armatools::armapath::to_slash_lower(texture_path);
    armatools::pboindex::ResolveResult rr;
    if (!index_->resolve(normalized, rr)) {
        app_log(LogLevel::Warning, "Could not resolve texture: " + texture_path);
        return;
    }

    auto data = extract_from_pbo(rr.pbo_path, rr.entry_name);
    if (data.empty()) {
        app_log(LogLevel::Warning, "Could not extract texture: " + texture_path);
        return;
    }

    try {
        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        auto [img, hdr] = armatools::paa::decode(stream);
        if (img.width <= 0 || img.height <= 0) return;
        show_preview(img);
    } catch (const std::exception& e) {
        app_log(LogLevel::Error, std::string("Texture decode error: ") + e.what());
    }
}

void TabP3dInfo::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search in PBO...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        search_scroll_.set_visible(true);
    } else {
        path_entry_.set_placeholder_text("P3D file path...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        search_scroll_.set_visible(false);
    }
}

void TabP3dInfo::on_search() {
    auto query = std::string(path_entry_.get_text());
    if (query.empty() || !db_) return;

    while (auto* row = search_results_.get_row_at_index(0))
        search_results_.remove(*row);
    search_results_data_.clear();

    auto results = db_->find_files("*" + query + "*.p3d");
    search_results_data_ = results;

    for (const auto& r : results) {
        auto display = r.prefix + "/" + r.file_path;
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        search_results_.append(*label);
    }
}

void TabP3dInfo::on_search_result_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto idx = static_cast<size_t>(row->get_index());
    if (idx >= search_results_data_.size()) return;
    load_from_pbo(search_results_data_[idx]);
}

void TabP3dInfo::load_from_pbo(const armatools::pboindex::FindResult& r) {
    auto data = extract_from_pbo(r.pbo_path, r.file_path);
    if (data.empty()) {
        model_info_label_.set_text("Error: Could not extract from PBO");
        return;
    }

    // Clear
    while (auto* row = lod_list_.get_row_at_index(0))
        lod_list_.remove(*row);
    detail_view_.get_buffer()->set_text("");
    model_.reset();
    model_path_.clear();
    model_panel_.clear();
    missing_textures_.clear();
    texture_header_.set_visible(false);
    texture_scroll_.set_visible(false);
    extract_button_.set_visible(false);
    extract_status_.set_visible(false);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }

    try {
        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        model_ = std::make_shared<ModelData>();
        model_->p3d = armatools::p3d::read(stream);
        model_path_ = r.prefix + "/" + r.file_path;

        const auto& p = model_->p3d;

        std::ostringstream info;
        info << "Format: " << p.format << " v" << p.version << "\n";
        info << "LODs: " << p.lods.size() << "\n";

        if (p.model_info) {
            info << "Mass: " << p.model_info->mass << "\n";
            info << "Armor: " << p.model_info->armor << "\n";
            info << "Bounding sphere: " << p.model_info->bounding_sphere << "\n";
        }

        auto size_result = armatools::p3d::calculate_size(p);
        if (size_result.info) {
            auto& s = *size_result.info;
            info << "Dimensions: " << s.dimensions[0] << " x "
                 << s.dimensions[1] << " x " << s.dimensions[2] << " m\n";
        }

        model_info_label_.set_text(info.str());

        for (const auto& lod : p.lods) {
            auto text = lod.resolution_name + "  (V:" + std::to_string(lod.vertex_count)
                      + " F:" + std::to_string(lod.face_count) + ")";
            auto* label = Gtk::make_managed<Gtk::Label>(text);
            label->set_halign(Gtk::Align::START);
            lod_list_.append(*label);
        }

        if (!p.lods.empty()) {
            lod_list_.select_row(*lod_list_.get_row_at_index(0));
        }

        app_log(LogLevel::Info, "Loaded P3D from PBO: " + model_path_);

    } catch (const std::exception& e) {
        model_info_label_.set_text(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, "P3D PBO load error: " + std::string(e.what()));
    }
}

void TabP3dInfo::extract_missing_textures() {
    if (missing_textures_.empty()) return;
    if (!db_) {
        app_log(LogLevel::Error, "No A3DB configured -- cannot extract textures");
        return;
    }
    if (!cfg_ || cfg_->drive_root.empty()) {
        app_log(LogLevel::Error, "No Drive Root configured -- cannot extract textures");
        return;
    }

    // Join any previous extraction thread
    if (extract_thread_.joinable())
        extract_thread_.join();

    // Show spinner
    extract_button_.set_sensitive(false);
    extract_spinner_.set_visible(true);
    extract_spinner_.set_spinning(true);
    extract_status_.set_text("Extracting...");
    extract_status_.set_visible(true);

    auto textures = missing_textures_;
    auto drive_root = cfg_->drive_root;

    // Capture db_ as raw pointer (it lives on this object, and we join in destructor)
    auto* db = db_.get();

    app_log(LogLevel::Info, "Extracting " + std::to_string(textures.size())
            + " missing textures...");

    extract_thread_ = std::thread([this, db, textures, drive_root]() {
        namespace fs = std::filesystem;
        int extracted = 0, not_found = 0, failed = 0;

        for (const auto& tex : textures) {
            auto normalized = armatools::armapath::to_os(tex);
            auto basename = normalized.filename().string();

            // Search for this texture in the database
            std::string pattern = "*" + basename;
            std::vector<armatools::pboindex::FindResult> results;
            try {
                results = db->find_files(pattern);
            } catch (...) {
                not_found++;
                continue;
            }

            if (results.empty()) {
                Glib::signal_idle().connect_once([tex]() {
                    app_log(LogLevel::Warning, "  not found in DB: " + tex);
                });
                not_found++;
                continue;
            }

            // Extract from the first matching PBO
            auto& r = results[0];
            auto data = extract_from_pbo(r.pbo_path, r.file_path);
            if (data.empty()) {
                Glib::signal_idle().connect_once([tex]() {
                    app_log(LogLevel::Warning, "  extract failed: " + tex);
                });
                failed++;
                continue;
            }

            // Write to drive_root preserving path structure
            auto dest = fs::path(drive_root) / normalized;
            std::error_code ec;
            fs::create_directories(dest.parent_path(), ec);
            if (ec) {
                failed++;
                continue;
            }

            std::ofstream out(dest, std::ios::binary);
            if (!out.is_open()) {
                failed++;
                continue;
            }
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
            out.close();

            auto dest_str = dest.string();
            Glib::signal_idle().connect_once([dest_str]() {
                app_log(LogLevel::Info, "  extracted: " + dest_str);
            });
            extracted++;
        }

        // Report results on main thread and refresh
        Glib::signal_idle().connect_once([this, extracted, not_found, failed]() {
            app_log(LogLevel::Info, "Extract done: " + std::to_string(extracted)
                    + " extracted, " + std::to_string(not_found) + " not found, "
                    + std::to_string(failed) + " failed");

            extract_spinner_.set_spinning(false);
            extract_spinner_.set_visible(false);
            extract_button_.set_sensitive(true);

            if (extracted > 0) {
                extract_status_.set_text(std::to_string(extracted) + " extracted");

                // Reload textures and refresh the texture list for the current LOD
                if (model_) {
                    auto* row = lod_list_.get_selected_row();
                    if (row) {
                        int idx = row->get_index();
                        if (idx >= 0 && static_cast<size_t>(idx) < model_->p3d.lods.size()) {
                            const auto& lod = model_->p3d.lods[static_cast<size_t>(idx)];
                            model_panel_.show_lod(lod, model_path_);
                            update_texture_list(lod);
                            model_panel_.gl_view().queue_render();
                        }
                    }
                }
            } else {
                extract_status_.set_text("No textures extracted");
            }
        });
    });
}
