#include "tab_wrp_project.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/armapath.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
namespace hp = armatools::heightpipe;

namespace {

hp::CorrectionPreset parse_heightpipe_preset(const Glib::ustring& text) {
    const std::string s = text;
    if (s == "none") return hp::CorrectionPreset::None;
    if (s == "sharp") return hp::CorrectionPreset::Sharp;
    if (s == "retain_detail") return hp::CorrectionPreset::RetainDetail;
    return hp::CorrectionPreset::Terrain16x;
}

uint32_t parse_seed_or_default(const Glib::ustring& text, uint32_t fallback) {
    try {
        if (!text.empty()) return static_cast<uint32_t>(std::stoul(std::string(text)));
    } catch (...) {}
    return fallback;
}

double parse_double_or_default(const Glib::ustring& text, double fallback) {
    try {
        if (!text.empty()) return std::stod(std::string(text));
    } catch (...) {}
    return fallback;
}

std::string sanitize_stem_for_filename(std::string stem) {
    if (stem.empty()) return "wrp";
    for (char& c : stem) {
        const bool ok = (c >= 'a' && c <= 'z')
                     || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9')
                     || c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return stem;
}

} // namespace

TabWrpProject::TabWrpProject() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    auto make_icon_button = [](Gtk::Button& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
    };
    make_icon_button(scan_button_, "system-search-symbolic", "Scan/search WRP files");
    make_icon_button(folder_button_, "document-open-symbolic", "Browse folder with WRP files");
    make_icon_button(output_browse_, "document-open-symbolic", "Browse output directory");
    make_icon_button(replace_browse_, "document-open-symbolic", "Browse replacement TSV file");

    left_box_.set_margin(8);

    // WRP file browser
    source_combo_.set_tooltip_text("Filter WRP files by A3DB source");
    source_combo_.append("", "All");
    source_combo_.set_active_id("");
    filter_entry_.set_hexpand(true);
    filter_entry_.set_placeholder_text("Filter WRP files...");
    filter_box_.append(source_label_);
    filter_box_.append(source_combo_);
    filter_box_.append(filter_entry_);
    filter_box_.append(scan_button_);
    filter_box_.append(folder_button_);
    left_box_.append(filter_box_);

    list_scroll_.set_vexpand(true);
    list_scroll_.set_child(file_list_);
    left_box_.append(list_scroll_);

    // Output row
    output_label_.set_size_request(80, -1);
    output_entry_.set_hexpand(true);
    output_entry_.set_placeholder_text("Output directory...");
    output_box_.set_margin_top(8);
    output_box_.append(output_label_);
    output_box_.append(output_entry_);
    output_box_.append(output_browse_);
    left_box_.append(output_box_);

    // Options grid
    options_grid_.set_row_spacing(4);
    options_grid_.set_column_spacing(8);
    options_grid_.set_margin_top(4);

    int row = 0;
    auto add_row = [&](const std::string& label, Gtk::Widget& widget) {
        auto* lbl = Gtk::make_managed<Gtk::Label>(label);
        lbl->set_halign(Gtk::Align::START);
        options_grid_.attach(*lbl, 0, row);
        widget.set_hexpand(true);
        options_grid_.attach(widget, 1, row);
        row++;
    };

    add_row("Offset X:", offset_x_entry_);
    add_row("Offset Z:", offset_z_entry_);

    hm_scale_combo_.append("1");
    hm_scale_combo_.append("2");
    hm_scale_combo_.append("4");
    hm_scale_combo_.append("8");
    hm_scale_combo_.append("16");
    hm_scale_combo_.set_active(0);
    add_row("HM Scale:", hm_scale_combo_);

    heightpipe_preset_combo_.append("none");
    heightpipe_preset_combo_.append("sharp");
    heightpipe_preset_combo_.append("retain_detail");
    heightpipe_preset_combo_.append("terrain_16x");
    heightpipe_preset_combo_.set_active(3);
    add_row("HP Preset:", heightpipe_preset_combo_);

    heightpipe_seed_entry_.set_text("1");
    add_row("HP Seed:", heightpipe_seed_entry_);

    options_grid_.attach(use_heightpipe_check_, 0, row, 2);
    row++;

    add_row("Split:", split_entry_);
    add_row("Style:", style_entry_);

    // Replace row: entry + browse button in a box
    {
        auto* replace_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        replace_entry_.set_hexpand(true);
        replace_entry_.set_placeholder_text("Replacement TSV file (from ObjReplace tab)...");
        replace_box->append(replace_entry_);
        replace_box->append(replace_browse_);
        replace_box->set_hexpand(true);
        add_row("Replace:", *replace_box);
    }

    options_grid_.attach(extract_p3d_check_, 0, row, 2);
    row++;
#if defined(WRP2PROJECT_WITH_TV4L)
    options_grid_.attach(empty_layers_check_, 0, row, 2);
#endif

    left_box_.append(options_grid_);

    // Action row
    action_box_.set_margin_top(4);
    action_box_.append(generate_button_);
    action_box_.append(save_defaults_button_);
    action_box_.append(status_label_);
    status_label_.set_hexpand(true);
    status_label_.set_halign(Gtk::Align::START);
    left_box_.append(action_box_);

    // Right panel: heightmap preview + log
    right_box_.set_margin(8);
    hm_info_label_.set_halign(Gtk::Align::START);
    hm_info_label_.set_text("Select a WRP file to preview heightmap");
    right_box_.append(hm_info_label_);

    hm_picture_.set_content_fit(Gtk::ContentFit::CONTAIN);
    hm_scroll_.set_child(hm_picture_);
    hm_scroll_.set_vexpand(true);
    hm_scroll_.set_hexpand(true);
    right_box_.append(hm_scroll_);

    // Log
    log_view_.set_editable(false);
    log_view_.set_monospace(true);
    log_scroll_.set_size_request(-1, 150);
    log_scroll_.set_child(log_view_);
    right_box_.append(log_scroll_);

    // Paned layout
    set_start_child(left_box_);
    set_end_child(right_box_);
    set_resize_start_child(true);
    set_resize_end_child(true);
    set_position(400);

    // Signals
    scan_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_scan));
    folder_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_folder_browse));
    source_combo_.signal_changed().connect(sigc::mem_fun(*this, &TabWrpProject::on_source_changed));
    filter_entry_.signal_changed().connect(sigc::mem_fun(*this, &TabWrpProject::on_filter_changed));
    file_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabWrpProject::on_file_selected));
    output_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_output_browse));
    replace_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_replace_browse));
    generate_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_generate));
    save_defaults_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabWrpProject::on_save_defaults));
}

TabWrpProject::~TabWrpProject() {
    ++scan_generation_;
    if (scan_thread_.joinable())
        scan_thread_.join();
    if (worker_.joinable())
        worker_.join();
    if (hm_worker_.joinable())
        hm_worker_.join();
    if (!selected_wrp_temp_path_.empty()) {
        std::error_code ec;
        fs::remove(selected_wrp_temp_path_, ec);
    }
}

void TabWrpProject::set_config(Config* cfg) {
    cfg_ = cfg;
    populate_defaults();
    refresh_source_combo();

    if (cfg_ && !cfg_->worlds_dir.empty()) {
        scan_dir_ = cfg_->worlds_dir;
        on_scan();
    }
}

void TabWrpProject::refresh_source_combo() {
    source_combo_updating_ = true;
    source_combo_.remove_all();
    source_combo_.append("", "All");

    if (cfg_ && !cfg_->a3db_path.empty()) {
        try {
            auto db = armatools::pboindex::DB::open(cfg_->a3db_path);
            static const std::unordered_map<std::string, std::string> source_labels = {
                {"arma3", "Arma 3"},
                {"workshop", "Workshop"},
                {"ofp", "OFP/CWA"},
                {"arma1", "Arma 1"},
                {"arma2", "Arma 2"},
                {"custom", "Custom"},
            };
            for (const auto& src : db.query_sources()) {
                auto it = source_labels.find(src);
                source_combo_.append(src, it != source_labels.end() ? it->second : src);
            }
        } catch (...) {
        }
    }

    source_combo_.set_active_id(current_source_);
    source_combo_updating_ = false;
}

void TabWrpProject::on_source_changed() {
    if (source_combo_updating_) return;
    current_source_ = std::string(source_combo_.get_active_id());
    on_scan();
}

void TabWrpProject::populate_defaults() {
    if (!cfg_) return;
    const auto& d = cfg_->wrp2project_defaults;
    offset_x_entry_.set_text(d.offset_x);
    offset_z_entry_.set_text(d.offset_z);
    split_entry_.set_text(d.split);
    style_entry_.set_text(d.style);
    replace_entry_.set_text(d.replace_file);
    extract_p3d_check_.set_active(d.extract_p3d);
    use_heightpipe_check_.set_active(d.use_heightpipe);
    heightpipe_seed_entry_.set_text(d.heightpipe_seed.empty() ? "1" : d.heightpipe_seed);
    if (d.heightpipe_preset == "none") heightpipe_preset_combo_.set_active(0);
    else if (d.heightpipe_preset == "sharp") heightpipe_preset_combo_.set_active(1);
    else if (d.heightpipe_preset == "retain_detail") heightpipe_preset_combo_.set_active(2);
    else heightpipe_preset_combo_.set_active(3);
#if defined(WRP2PROJECT_WITH_TV4L)
    empty_layers_check_.set_active(d.empty_layers);
#endif

    if (output_entry_.get_text().empty() && !cfg_->drive_root.empty()) {
        output_entry_.set_text(cfg_->drive_root);
    }

    if (d.hm_scale == "2") hm_scale_combo_.set_active(1);
    else if (d.hm_scale == "4") hm_scale_combo_.set_active(2);
    else if (d.hm_scale == "8") hm_scale_combo_.set_active(3);
    else if (d.hm_scale == "16") hm_scale_combo_.set_active(4);
    else hm_scale_combo_.set_active(0);
}

// ---------------------------------------------------------------------------
// WRP file browser
// ---------------------------------------------------------------------------

void TabWrpProject::on_folder_browse() {
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
                app_log(LogLevel::Warning, "WrpProject: folder dialog failed: " + std::string(e.what()));
            } catch (...) {
                app_log(LogLevel::Warning, "WrpProject: folder dialog failed");
            }
        });
}

void TabWrpProject::on_scan() {
    if ((!cfg_ || cfg_->a3db_path.empty()) && scan_dir_.empty()) return;
    const auto dir = scan_dir_;
    const auto source = current_source_;
    const auto db_path = cfg_ ? cfg_->a3db_path : std::string();
    const unsigned gen = ++scan_generation_;
    status_label_.set_text("Scanning WRP files...");
    if (scan_thread_.joinable()) {
        scan_thread_.request_stop();
        scan_thread_.join();
    }
    scan_thread_ = std::jthread([this, dir, source, db_path, gen](std::stop_token st) {
        std::vector<WrpFileEntry> files;
        std::string err;
        try {
            if (!db_path.empty()) {
                auto db = armatools::pboindex::DB::open(db_path);
                auto results = db.find_files("*.wrp", source);
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
            }
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "unknown error";
        }
        if (st.stop_requested()) return;
        if (files.empty() && !dir.empty()) {
            std::error_code ec;
            for (auto& entry : fs::recursive_directory_iterator(
                     dir, fs::directory_options::skip_permission_denied, ec)) {
                if (st.stop_requested()) return;
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
        if (st.stop_requested()) return;
        std::sort(files.begin(), files.end(),
                  [](const auto& a, const auto& b) { return a.full_path < b.full_path; });
        Glib::signal_idle().connect_once([this, files = std::move(files), gen, err = std::move(err)]() mutable {
            if (gen != scan_generation_.load()) return;
            if (!err.empty()) {
                status_label_.set_text("Scan failed: " + err);
                app_log(LogLevel::Warning, "WrpProject scan failed: " + err);
                return;
            }
            wrp_files_ = std::move(files);
            on_filter_changed();
            status_label_.set_text("Ready");
        });
    });
}

void TabWrpProject::scan_wrp_files(const std::string& dir) {
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec)) {
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

void TabWrpProject::on_filter_changed() {
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

void TabWrpProject::update_file_list() {
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

void TabWrpProject::on_file_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    int idx = row->get_index();
    if (idx < 0 || idx >= static_cast<int>(filtered_files_.size())) return;

    selected_wrp_entry_ = filtered_files_[static_cast<size_t>(idx)];
    selected_wrp_entry_valid_ = true;
    selected_wrp_path_ = selected_wrp_entry_.full_path;

    // Auto-suggest output directory based on selected file
    if (output_entry_.get_text().empty() || !cfg_ || cfg_->drive_root.empty()) {
        fs::path p{selected_wrp_entry_.full_path};
        output_entry_.set_text((p.parent_path() / p.stem()).string());
    }

    // Load heightmap preview
    std::string wrp_input_path, err;
    if (!materialize_wrp_entry(selected_wrp_entry_, wrp_input_path, err)) {
        hm_info_label_.set_text("Error: " + err);
        return;
    }
    if (wrp_input_path != hm_loaded_path_) {
        load_heightmap(wrp_input_path);
    }
}

// ---------------------------------------------------------------------------
// Heightmap preview
// ---------------------------------------------------------------------------

void TabWrpProject::load_heightmap(const std::string& path) {
    if (hm_loading_) return;
    hm_loading_ = true;
    hm_info_label_.set_text("Loading heightmap...");

    if (hm_worker_.joinable()) {
        hm_worker_.request_stop();
        hm_worker_.join();
    }

    hm_worker_ = std::jthread([this, path](std::stop_token st) {
        std::string info_text;
        std::vector<float> elevations;
        int grid_x = 0, grid_y = 0;

        try {
            if (st.stop_requested()) return;
            std::ifstream f(path, std::ios::binary);
            if (f) {
                armatools::wrp::Options opts;
                opts.no_objects = true;
                auto wd = armatools::wrp::read(f, opts);

                grid_x = wd.grid.terrain_x;
                grid_y = wd.grid.terrain_y;
                elevations = std::move(wd.elevations);

                std::ostringstream ss;
                ss << fs::path(path).filename().string()
                   << "  |  " << grid_x << "x" << grid_y
                   << "  |  " << wd.bounds.world_size_x << "x" << wd.bounds.world_size_y << "m"
                   << "  |  Elev: " << wd.bounds.min_elevation << " - " << wd.bounds.max_elevation << "m";
                info_text = ss.str();
            } else {
                info_text = "Error: cannot open file";
            }
        } catch (const std::exception& e) {
            info_text = std::string("Error: ") + e.what();
        }

        if (st.stop_requested()) return;
        Glib::signal_idle().connect_once([this, info_text = std::move(info_text),
                                          elevations = std::move(elevations),
                                          grid_x, grid_y, path]() {
            hm_info_label_.set_text(info_text);

            if (!elevations.empty() && grid_x > 0 && grid_y > 0) {
                auto texture = render_heightmap(elevations, grid_x, grid_y);
                if (texture) hm_picture_.set_paintable(texture);
            }

            hm_loaded_path_ = path;
            hm_loading_ = false;
        });
    });
}

Glib::RefPtr<Gdk::Texture> TabWrpProject::render_heightmap(
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

bool TabWrpProject::materialize_wrp_entry(const WrpFileEntry& entry,
                                          std::string& out_path,
                                          std::string& err) {
    if (!entry.from_pbo) {
        out_path = entry.full_path;
        return true;
    }

    auto data = extract_from_pbo(entry.pbo_path, entry.entry_name);
    if (data.empty()) {
        err = "cannot extract from PBO";
        return false;
    }

    if (!selected_wrp_temp_path_.empty()) {
        std::error_code ec;
        fs::remove(selected_wrp_temp_path_, ec);
        selected_wrp_temp_path_.clear();
    }

    const auto key = entry.pbo_path + "|" + entry.entry_name;
    const auto hash = std::to_string(std::hash<std::string>{}(key));
    const auto pbo_bucket = std::to_string(std::hash<std::string>{}(entry.pbo_path));
    auto tmp_dir = fs::temp_directory_path() / "arma-tools-wrp" / pbo_bucket;
    std::error_code mk_ec;
    fs::create_directories(tmp_dir, mk_ec);
    if (mk_ec) {
        err = "cannot create temporary WRP directory";
        return false;
    }

    std::string tmp_file_name = fs::path(entry.entry_name).filename().string();
    if (tmp_file_name.empty()) tmp_file_name = "world.wrp";
    auto tmp = tmp_dir / tmp_file_name;
    if (fs::exists(tmp)) {
        const auto stem = fs::path(tmp_file_name).stem().string();
        const auto ext = fs::path(tmp_file_name).extension().string();
        tmp = tmp_dir / (sanitize_stem_for_filename(stem) + "-" + hash + ext);
    }
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        err = "cannot create temporary WRP file";
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out.good()) {
        err = "cannot write temporary WRP file";
        return false;
    }

    selected_wrp_temp_path_ = tmp.string();
    out_path = selected_wrp_temp_path_;
    return true;
}

// ---------------------------------------------------------------------------
// Options / generate
// ---------------------------------------------------------------------------

void TabWrpProject::on_save_defaults() {
    if (!cfg_) return;
    auto& d = cfg_->wrp2project_defaults;
    d.offset_x = offset_x_entry_.get_text();
    d.offset_z = offset_z_entry_.get_text();
    d.split = split_entry_.get_text();
    d.hm_scale = hm_scale_combo_.get_active_text();
    d.style = style_entry_.get_text();
    d.replace_file = replace_entry_.get_text();
    d.extract_p3d = extract_p3d_check_.get_active();
    d.use_heightpipe = use_heightpipe_check_.get_active();
    d.heightpipe_preset = heightpipe_preset_combo_.get_active_text();
    d.heightpipe_seed = heightpipe_seed_entry_.get_text();
#if defined(WRP2PROJECT_WITH_TV4L)
    d.empty_layers = empty_layers_check_.get_active();
#endif
    save_config(*cfg_);
    status_label_.set_text("Defaults saved.");
}

void TabWrpProject::on_output_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->select_folder_finish(result);
                if (file) output_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabWrpProject::on_replace_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("TSV files");
    filter->add_pattern("*.tsv");
    filter->add_pattern("*.txt");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    auto all = Gtk::FileFilter::create();
    all->set_name("All files");
    all->add_pattern("*");
    filters->append(all);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->open(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->open_finish(result);
                if (file) replace_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabWrpProject::on_generate() {
    if (!cfg_) return;

    if (!selected_wrp_entry_valid_) {
        status_label_.set_text("Please select a WRP file from the list.");
        return;
    }
    std::string wrp_input_path, wrp_err;
    if (!materialize_wrp_entry(selected_wrp_entry_, wrp_input_path, wrp_err)) {
        status_label_.set_text("WRP input error: " + wrp_err);
        return;
    }
    auto output = std::string(output_entry_.get_text());
    if (output.empty()) {
        status_label_.set_text("Please specify an output directory.");
        return;
    }

    auto tool = resolve_tool_path(*cfg_, "wrp2project");
    if (tool.empty()) {
        status_label_.set_text("Error: wrp2project binary not found.");
        return;
    }

    // Build argument list
    std::vector<std::string> args;
    args.push_back(wrp_input_path);
    args.push_back(output);
    std::string terrain_name;
    {
        std::string src = selected_wrp_entry_.from_pbo
            ? selected_wrp_entry_.entry_name
            : selected_wrp_entry_.full_path;
        terrain_name = fs::path(src).stem().string();
        for (char& c : terrain_name) {
            if (c == ' ' || c == '-' || c == '.') c = '_';
        }
        if (!terrain_name.empty()) {
            terrain_name[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(terrain_name[0])));
        }
    }
    if (!terrain_name.empty()) {
        args.push_back("--name");
        args.push_back(terrain_name);
    }

    auto ox = std::string(offset_x_entry_.get_text());
    auto oz = std::string(offset_z_entry_.get_text());
    if (!ox.empty()) { args.push_back("-offset-x"); args.push_back(ox); }
    if (!oz.empty()) { args.push_back("-offset-z"); args.push_back(oz); }

    auto scale = std::string(hm_scale_combo_.get_active_text());
    if (scale != "1") { args.push_back("--hm-scale"); args.push_back(scale); }

    auto split = std::string(split_entry_.get_text());
    if (!split.empty()) { args.push_back("--split"); args.push_back(split); }

    if (!cfg_->drive_root.empty()) { args.push_back("--drive"); args.push_back(cfg_->drive_root); }
    if (!cfg_->a3db_path.empty()) { args.push_back("--db"); args.push_back(cfg_->a3db_path); }

    auto style = std::string(style_entry_.get_text());
    if (!style.empty()) { args.push_back("--style"); args.push_back(style); }

    auto replace = std::string(replace_entry_.get_text());
    if (!replace.empty()) { args.push_back("--replace"); args.push_back(replace); }

    if (extract_p3d_check_.get_active()) { args.push_back("--extract-models"); }
#if defined(WRP2PROJECT_WITH_TV4L)
    if (empty_layers_check_.get_active()) { args.push_back("--empty-layers"); }
#endif

    args = apply_tool_verbosity(cfg_, args, true);

    const int hm_scale = std::stoi(scale);
    const bool use_heightpipe = use_heightpipe_check_.get_active();
    const hp::CorrectionPreset hp_preset = parse_heightpipe_preset(heightpipe_preset_combo_.get_active_text());
    const uint32_t hp_seed = parse_seed_or_default(heightpipe_seed_entry_.get_text(), 1u);
    const double offset_x = parse_double_or_default(offset_x_entry_.get_text(), 200000.0);
    const double offset_z = parse_double_or_default(offset_z_entry_.get_text(), 0.0);
    const std::string selected_wrp_path = wrp_input_path;

    // Build display string for log
    std::string display_cmd = tool;
    for (const auto& a : args) display_cmd += " " + a;
    if (use_heightpipe && hm_scale > 1) {
        display_cmd += "\n(post) heightpipe correction enabled";
    }

    status_label_.set_text("Generating...");
    generate_button_.set_sensitive(false);
    log_view_.get_buffer()->set_text("Running: " + display_cmd + "\n\n");

    // Join previous worker if still running
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }

    auto stream_consumer = [this](std::string chunk) {
        Glib::signal_idle().connect_once([this, chunk = std::move(chunk)]() {
            auto tbuf = log_view_.get_buffer();
            tbuf->insert(tbuf->end(), chunk);
        });
    };

    worker_ = std::jthread([this, tool, args, hm_scale, use_heightpipe, hp_preset, hp_seed,
                           offset_x, offset_z, selected_wrp_path, output, stream_consumer](std::stop_token st) mutable {
        if (st.stop_requested()) return;
        auto result = run_subprocess(tool, args, stream_consumer);
        std::string post_log;

        if (result.status == 0 && use_heightpipe && hm_scale > 1) {
            const bool ok = apply_heightpipe_to_project(
                selected_wrp_path, output, hm_scale, offset_x, offset_z, hp_preset, hp_seed, post_log);
            if (!ok) {
                result.status = 1;
            }
        }

        if (st.stop_requested()) return;
        Glib::signal_idle().connect_once([this, result, post_log = std::move(post_log)]() {
            auto tbuf = log_view_.get_buffer();
            if (!post_log.empty()) {
                tbuf->insert(tbuf->end(), "\n");
                tbuf->insert(tbuf->end(), post_log);
                tbuf->insert(tbuf->end(), "\n");
            }
            if (result.status == 0) {
                status_label_.set_text("Project generated successfully.");
            } else {
                status_label_.set_text("Generation failed (exit " + std::to_string(result.status) + ").");
            }
            generate_button_.set_sensitive(true);
        });
    });
}

bool TabWrpProject::apply_heightpipe_to_project(
    const std::string& wrp_path,
    const std::string& output_dir,
    int scale,
    double offset_x,
    double offset_z,
    hp::CorrectionPreset preset,
    uint32_t seed,
    std::string& log_text) {

    try {
        if (scale != 2 && scale != 4 && scale != 8 && scale != 16) {
            log_text = "heightpipe: skipped (scale must be 2/4/8/16).";
            return true;
        }

        std::ifstream f(wrp_path, std::ios::binary);
        if (!f) {
            log_text = "heightpipe: error opening WRP: " + wrp_path;
            return false;
        }

        armatools::wrp::WorldData world = armatools::wrp::read(f, {.no_objects = true});
        if (world.elevations.empty()) {
            log_text = "heightpipe: no elevation data in WRP.";
            return false;
        }

        int src_w = world.grid.terrain_x;
        int src_h = world.grid.terrain_y;
        if (static_cast<int>(world.elevations.size()) != src_w * src_h) {
            src_w = world.grid.cells_x;
            src_h = world.grid.cells_y;
        }
        if (static_cast<int>(world.elevations.size()) != src_w * src_h) {
            log_text = std::format(
                "heightpipe: elevation size {} does not match grid {}x{}.",
                world.elevations.size(), src_w, src_h);
            return false;
        }

        hp::Heightmap in(src_w, src_h, 0.0f);
        in.data = world.elevations;

        hp::PipelineOptions opt;
        opt.scale = scale;
        opt.seed = seed;
        opt.resample = hp::ResampleMethod::Bicubic;
        opt.correction = hp::correction_preset_for_scale(scale, preset);
        opt.erosion = hp::erosion_preset_for_scale(scale);

        const hp::PipelineOutputs out = hp::run_pipeline(in, opt);

        const fs::path asc_path = fs::path(output_dir) / "source" / "heightmap.asc";
        fs::create_directories(asc_path.parent_path());
        std::ofstream asc(asc_path);
        if (!asc) {
            log_text = "heightpipe: cannot write " + asc_path.string();
            return false;
        }

        const double cell_size = world.bounds.world_size_x / static_cast<double>(out.out.width);
        asc << std::format("ncols         {}\n", out.out.width);
        asc << std::format("nrows         {}\n", out.out.height);
        asc << std::format("xllcorner     {:.6f}\n", offset_x);
        asc << std::format("yllcorner     {:.6f}\n", offset_z);
        asc << std::format("cellsize      {:.6f}\n", cell_size);
        asc << "NODATA_value  -9999\n";
        for (int row = out.out.height - 1; row >= 0; --row) {
            for (int col = 0; col < out.out.width; ++col) {
                if (col > 0) asc << ' ';
                asc << std::format("{:.4f}", out.out.data[static_cast<size_t>(row * out.out.width + col)]);
            }
            asc << '\n';
        }

        log_text = std::format(
            "heightpipe: wrote corrected source/heightmap.asc ({}x{}, scale {}, seed {}).",
            out.out.width, out.out.height, scale, seed);
        return true;
    } catch (const std::exception& e) {
        log_text = std::string("heightpipe: ") + e.what();
        return false;
    }
}
