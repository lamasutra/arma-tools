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
#include <unordered_map>
#include <unordered_set>

struct TabP3dInfo::ModelData {
    std::shared_ptr<armatools::p3d::P3DFile> p3d;
};

TabP3dInfo::TabP3dInfo() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    auto make_icon_button = [](Gtk::Button& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
    };
    make_icon_button(browse_button_, "document-open-symbolic", "Browse P3D file");
    make_icon_button(search_button_, "system-search-symbolic", "Search indexed PBOs for P3D");

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

    source_combo_.set_tooltip_text("Filter by A3DB source");
    source_combo_.append("", "All");
    source_combo_.set_active_id("");
    source_box_.append(source_label_);
    source_box_.append(source_combo_);
    source_box_.set_visible(false);
    left_box_.append(source_box_);

    // Search results (PBO mode only)
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    left_box_.append(search_scroll_);

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
    model_panel_.set_on_lod_changed([this](const armatools::p3d::LOD& lod, int idx) {
        on_model_lod_changed(lod, idx);
    });

    // Signals
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dInfo::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_file(path_entry_.get_text());
    });
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabP3dInfo::on_pbo_mode_changed));
    source_combo_.signal_changed().connect(sigc::mem_fun(*this, &TabP3dInfo::on_source_changed));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dInfo::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabP3dInfo::on_search_result_selected));

}

TabP3dInfo::~TabP3dInfo() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }
}

void TabP3dInfo::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabP3dInfo::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_loader_service_ = service;
    model_panel_.set_model_loader_service(service);
}

void TabP3dInfo::set_texture_loader_service(
    const std::shared_ptr<TexturesLoaderService>& service) {
    model_panel_.set_texture_loader_service(service);
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
        refresh_source_combo();
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
    detail_view_.get_buffer()->set_text("");
    model_.reset();
    model_path_.clear();
    model_panel_.clear();
    model_panel_.set_info_line("");

    // Hide texture UI
    texture_header_.set_visible(false);
    texture_scroll_.set_visible(false);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }

    if (model_loader_service_) {
        model_ = std::make_shared<ModelData>();
        model_path_ = path;
        model_panel_.load_p3d(path);
        return;
    }

    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            model_panel_.set_info_line("Error: Cannot open file");
            return;
        }

        model_ = std::make_shared<ModelData>();
        model_->p3d = std::make_shared<armatools::p3d::P3DFile>(armatools::p3d::read(f));
        model_path_ = path;
        const auto& p = *model_->p3d;

        app_log(LogLevel::Info, "Loaded P3D: " + path);

        // One-line model summary in ModelViewPanel toolbar
        std::ostringstream info;
        info << "Format: " << p.format << " v" << p.version
             << " | LODs: " << p.lods.size();
        auto size_result = armatools::p3d::calculate_size(p);
        if (size_result.info) {
            auto& s = *size_result.info;
            info << " | Size: " << s.dimensions[0] << "x"
                 << s.dimensions[1] << "x" << s.dimensions[2] << "m";
        }
        model_panel_.set_info_line(info.str());

        model_panel_.set_model_data(model_->p3d, model_path_);

    } catch (const std::exception& e) {
        model_panel_.set_info_line(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, "P3D load error: " + std::string(e.what()));
    }
}

void TabP3dInfo::open_model_path(const std::string& model_path) {
    if (model_path.empty()) return;
    path_entry_.set_text(model_path);

    detail_view_.get_buffer()->set_text("");
    model_.reset();
    model_path_.clear();
    model_panel_.clear();
    model_panel_.set_info_line("");
    texture_header_.set_visible(false);
    texture_scroll_.set_visible(false);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }

    if (model_loader_service_) {
        model_ = std::make_shared<ModelData>();
        model_path_ = model_path;
        model_panel_.load_p3d(model_path);
        return;
    }

    try {
        model_ = std::make_shared<ModelData>();
        std::ifstream f(model_path, std::ios::binary);
        if (!f.is_open()) {
            model_panel_.set_info_line("Error: Cannot open file");
            return;
        }
        model_->p3d = std::make_shared<armatools::p3d::P3DFile>(armatools::p3d::read(f));
        model_path_ = model_path;

        const auto& p = *model_->p3d;
        std::ostringstream info;
        info << "Format: " << p.format << " v" << p.version
             << " | LODs: " << p.lods.size();
        auto size_result = armatools::p3d::calculate_size(p);
        if (size_result.info) {
            auto& s = *size_result.info;
            info << " | Size: " << s.dimensions[0] << "x"
                 << s.dimensions[1] << "x" << s.dimensions[2] << "m";
        }
        model_panel_.set_info_line(info.str());
        model_panel_.set_model_data(model_->p3d, model_path_);
    } catch (const std::exception& e) {
        model_panel_.set_info_line(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, "P3D load error: " + std::string(e.what()));
    }
}

void TabP3dInfo::on_model_lod_changed(const armatools::p3d::LOD& lod, int idx) {
    if (!model_) return;
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
    detail << "Index: " << idx << "\n";

    detail_view_.get_buffer()->set_text(detail.str());
}

void TabP3dInfo::update_texture_list(const armatools::p3d::LOD& lod) {
    // Clear existing texture rows
    while (auto* child = texture_list_.get_first_child())
        texture_list_.remove(*child);

    if (lod.textures.empty()) {
        texture_header_.set_visible(false);
        texture_scroll_.set_visible(false);
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
            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name("image-x-generic-symbolic");
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
}

void TabP3dInfo::on_texture_clicked(const std::string& texture_path) {
    if (armatools::armapath::is_procedural_texture(texture_path))
        return;

    namespace fs = std::filesystem;
    const auto normalized = armatools::armapath::to_slash_lower(texture_path);

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

    auto try_decode_data = [&](const std::vector<uint8_t>& data) -> bool {
        if (data.empty()) return false;
        try {
            std::string str(data.begin(), data.end());
            std::istringstream stream(str);
            auto [img, hdr] = armatools::paa::decode(stream);
            if (img.width <= 0 || img.height <= 0) return false;
            show_preview(img);
            return true;
        } catch (...) { return false; }
    };

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

    // 1) Resolve via index first
    if (index_) {
        armatools::pboindex::ResolveResult rr;
        if (index_->resolve(normalized, rr)) {
            if (try_decode_data(extract_from_pbo(rr.pbo_path, rr.entry_name))) return;
        }
    }

    // 2) Fallback via DB file search
    if (db_) {
        auto filename = fs::path(normalized).filename().string();
        auto results = db_->find_files("*" + filename);
        for (const auto& r : results) {
            auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
            if (full == normalized || full.ends_with("/" + normalized)) {
                if (try_decode_data(extract_from_pbo(r.pbo_path, r.file_path))) return;
            }
        }
    }

    // 3) Last fallback: disk
    if (cfg_ && !cfg_->drive_root.empty()) {
        auto on_disk = armatools::armapath::to_os(texture_path);
        auto base_dir = fs::path(model_path_).parent_path();
        if (try_decode_file(base_dir / on_disk)) return;
        if (try_decode_file(base_dir / on_disk.filename())) return;
        if (try_decode_file(fs::path(cfg_->drive_root) / on_disk)) return;
    }

    app_log(LogLevel::Warning, "Could not load texture preview: " + texture_path);
}

void TabP3dInfo::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search in PBO...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        source_box_.set_visible(true);
        search_scroll_.set_visible(true);
    } else {
        path_entry_.set_placeholder_text("P3D file path...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        source_box_.set_visible(false);
        search_scroll_.set_visible(false);
    }
}

void TabP3dInfo::refresh_source_combo() {
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

void TabP3dInfo::on_source_changed() {
    if (source_combo_updating_) return;
    current_source_ = std::string(source_combo_.get_active_id());
    if (pbo_mode_) on_search();
}

void TabP3dInfo::on_search() {
    auto query = std::string(path_entry_.get_text());
    if (query.empty() || !db_) return;

    while (auto* row = search_results_.get_row_at_index(0))
        search_results_.remove(*row);
    search_results_data_.clear();

    std::vector<armatools::pboindex::FindResult> results;
    if (current_source_.empty())
        results = db_->find_files("*" + query + "*.p3d");
    else
        results = db_->find_files("*" + query + "*.p3d", current_source_);
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
    // Clear
    detail_view_.get_buffer()->set_text("");
    model_.reset();
    model_path_.clear();
    model_panel_.clear();
    model_panel_.set_info_line("");
    texture_header_.set_visible(false);
    texture_scroll_.set_visible(false);
    if (texture_preview_window_) {
        texture_preview_window_->close();
        texture_preview_window_.reset();
    }

    const auto model_path = r.prefix + "/" + r.file_path;
    if (model_loader_service_) {
        model_ = std::make_shared<ModelData>();
        model_path_ = model_path;
        model_panel_.load_p3d(model_path_);
        return;
    }

    auto data = extract_from_pbo(r.pbo_path, r.file_path);
    if (data.empty()) {
        model_panel_.set_info_line("Error: Could not extract from PBO");
        return;
    }

    try {
        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        model_ = std::make_shared<ModelData>();
        model_->p3d = std::make_shared<armatools::p3d::P3DFile>(armatools::p3d::read(stream));
        model_path_ = model_path;

        const auto& p = *model_->p3d;

        std::ostringstream info;
        info << "Format: " << p.format << " v" << p.version
             << " | LODs: " << p.lods.size();
        auto size_result = armatools::p3d::calculate_size(p);
        if (size_result.info) {
            auto& s = *size_result.info;
            info << " | Size: " << s.dimensions[0] << "x"
                 << s.dimensions[1] << "x" << s.dimensions[2] << "m";
        }
        model_panel_.set_info_line(info.str());

        model_panel_.set_model_data(model_->p3d, model_path_);

        app_log(LogLevel::Info, "Loaded P3D from PBO: " + model_path_);

    } catch (const std::exception& e) {
        model_panel_.set_info_line(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, "P3D PBO load error: " + std::string(e.what()));
    }
}

