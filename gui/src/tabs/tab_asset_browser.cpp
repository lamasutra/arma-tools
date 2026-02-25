#include "tab_asset_browser.h"
#include "audio_draw_util.h"
#include "log_panel.h"
#include "pbo_util.h"
#include "procedural_texture.h"

#include <armatools/config.h>
#include <armatools/armapath.h>
#include <armatools/ogg.h>
#include <armatools/p3d.h>
#include <armatools/paa.h>
#include <armatools/rvmat.h>
#include <armatools/wss.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Icon selection by file extension
// ---------------------------------------------------------------------------
std::string TabAssetBrowser::icon_for_extension(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == ".p3d")                          return "emblem-system-symbolic";
    if (lower == ".paa" || lower == ".pac")        return "image-x-generic-symbolic";
    if (lower == ".ogg" || lower == ".wss" ||
        lower == ".wav")                           return "audio-x-generic-symbolic";
    if (lower == ".bin" || lower == ".rvmat" || lower == ".cpp" ||
        lower == ".hpp")                           return "text-x-generic-symbolic";
    if (lower == ".sqf" || lower == ".sqs")        return "text-x-script-symbolic";
    if (lower == ".wrp")                           return "x-office-address-book-symbolic";
    if (lower == ".pbo")                           return "package-x-generic-symbolic";
    if (lower == ".jpg" || lower == ".jpeg")       return "image-x-generic-symbolic";

    return "text-x-generic-symbolic";
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
TabAssetBrowser::TabAssetBrowser() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    auto make_icon_button = [](Gtk::Button& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
    };

    // Left panel
    left_box_.set_margin(8);
    left_box_.set_size_request(200, -1);

    make_icon_button(build_button_, "database-add-symbolic",
                     "Build DB: build a new asset database from configured sources");
    make_icon_button(update_button_, "view-refresh-symbolic",
                     "Update DB: incrementally update the asset database");
    make_icon_button(search_button_, "system-search-symbolic",
                     "Search files by glob pattern");
    stats_button_.set_tooltip_text("Show database statistics");
    toolbar_box_.append(build_button_);
    toolbar_box_.append(update_button_);
    toolbar_box_.append(stats_button_);
    left_box_.append(toolbar_box_);

    source_label_.set_halign(Gtk::Align::START);
    source_combo_.set_tooltip_text("Filter by PBO source (game directory)");
    source_combo_.append("", "All");
    source_combo_.set_active_id("");
    source_combo_.set_hexpand(true);
    source_box_.append(source_label_);
    source_box_.append(source_combo_);
    left_box_.append(source_box_);

    search_entry_.set_hexpand(true);
    search_entry_.set_placeholder_text("Search pattern (e.g. *.p3d)...");
    search_box_.append(search_entry_);
    search_box_.append(search_button_);
    left_box_.append(search_box_);

    breadcrumb_label_.set_halign(Gtk::Align::START);
    breadcrumb_label_.set_ellipsize(Pango::EllipsizeMode::END);
    left_box_.append(breadcrumb_label_);

    list_scroll_.set_vexpand(true);
    list_scroll_.set_child(dir_list_);
    if (auto adj = list_scroll_.get_vadjustment()) {
        scroll_value_conn_ = adj->signal_value_changed().connect(
            sigc::mem_fun(*this, &TabAssetBrowser::try_load_next_page));
    }
    left_box_.append(list_scroll_);

    status_label_.set_halign(Gtk::Align::START);
    left_box_.append(status_label_);

    set_start_child(left_box_);
    set_position(400);

    // Right panel
    right_box_.set_margin(8);

    file_info_label_.set_halign(Gtk::Align::START);
    file_info_label_.set_wrap(false);
    file_info_label_.set_single_line_mode(true);
    file_info_label_.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    file_info_label_.set_selectable(true);
    right_box_.append(file_info_label_);

    info_view_.set_editable(false);
    info_view_.set_monospace(true);
    info_scroll_.set_vexpand(true);
    info_scroll_.set_child(info_view_);
    right_box_.append(info_scroll_);

    preview_picture_.set_can_shrink(true);
    preview_picture_.set_content_fit(Gtk::ContentFit::CONTAIN);
    preview_scroll_.set_child(preview_picture_);
    preview_scroll_.set_vexpand(true);
    preview_scroll_.set_visible(false);
    right_box_.append(preview_scroll_);

    rvmat_info_view_.set_editable(false);
    rvmat_info_view_.set_monospace(true);
    rvmat_info_scroll_.set_child(rvmat_info_view_);
    rvmat_info_scroll_.set_hexpand(true);
    rvmat_info_scroll_.set_vexpand(true);
    rvmat_paned_.set_start_child(rvmat_info_scroll_);
    rvmat_paned_.set_end_child(rvmat_preview_);
    rvmat_paned_.set_resize_start_child(true);
    rvmat_paned_.set_resize_end_child(true);
    rvmat_paned_.set_shrink_start_child(false);
    rvmat_paned_.set_shrink_end_child(false);
    rvmat_paned_.set_position(420);
    rvmat_paned_.set_vexpand(true);
    rvmat_paned_.set_visible(false);
    right_box_.append(rvmat_paned_);

    model_panel_.set_vexpand(true);
    model_panel_.set_visible(false);
    right_box_.append(model_panel_);

    // --- Audio panel ---
    audio_panel_.set_visible(false);

    audio_info_label_.set_halign(Gtk::Align::START);
    audio_info_label_.set_valign(Gtk::Align::START);
    audio_info_label_.set_wrap(true);
    audio_info_label_.set_selectable(true);
    audio_info_scroll_.set_child(audio_info_label_);
    audio_info_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    audio_info_scroll_.set_hexpand(true);
    audio_info_scroll_.set_max_content_height(100);
    audio_info_scroll_.set_propagate_natural_height(true);
    audio_panel_.append(audio_info_scroll_);

    // Waveform
    audio_waveform_area_.set_hexpand(true);
    audio_waveform_area_.set_vexpand(true);
    audio_waveform_area_.set_draw_func(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_draw_waveform));
    auto wf_click = Gtk::GestureClick::create();
    wf_click->signal_released().connect(
        [this](int, double x, double) {
            int w = audio_waveform_area_.get_width();
            if (w > 0) audio_on_seek(x / w);
        });
    audio_waveform_area_.add_controller(wf_click);

    // Spectrogram
    audio_spectrogram_area_.set_hexpand(true);
    audio_spectrogram_area_.set_vexpand(true);
    audio_spectrogram_area_.set_draw_func(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_draw_spectrogram));
    auto sp_click = Gtk::GestureClick::create();
    sp_click->signal_released().connect(
        [this](int, double x, double) {
            int w = audio_spectrogram_area_.get_width();
            if (w > 0) audio_on_seek(x / w);
        });
    audio_spectrogram_area_.add_controller(sp_click);

    // Paned: waveform | spectrogram
    audio_paned_.set_start_child(audio_waveform_area_);
    audio_paned_.set_end_child(audio_spectrogram_area_);
    audio_paned_.set_resize_start_child(true);
    audio_paned_.set_resize_end_child(true);
    audio_paned_.set_shrink_start_child(false);
    audio_paned_.set_shrink_end_child(false);
    audio_paned_.set_vexpand(true);
    audio_paned_.set_wide_handle(true);
    audio_paned_.add_css_class("audio-split");

    audio_panel_.append(audio_paned_);

    // Progress scale
    audio_progress_.set_range(0.0, 1.0);
    audio_progress_.set_increments(0.001, 0.01);
    audio_progress_.set_draw_value(false);
    audio_progress_.set_hexpand(true);
    audio_progress_.signal_value_changed().connect([this]() {
        if (!audio_updating_scale_)
            audio_on_seek(audio_progress_.get_value());
    });
    audio_panel_.append(audio_progress_);

    // Controls
    audio_controls_box_.set_valign(Gtk::Align::CENTER);
    audio_controls_box_.append(audio_play_btn_);
    audio_controls_box_.append(audio_pause_btn_);
    audio_controls_box_.append(audio_stop_btn_);
    audio_time_label_.set_halign(Gtk::Align::END);
    audio_time_label_.set_hexpand(true);
    audio_controls_box_.append(audio_time_label_);
    audio_panel_.append(audio_controls_box_);

    audio_play_btn_.set_sensitive(false);
    audio_pause_btn_.set_sensitive(false);
    audio_stop_btn_.set_sensitive(false);

    audio_play_btn_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_on_play));
    audio_pause_btn_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_on_pause));
    audio_stop_btn_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_on_stop));

    right_box_.append(audio_panel_);

    // Extract row (no Play button — controls are in audio panel now)
    extract_button_.set_tooltip_text("Extract selected file from PBO to disk");
    extract_drive_button_.set_tooltip_text("Extract file to drive root preserving path structure");
    extract_box_.append(extract_button_);
    extract_box_.append(extract_drive_button_);
    right_box_.append(extract_box_);

    set_end_child(right_box_);

    // Signals
    build_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_build_db));
    update_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_update_db));
    stats_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_stats));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_search));
    search_entry_.signal_activate().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_search));
    source_combo_.signal_changed().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_source_changed));
    dir_list_.signal_row_activated().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_row_activated));
    dir_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_row_selected));
    extract_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_extract));
    extract_drive_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAssetBrowser::on_extract_to_drive));
}

TabAssetBrowser::~TabAssetBrowser() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    if (scroll_value_conn_.connected()) scroll_value_conn_.disconnect();
    audio_stop_all();
    ++nav_generation_; // cancel any pending navigate
    if (nav_thread_.joinable()) {
        nav_thread_.request_stop();
        nav_thread_.join();
    }
}

void TabAssetBrowser::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabAssetBrowser::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_panel_.set_model_loader_service(service);
}

void TabAssetBrowser::set_texture_loader_service(
    const std::shared_ptr<LodTexturesLoaderService>& service) {
    model_panel_.set_texture_loader_service(service);
}

void TabAssetBrowser::set_config(Config* cfg) {
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

        if (db_ && index_) {
            app_log(LogLevel::Info, "Asset DB opened: " + snap.db_path);
            model_panel_.set_config(cfg_);
            model_panel_.set_pboindex(db_.get(), index_.get());
            refresh_source_combo();
            breadcrumb_label_.set_text("/");
            status_label_.set_text("Asset DB ready. Use Search or select source to browse.");
            return;
        }

        if (snap.error.empty()) return;
        bool outdated = snap.error.find("schema version mismatch") != std::string::npos
                     || snap.error.find("incompatible") != std::string::npos
                     || snap.error.find("missing required table") != std::string::npos;
        if (outdated) {
            app_log(LogLevel::Warning, "Outdated DB schema, rebuilding: " + snap.error);
            db_.reset();
            index_.reset();
            std::error_code ec;
            fs::remove(cfg_->a3db_path, ec);
            fs::remove(cfg_->a3db_path + "-wal", ec);
            fs::remove(cfg_->a3db_path + "-shm", ec);
            on_build_db();
        } else {
            app_log(LogLevel::Error, std::string("Asset DB open error: ") + snap.error);
            status_label_.set_text(std::string("DB open error: ") + snap.error);
            index_.reset();
        }
    });
}

void TabAssetBrowser::open_db() {
    if (pbo_index_service_) pbo_index_service_->refresh();
}

void TabAssetBrowser::refresh_source_combo() {
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
            std::string label = (it != source_labels.end()) ? it->second : src;
            source_combo_.append(src, label);
        }
    }

    source_combo_.set_active_id("");
    current_source_.clear();
    source_combo_updating_ = false;
}

void TabAssetBrowser::on_source_changed() {
    if (source_combo_updating_) return;
    current_source_ = std::string(source_combo_.get_active_id());
    navigate("");
}

void TabAssetBrowser::on_build_db() {
    if (!cfg_) return;
    if (cfg_->a3db_path.empty()) {
        status_label_.set_text("Error: a3db_path not configured.");
        return;
    }
    if (cfg_->arma3_dir.empty() && cfg_->ofp_dir.empty() && cfg_->arma1_dir.empty() && cfg_->arma2_dir.empty()) {
        status_label_.set_text("Error: no game directory configured.");
        return;
    }

    app_log(LogLevel::Info, "Building asset database...");
    {
        std::string cmd = "build_db -db " + cfg_->a3db_path;
        if (!cfg_->arma3_dir.empty()) cmd += " -arma3 " + cfg_->arma3_dir;
        if (!cfg_->workshop_dir.empty()) cmd += " -workshop " + cfg_->workshop_dir;
        if (!cfg_->ofp_dir.empty()) cmd += " -ofp " + cfg_->ofp_dir;
        if (!cfg_->arma1_dir.empty()) cmd += " -arma1 " + cfg_->arma1_dir;
        if (!cfg_->arma2_dir.empty()) cmd += " -arma2 " + cfg_->arma2_dir;
        if (cfg_->asset_browser_defaults.on_demand_metadata) cmd += " -ondemand";
        app_log(LogLevel::Debug, "exec: " + cmd);
    }
    build_button_.set_sensitive(false);
    update_button_.set_sensitive(false);
    status_label_.set_text("Building database...");

    auto db_path = cfg_->a3db_path;
    auto arma3_dir = cfg_->arma3_dir;
    auto workshop_dir = cfg_->workshop_dir;
    auto on_demand = cfg_->asset_browser_defaults.on_demand_metadata;
    armatools::pboindex::GameDirs game_dirs{cfg_->ofp_dir, cfg_->arma1_dir, cfg_->arma2_dir};

    db_.reset();
    index_.reset();

    std::thread([this, db_path, arma3_dir, workshop_dir, on_demand, game_dirs]() {
        try {
            armatools::pboindex::BuildOptions opts;
            opts.on_demand_metadata = on_demand;

            auto last_update = std::chrono::steady_clock::now();
            auto result = armatools::pboindex::DB::build_db(
                db_path, arma3_dir, workshop_dir, {}, opts,
                [this, &last_update](const armatools::pboindex::BuildProgress& p) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_update).count();
                    if (p.phase == "warning") {
                        auto warn = fs::path(p.pbo_path).filename().string() + ": " + p.file_name;
                        Glib::signal_idle().connect_once([this, warn]() {
                            app_log(LogLevel::Warning, warn);
                        });
                        return;
                    }
                    if (elapsed < 100 && p.phase != "discovery" && p.phase != "commit")
                        return; // Throttle: update UI at most 10x/sec
                    last_update = now;

                    auto msg = p.phase + ": " + std::to_string(p.pbo_index) +
                               "/" + std::to_string(p.pbo_total);
                    if (!p.pbo_path.empty()) {
                        msg += " " + fs::path(p.pbo_path).filename().string();
                    }
                    Glib::signal_idle().connect_once([this, msg]() {
                        status_label_.set_text(msg);
                    });
                },
                game_dirs);

            Glib::signal_idle().connect_once([this, result]() {
                auto msg = "Build complete: " + std::to_string(result.pbo_count) + " PBOs, " +
                    std::to_string(result.file_count) + " files";
                status_label_.set_text(msg);
                app_log(LogLevel::Info, msg);
                build_button_.set_sensitive(true);
                update_button_.set_sensitive(true);
                open_db();
            });
        } catch (const std::exception& e) {
            auto msg = std::string(e.what());
            Glib::signal_idle().connect_once([this, msg]() {
                status_label_.set_text("Build error: " + msg);
                app_log(LogLevel::Error, "Asset DB build error: " + msg);
                build_button_.set_sensitive(true);
                update_button_.set_sensitive(true);
            });
        }
    }).detach();
}

void TabAssetBrowser::on_update_db() {
    if (!cfg_) return;
    if (cfg_->a3db_path.empty() || !fs::exists(cfg_->a3db_path)) {
        status_label_.set_text("Error: No database to update. Build first.");
        return;
    }

    app_log(LogLevel::Info, "Updating asset database...");
    {
        std::string cmd = "update_db -db " + cfg_->a3db_path;
        if (!cfg_->arma3_dir.empty()) cmd += " -arma3 " + cfg_->arma3_dir;
        if (!cfg_->workshop_dir.empty()) cmd += " -workshop " + cfg_->workshop_dir;
        if (!cfg_->ofp_dir.empty()) cmd += " -ofp " + cfg_->ofp_dir;
        if (!cfg_->arma1_dir.empty()) cmd += " -arma1 " + cfg_->arma1_dir;
        if (!cfg_->arma2_dir.empty()) cmd += " -arma2 " + cfg_->arma2_dir;
        if (cfg_->asset_browser_defaults.on_demand_metadata) cmd += " -ondemand";
        app_log(LogLevel::Debug, "exec: " + cmd);
    }
    build_button_.set_sensitive(false);
    update_button_.set_sensitive(false);
    status_label_.set_text("Updating database...");

    auto db_path = cfg_->a3db_path;
    auto arma3_dir = cfg_->arma3_dir;
    auto workshop_dir = cfg_->workshop_dir;
    auto on_demand = cfg_->asset_browser_defaults.on_demand_metadata;
    armatools::pboindex::GameDirs game_dirs{cfg_->ofp_dir, cfg_->arma1_dir, cfg_->arma2_dir};

    db_.reset();
    index_.reset();

    std::thread([this, db_path, arma3_dir, workshop_dir, on_demand, game_dirs]() {
        try {
            armatools::pboindex::BuildOptions opts;
            opts.on_demand_metadata = on_demand;

            auto last_update = std::chrono::steady_clock::now();
            auto result = armatools::pboindex::DB::update_db(
                db_path, arma3_dir, workshop_dir, {}, opts,
                [this, &last_update](const armatools::pboindex::BuildProgress& p) {
                    if (p.phase == "warning") {
                        auto warn = fs::path(p.pbo_path).filename().string() + ": " + p.file_name;
                        Glib::signal_idle().connect_once([this, warn]() {
                            app_log(LogLevel::Warning, warn);
                        });
                        return;
                    }
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_update).count();
                    if (elapsed < 100 && p.phase != "discovery" && p.phase != "commit")
                        return;
                    last_update = now;

                    auto msg = p.phase + ": " + std::to_string(p.pbo_index) +
                               "/" + std::to_string(p.pbo_total);
                    Glib::signal_idle().connect_once([this, msg]() {
                        status_label_.set_text(msg);
                    });
                },
                game_dirs);

            Glib::signal_idle().connect_once([this, result]() {
                auto msg = "Update complete: +" + std::to_string(result.added) +
                    " -" + std::to_string(result.removed) +
                    " ~" + std::to_string(result.updated);
                status_label_.set_text(msg);
                app_log(LogLevel::Info, msg);
                build_button_.set_sensitive(true);
                update_button_.set_sensitive(true);
                open_db();
            });
        } catch (const std::exception& e) {
            auto msg = std::string(e.what());
            bool outdated = msg.find("schema version mismatch") != std::string::npos
                         || msg.find("incompatible") != std::string::npos;
            if (outdated) {
                // Schema outdated — delete and do full rebuild.
                std::error_code ec;
                fs::remove(db_path, ec);
                fs::remove(db_path + "-wal", ec);
                fs::remove(db_path + "-shm", ec);
                Glib::signal_idle().connect_once([this, msg]() {
                    app_log(LogLevel::Warning, "Outdated DB schema, rebuilding: " + msg);
                    status_label_.set_text("Schema outdated, rebuilding...");
                    on_build_db();
                });
            } else {
                Glib::signal_idle().connect_once([this, msg]() {
                    status_label_.set_text("Update error: " + msg);
                    app_log(LogLevel::Error, "Asset DB update error: " + msg);
                    build_button_.set_sensitive(true);
                    update_button_.set_sensitive(true);
                });
            }
        }
    }).detach();
}

void TabAssetBrowser::on_stats() {
    if (!db_) {
        info_view_.get_buffer()->set_text("No database loaded.");
        return;
    }

    try {
        auto s = db_->stats();
        std::ostringstream out;
        out << "Schema version: " << s.schema_version << "\n"
            << "Created: " << s.created_at << "\n"
            << "Arma 3 dir: " << s.arma3_dir << "\n"
            << "Workshop dir: " << s.workshop_dir << "\n";
        if (!s.ofp_dir.empty()) out << "OFP/CWA dir: " << s.ofp_dir << "\n";
        if (!s.arma1_dir.empty()) out << "Arma 1 dir: " << s.arma1_dir << "\n";
        if (!s.arma2_dir.empty()) out << "Arma 2 dir: " << s.arma2_dir << "\n";
        if (!s.mod_dirs.empty()) {
            out << "Mod dirs:\n";
            for (const auto& d : s.mod_dirs) out << "  " << d << "\n";
        }
        auto sources = db_->query_sources();
        if (!sources.empty()) {
            out << "\nSources:\n";
            for (const auto& src : sources) {
                out << "  " << src << "\n";
            }
        }

        out << "\nPBOs: " << s.pbo_count
            << " (" << s.pbos_with_prefix << " with prefix)\n"
            << "Files: " << s.file_count << "\n"
            << "Total data: " << (s.total_data_size / (1024 * 1024)) << " MB\n"
            << "P3D models: " << s.p3d_model_count << "\n"
            << "Textures: " << s.texture_count << "\n"
            << "Audio files: " << s.audio_file_count << "\n";

        file_info_label_.set_text("Database Statistics");
        info_view_.get_buffer()->set_text(out.str());
        preview_scroll_.set_visible(false);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("Error: ") + e.what());
    }
}

void TabAssetBrowser::on_search() {
    if (!db_) {
        status_label_.set_text("No database loaded.");
        return;
    }

    auto pattern = std::string(search_entry_.get_text());
    if (pattern.empty()) return;

    unsigned gen = ++nav_generation_;
    browse_is_search_ = true;
    active_search_pattern_ = pattern;
    has_more_results_ = true;
    loading_more_results_ = false;
    current_offset_ = 0;
    status_label_.set_text("Searching...");
    search_button_.set_sensitive(false);
    load_next_search_page(gen, true);
}

void TabAssetBrowser::navigate(const std::string& path) {
    if (!db_) return;

    current_path_ = path;
    browse_is_search_ = false;
    has_more_results_ = true;
    loading_more_results_ = false;
    current_offset_ = 0;
    breadcrumb_label_.set_text(path.empty() ? "/" : path);
    status_label_.set_text("Loading...");
    unsigned gen = ++nav_generation_;
    load_next_directory_page(gen, true);
}

void TabAssetBrowser::load_next_search_page(unsigned gen, bool reset) {
    if (!cfg_ || loading_more_results_ || !has_more_results_) return;
    loading_more_results_ = true;
    status_label_.set_text(reset ? "Searching..." : "Loading more...");

    if (nav_thread_.joinable()) {
        nav_thread_.request_stop();
        nav_thread_.join();
    }

    auto db_path = cfg_->a3db_path;
    auto source = current_source_;
    auto pattern = active_search_pattern_;
    auto offset = current_offset_;
    nav_thread_ = std::jthread([this, db_path, source, pattern, offset, gen, reset](std::stop_token st) {
        if (st.stop_requested()) return;
        try {
            auto db = armatools::pboindex::DB::open(db_path);
            if (st.stop_requested()) return;
            auto results = db.find_files(pattern, source, kPageSize, offset);
            Glib::signal_idle().connect_once(
                [this, pattern, source, results = std::move(results), gen, reset]() mutable {
                    if (gen != nav_generation_.load()) return;
                    has_more_results_ = results.size() == kPageSize;
                    append_search_results_page(results, reset);
                    current_offset_ += results.size();
                    loading_more_results_ = false;
                    status_label_.set_text(has_more_results_ ? "Scroll to load more..." : "");
                    search_button_.set_sensitive(true);
                    app_log(LogLevel::Info, "Search '" + pattern + "'" +
                        (source.empty() ? "" : " [" + source + "]") +
                        ": loaded " + std::to_string(search_results_.size()) +
                        (has_more_results_ ? "+" : "") + " results");
                });
        } catch (const std::exception& e) {
            auto msg = std::string(e.what());
            Glib::signal_idle().connect_once([this, msg, gen]() {
                if (gen != nav_generation_.load()) return;
                app_log(LogLevel::Error, "Search error: " + msg);
                status_label_.set_text("Search error: " + msg);
                loading_more_results_ = false;
                search_button_.set_sensitive(true);
            });
        }
    });
}

void TabAssetBrowser::load_next_directory_page(unsigned gen, bool reset) {
    if (!cfg_ || loading_more_results_ || !has_more_results_) return;
    loading_more_results_ = true;
    status_label_.set_text(reset ? "Loading..." : "Loading more...");

    if (nav_thread_.joinable()) {
        nav_thread_.request_stop();
        nav_thread_.join();
    }

    auto db_path = cfg_->a3db_path;
    auto source = current_source_;
    auto path = current_path_;
    auto offset = current_offset_;
    nav_thread_ = std::jthread([this, db_path, source, path, offset, gen, reset](std::stop_token st) {
        if (st.stop_requested()) return;
        try {
            auto db = armatools::pboindex::DB::open(db_path);
            std::vector<armatools::pboindex::DirEntry> entries;
            if (source.empty()) {
                entries = db.list_dir(path, kPageSize, offset);
            } else {
                entries = db.list_dir_for_source(path, source, kPageSize, offset);
            }
            if (st.stop_requested()) return;
            Glib::signal_idle().connect_once([this, entries = std::move(entries), gen, reset]() mutable {
                if (gen != nav_generation_.load()) return;
                append_directory_page(entries, reset);
                current_offset_ += entries.size();
                has_more_results_ = entries.size() == kPageSize;
                loading_more_results_ = false;
                status_label_.set_text(has_more_results_ ? "Scroll to load more..." : "");
            });
        } catch (const std::exception& e) {
            auto msg = std::string(e.what());
            Glib::signal_idle().connect_once([this, msg, gen]() {
                if (gen != nav_generation_.load()) return;
                app_log(LogLevel::Error, "Navigate error: " + msg);
                status_label_.set_text("Navigate error: " + msg);
                loading_more_results_ = false;
            });
        }
    });
}

void TabAssetBrowser::append_directory_page(
    const std::vector<armatools::pboindex::DirEntry>& entries,
    bool reset) {
    if (reset) {
        dir_list_.set_visible(false);
        search_results_.clear();
        current_entries_.clear();
        dir_list_.unselect_all();
        while (auto* row = dir_list_.get_row_at_index(0))
            dir_list_.remove(*row);

        if (!current_path_.empty()) {
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name("go-up-symbolic");
            auto* label = Gtk::make_managed<Gtk::Label>("..");
            label->set_halign(Gtk::Align::START);
            box->append(*icon);
            box->append(*label);
            dir_list_.append(*box);
        }
    }

    for (const auto& entry : entries) {
        current_entries_.push_back(entry);
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        if (entry.is_dir) {
            icon->set_from_icon_name("folder-symbolic");
        } else {
            auto ext = fs::path(entry.name).extension().string();
            icon->set_from_icon_name(icon_for_extension(ext));
        }
        auto* label = Gtk::make_managed<Gtk::Label>(entry.name);
        label->set_halign(Gtk::Align::START);
        label->set_hexpand(true);
        box->append(*icon);
        box->append(*label);

        if (!entry.is_dir && !entry.files.empty()) {
            auto size_str = std::to_string(entry.files[0].data_size) + " B";
            auto* size_label = Gtk::make_managed<Gtk::Label>(size_str);
            size_label->add_css_class("dim-label");
            box->append(*size_label);
        }

        dir_list_.append(*box);
    }

    dir_list_.set_visible(true);
}

void TabAssetBrowser::append_search_results_page(
    const std::vector<armatools::pboindex::FindResult>& results,
    bool reset) {
    if (reset) {
        search_results_.clear();
        current_entries_.clear();
        dir_list_.unselect_all();
        while (auto* row = dir_list_.get_row_at_index(0))
            dir_list_.remove(*row);
        current_path_.clear();
    }

    for (const auto& r : results) {
        search_results_.push_back(r);
        auto display = r.prefix + "/" + r.file_path;
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        dir_list_.append(*label);
    }

    breadcrumb_label_.set_text("Search results: " +
                               std::to_string(search_results_.size()) +
                               (has_more_results_ ? "+" : "") + " files");
}

void TabAssetBrowser::try_load_next_page() {
    if (!has_more_results_ || loading_more_results_) return;
    auto adj = list_scroll_.get_vadjustment();
    if (!adj) return;
    double bottom = adj->get_value() + adj->get_page_size();
    if ((adj->get_upper() - bottom) > 120.0) return;

    unsigned gen = nav_generation_.load();
    if (browse_is_search_) load_next_search_page(gen, false);
    else load_next_directory_page(gen, false);
}

void TabAssetBrowser::populate_list(
    const std::vector<armatools::pboindex::DirEntry>& entries) {
    append_directory_page(entries, true);
}

void TabAssetBrowser::show_search_results(
    const std::vector<armatools::pboindex::FindResult>& results) {
    append_search_results_page(results, true);
}

void TabAssetBrowser::on_row_activated(Gtk::ListBoxRow* row) {
    if (!row || !db_) return;

    int idx = row->get_index();

    // If showing search results (breadcrumb starts with "Search")
    auto bc = std::string(breadcrumb_label_.get_text());
    if (bc.starts_with("Search results:")) {
        if (idx >= 0 && static_cast<size_t>(idx) < search_results_.size()) {
            show_file_info(search_results_[static_cast<size_t>(idx)]);
        }
        return;
    }

    // Handle ".." entry
    int offset = current_path_.empty() ? 0 : 1;
    if (!current_path_.empty() && idx == 0) {
        // Go up: strip last path component (no trailing slash convention)
        auto pos = current_path_.rfind('/');
        if (pos == std::string::npos) {
            navigate("");
        } else {
            navigate(current_path_.substr(0, pos));
        }
        return;
    }

    // Normal directory entry
    auto entry_idx = static_cast<size_t>(idx - offset);
    if (entry_idx >= current_entries_.size()) return;
    const auto& entry = current_entries_[entry_idx];
    if (entry.is_dir) {
        navigate(current_path_.empty() ? entry.name : current_path_ + "/" + entry.name);
    } else if (!entry.files.empty()) {
        show_file_info(entry.files[0]);
    }
}

void TabAssetBrowser::on_row_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    armatools::pboindex::FindResult file;
    if (!get_selected_file(file)) return;
    show_file_info(file);
}

void TabAssetBrowser::show_file_info(const armatools::pboindex::FindResult& file) {
    std::ostringstream info;
    info << file.file_path
         << " | " << file.data_size << " bytes"
         << " | prefix: " << (file.prefix.empty() ? "-" : file.prefix)
         << " | pbo: " << fs::path(file.pbo_path).filename().string();

    file_info_label_.set_text(info.str());
    info_view_.get_buffer()->set_text("");
    info_scroll_.set_visible(true);
    preview_scroll_.set_visible(false);
    preview_picture_.set_paintable({});
    rvmat_paned_.set_visible(false);
    model_panel_.set_visible(false);
    audio_panel_.set_visible(false);
    audio_stop_all();

    // Determine file type
    auto ext = fs::path(file.file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".p3d") {
        preview_p3d(file);
    } else if (ext == ".paa" || ext == ".pac") {
        preview_paa(file);
    } else if (ext == ".ogg" || ext == ".wss" || ext == ".wav") {
        preview_audio(file);
    } else if (ext == ".rvmat") {
        preview_rvmat(file);
    } else if (ext == ".bin") {
        preview_config(file);
    } else if (ext == ".jpg" || ext == ".jpeg") {
        preview_jpg(file);
    } else if (ext == ".hpp" || ext == ".cpp" || ext == ".sqf" ||
               ext == ".sqs" || ext == ".ext" || ext == ".h" ||
               ext == ".inc" || ext == ".cfg") {
        preview_text(file);
    }
}

static std::vector<uint8_t> extract_from_pbo_file(
    const armatools::pboindex::FindResult& file) {
    return extract_from_pbo(file.pbo_path, file.file_path);
}

void TabAssetBrowser::preview_p3d(const armatools::pboindex::FindResult& file) {
    model_panel_.load_p3d(file.prefix + "/" + file.file_path);
    model_panel_.set_visible(true);
    info_scroll_.set_visible(false);
}

void TabAssetBrowser::preview_paa(const armatools::pboindex::FindResult& file) {
    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        auto [img, hdr] = armatools::paa::decode(stream);

        std::ostringstream info;
        info << file.file_path
             << " | " << file.data_size << " bytes"
             << " | " << hdr.format
             << " | " << hdr.width << "x" << hdr.height;
        file_info_label_.set_text(info.str());
        info_scroll_.set_visible(false);

        // Show image preview
        auto pixbuf = Gdk::Pixbuf::create_from_data(
            img.pixels.data(),
            Gdk::Colorspace::RGB,
            true, 8,
            img.width, img.height,
            img.width * 4);
        auto copy = pixbuf->copy();
        auto texture = Gdk::Texture::create_for_pixbuf(copy);
        preview_picture_.set_paintable(texture);
        preview_scroll_.set_visible(true);
        preview_scroll_.set_size_request(-1, 380);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("PAA error: ") + e.what());
    }
}

void TabAssetBrowser::preview_audio(const armatools::pboindex::FindResult& file) {
    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        auto ext = fs::path(file.file_path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        auto display_name = fs::path(file.file_path).filename().string();

        audio_load_from_memory(data.data(), data.size(), ext, display_name);
        info_scroll_.set_visible(false);
        audio_panel_.set_visible(true);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("Audio error: ") + e.what());
    }
}

void TabAssetBrowser::preview_config(const armatools::pboindex::FindResult& file) {
    if (cfg_ && !cfg_->asset_browser_defaults.auto_derap) {
        info_view_.get_buffer()->set_text("(Auto-derap disabled in configuration)");
        return;
    }

    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        auto config = armatools::config::read(stream);

        std::ostringstream out;
        armatools::config::write_text(out, config);
        info_view_.get_buffer()->set_text(out.str());
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("Config error: ") + e.what());
    }
}

void TabAssetBrowser::preview_rvmat(const armatools::pboindex::FindResult& file) {
    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        std::string str(data.begin(), data.end());
        std::istringstream stream(str, std::ios::binary);

        armatools::config::Config cfg;
        if (data.size() >= 4 && data[0] == 0x00 &&
            data[1] == 'r' && data[2] == 'a' && data[3] == 'P') {
            cfg = armatools::config::read(stream);
        } else {
            cfg = armatools::config::parse_text(stream);
        }

        auto mat = armatools::rvmat::parse(cfg);
        rvmat_preview_.clear_material();
        GLRvmatPreview::MaterialParams mp;
        mp.ambient[0] = mat.ambient[0];
        mp.ambient[1] = mat.ambient[1];
        mp.ambient[2] = mat.ambient[2];
        mp.diffuse[0] = mat.diffuse[0];
        mp.diffuse[1] = mat.diffuse[1];
        mp.diffuse[2] = mat.diffuse[2];
        mp.emissive[0] = mat.emissive[0];
        mp.emissive[1] = mat.emissive[1];
        mp.emissive[2] = mat.emissive[2];
        mp.specular[0] = mat.specular[0];
        mp.specular[1] = mat.specular[1];
        mp.specular[2] = mat.specular[2];
        mp.specular_power = std::max(2.0f, mat.specular_power);
        rvmat_preview_.set_material_params(mp);

        auto lower = [](std::string v) {
            std::transform(v.begin(), v.end(), v.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        };
        auto stage_rank = [&](const std::string& path, const std::string& kind) {
            auto p = lower(path);
            if (kind == "diff") {
                if (p.find("_mco.") != std::string::npos) return 40;
                if (p.find("_co.") != std::string::npos) return 30;
                if (p.find("_ca.") != std::string::npos) return 20;
                return 1;
            }
            if (kind == "nrm") return p.find("_nohq.") != std::string::npos ? 100 : 0;
            if (kind == "spec") return p.find("_smdi.") != std::string::npos ? 100 : 0;
            return 0;
        };

        std::string best_diff;
        std::string best_nrm;
        std::string best_spec;
        int rd = -1, rn = -1, rs = -1;
        for (const auto& st : mat.stages) {
            if (st.texture_path.empty()) continue;
            int sd = stage_rank(st.texture_path, "diff");
            int sn = stage_rank(st.texture_path, "nrm");
            int ss = stage_rank(st.texture_path, "spec");
            if (sd > rd) { rd = sd; best_diff = st.texture_path; }
            if (sn > rn) { rn = sn; best_nrm = st.texture_path; }
            if (ss > rs) { rs = ss; best_spec = st.texture_path; }
        }

        if (!best_diff.empty()) {
            if (auto tex = load_preview_texture_asset(file, best_diff)) {
                rvmat_preview_.set_diffuse_texture(tex->width, tex->height, tex->pixels.data());
            }
        }
        if (!best_nrm.empty()) {
            if (auto tex = load_preview_texture_asset(file, best_nrm)) {
                rvmat_preview_.set_normal_texture(tex->width, tex->height, tex->pixels.data());
            }
        }
        if (!best_spec.empty()) {
            if (auto tex = load_preview_texture_asset(file, best_spec)) {
                rvmat_preview_.set_specular_texture(tex->width, tex->height, tex->pixels.data());
            }
        }

        auto fmt_rgba = [](const std::array<float, 4>& c) {
            std::ostringstream s;
            s << std::fixed << std::setprecision(3)
              << c[0] << ", " << c[1] << ", " << c[2] << ", " << c[3];
            return s.str();
        };

        std::ostringstream out;
        out << "Type: RVMAT\n"
            << "Pixel shader: " << (mat.pixel_shader.empty() ? "-" : mat.pixel_shader) << "\n"
            << "Vertex shader: " << (mat.vertex_shader.empty() ? "-" : mat.vertex_shader) << "\n"
            << "Surface: " << (mat.surface.empty() ? "-" : mat.surface) << "\n"
            << "Specular power: " << mat.specular_power << "\n"
            << "Ambient: " << fmt_rgba(mat.ambient) << "\n"
            << "Diffuse: " << fmt_rgba(mat.diffuse) << "\n"
            << "ForcedDiffuse: " << fmt_rgba(mat.forced_diffuse) << "\n"
            << "Emissive: " << fmt_rgba(mat.emissive) << "\n"
            << "Specular: " << fmt_rgba(mat.specular) << "\n"
            << "Stages: " << mat.stages.size() << "\n";

        for (const auto& st : mat.stages) {
            out << "  Stage" << st.stage_number
                << " texture: " << (st.texture_path.empty() ? "-" : st.texture_path) << "\n"
                << "  Stage" << st.stage_number
                << " uvSource: " << (st.uv_source.empty() ? "-" : st.uv_source) << "\n";
        }

        rvmat_info_view_.get_buffer()->set_text(out.str());
        info_scroll_.set_visible(false);
        rvmat_paned_.set_visible(true);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("RVMAT error: ") + e.what());
    }
}

void TabAssetBrowser::preview_jpg(const armatools::pboindex::FindResult& file) {
    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        auto loader = Gdk::PixbufLoader::create();
        loader->write(data.data(), data.size());
        loader->close();

        auto pixbuf = loader->get_pixbuf();
        if (!pixbuf) {
            info_view_.get_buffer()->set_text("Failed to decode JPG image.");
            return;
        }

        std::ostringstream out;
        out << "Format: JPEG\n"
            << "Dimensions: " << pixbuf->get_width() << " x "
            << pixbuf->get_height() << "\n";
        info_view_.get_buffer()->set_text(out.str());

        auto texture = Gdk::Texture::create_for_pixbuf(pixbuf);
        preview_picture_.set_paintable(texture);
        preview_scroll_.set_visible(true);
        preview_scroll_.set_size_request(-1, 256);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("JPG error: ") + e.what());
    }
}

void TabAssetBrowser::preview_text(const armatools::pboindex::FindResult& file) {
    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            info_view_.get_buffer()->set_text("Could not extract file from PBO.");
            return;
        }

        // Cap at 500KB to avoid UI freeze
        static constexpr size_t kMaxTextSize = 500 * 1024;
        size_t len = std::min(data.size(), kMaxTextSize);
        std::string text(reinterpret_cast<const char*>(data.data()), len);
        if (data.size() > kMaxTextSize) {
            text += "\n\n... (truncated at 500KB, total " +
                    std::to_string(data.size()) + " bytes)";
        }

        info_view_.get_buffer()->set_text(text);
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("Text error: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Helper: get the currently selected file from the list
// ---------------------------------------------------------------------------
bool TabAssetBrowser::get_selected_file(armatools::pboindex::FindResult& out) {
    if (!db_) return false;

    auto* row = dir_list_.get_selected_row();
    if (!row) return false;

    int idx = row->get_index();

    auto bc = std::string(breadcrumb_label_.get_text());
    if (bc.starts_with("Search results:")) {
        if (idx < 0 || static_cast<size_t>(idx) >= search_results_.size()) return false;
        out = search_results_[static_cast<size_t>(idx)];
        return true;
    } else {
        int offset = current_path_.empty() ? 0 : 1;
        auto entry_idx = static_cast<size_t>(idx - offset);
        if (entry_idx >= current_entries_.size()) return false;
        if (current_entries_[entry_idx].is_dir || current_entries_[entry_idx].files.empty())
            return false;
        out = current_entries_[entry_idx].files[0];
        return true;
    }
}

std::optional<TabAssetBrowser::DecodedTexture> TabAssetBrowser::load_preview_texture_asset(
    const armatools::pboindex::FindResult& context_file,
    const std::string& texture_path) {
    if (armatools::armapath::is_procedural_texture(texture_path)) {
        if (auto img = procedural_texture::generate(texture_path)) {
            return DecodedTexture{img->width, img->height, img->pixels};
        }
        return std::nullopt;
    }
    auto normalize = [](std::string p) {
        p = armatools::armapath::to_slash_lower(p);
        while (!p.empty() && (p.front() == '/' || p.front() == '\\'))
            p.erase(p.begin());
        return p;
    };
    auto decode = [](const std::vector<uint8_t>& bytes) -> std::optional<DecodedTexture> {
        if (bytes.empty()) return std::nullopt;
        try {
            std::string str(bytes.begin(), bytes.end());
            std::istringstream stream(str);
            auto [img, _hdr] = armatools::paa::decode(stream);
            if (img.width <= 0 || img.height <= 0) return std::nullopt;
            return DecodedTexture{img.width, img.height, img.pixels};
        } catch (...) {
            return std::nullopt;
        }
    };

    auto try_extract = [&](const std::string& key) -> std::optional<DecodedTexture> {
        if (key.empty()) return std::nullopt;
        if (index_) {
            armatools::pboindex::ResolveResult rr;
            if (index_->resolve(key, rr)) {
                if (auto out = decode(extract_from_pbo(rr.pbo_path, rr.entry_name)))
                    return out;
            }
        }
        if (db_) {
            auto filename = fs::path(key).filename().string();
            auto results = db_->find_files("*" + filename);
            for (const auto& r : results) {
                auto full = armatools::armapath::to_slash_lower(r.prefix + "/" + r.file_path);
                if (full == key || full.ends_with("/" + key)) {
                    if (auto out = decode(extract_from_pbo(r.pbo_path, r.file_path)))
                        return out;
                }
            }
        }
        return std::nullopt;
    };

    auto rel = normalize(texture_path);
    auto base = normalize(context_file.prefix + "/" + context_file.file_path);
    auto candidate = rel;
    if (!(rel.starts_with("a3/") || rel.starts_with("ca/") ||
          rel.starts_with("cup/") || rel.starts_with("dz/"))) {
        candidate = normalize((fs::path(base).parent_path() / fs::path(rel)).generic_string());
    }

    std::vector<std::string> keys{candidate};
    if (fs::path(candidate).extension().empty()) {
        keys.push_back(candidate + ".paa");
        keys.push_back(candidate + ".pac");
    }
    for (const auto& k : keys) {
        if (auto out = try_extract(k)) return out;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Extract (original)
// ---------------------------------------------------------------------------
void TabAssetBrowser::on_extract() {
    if (!db_) return;

    armatools::pboindex::FindResult file;
    if (!get_selected_file(file)) {
        status_label_.set_text("No file selected.");
        return;
    }

    // Ask for output directory
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog, file](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto folder = dialog->select_folder_finish(result);
                if (!folder) return;

                auto out_dir = folder->get_path();
                auto data = extract_from_pbo_file(file);
                if (data.empty()) {
                    status_label_.set_text("Extract failed: could not read from PBO.");
                    return;
                }

                auto out_path = fs::path(out_dir) / fs::path(file.file_path).filename();
                std::ofstream out(out_path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size()));
                app_log(LogLevel::Info, "Extracted: " + out_path.string());
                status_label_.set_text("Extracted to: " + out_path.string());
            } catch (const std::exception& e) {
                status_label_.set_text(std::string("Extract error: ") + e.what());
            } catch (...) {}
        });
}

// ---------------------------------------------------------------------------
// Extract to drive root
// ---------------------------------------------------------------------------
void TabAssetBrowser::on_extract_to_drive() {
    if (!db_ || !cfg_) return;

    if (cfg_->drive_root.empty()) {
        status_label_.set_text("Error: drive_root not configured.");
        return;
    }

    armatools::pboindex::FindResult file;
    if (!get_selected_file(file)) {
        status_label_.set_text("No file selected.");
        return;
    }

    try {
        auto data = extract_from_pbo_file(file);
        if (data.empty()) {
            status_label_.set_text("Extract failed: could not read from PBO.");
            return;
        }

        // Build output path: drive_root / prefix / file_path
        fs::path out_path = fs::path(cfg_->drive_root);
        if (!file.prefix.empty()) {
            out_path /= file.prefix;
        }
        out_path /= file.file_path;

        // Create parent directories
        fs::create_directories(out_path.parent_path());

        std::ofstream out(out_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));

        app_log(LogLevel::Info, "Extracted to drive: " + out_path.string());
        status_label_.set_text("Extracted to: " + out_path.string());
    } catch (const std::exception& e) {
        status_label_.set_text(std::string("Extract error: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// In-process audio player
// ---------------------------------------------------------------------------

void TabAssetBrowser::audio_load_from_memory(const uint8_t* data, size_t size,
                                              const std::string& ext,
                                              const std::string& display_name) {
    audio_stop_all();

    audio_waveform_envelope_.clear();
    audio_spectrogram_surface_.reset();
    audio_waveform_area_.queue_draw();
    audio_spectrogram_area_.queue_draw();

    try {
        audio_decoded_ = decode_memory(data, size, ext);

        // Build info text
        std::ostringstream info;
        info << "File: " << display_name << "\n";

        std::string lower_ext = ext;
        std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

        if (lower_ext == ".ogg") {
            std::string str(reinterpret_cast<const char*>(data), size);
            std::istringstream stream(str);
            auto hdr = armatools::ogg::read_header(stream);
            info << "Format: OGG Vorbis\n"
                 << "Channels: " << hdr.channels << "\n"
                 << "Sample rate: " << hdr.sample_rate << " Hz\n";
            if (!hdr.encoder.empty())
                info << "Encoder: " << hdr.encoder << "\n";
        } else if (lower_ext == ".wss" || lower_ext == ".wav") {
            std::string str(reinterpret_cast<const char*>(data), size);
            std::istringstream stream(str);
            auto audio = armatools::wss::read(stream);
            info << "Format: " << audio.format << "\n"
                 << "Channels: " << audio.channels << "\n"
                 << "Sample rate: " << audio.sample_rate << " Hz\n"
                 << "Bits/sample: " << audio.bits_per_sample << "\n";
        }

        info << "Duration: " << std::fixed << std::setprecision(2)
             << audio_decoded_.duration() << " s\n";

        audio_info_label_.set_text(info.str());

        // Load into engine
        audio_engine_.load(audio_decoded_);

        // Compute mono for visualizations
        audio_mono_ = mix_to_mono(audio_decoded_);

        // Compute waveform
        audio_compute_waveform();
        audio_waveform_area_.queue_draw();

        // Compute spectrogram in background
        audio_compute_spectrogram_async();

        // Enable controls
        audio_play_btn_.set_sensitive(true);
        audio_pause_btn_.set_sensitive(false);
        audio_stop_btn_.set_sensitive(false);

        // Reset scale + time
        audio_updating_scale_ = true;
        audio_progress_.set_value(0.0);
        audio_updating_scale_ = false;
        audio_time_label_.set_text("0:00 / " + audio_format_time(audio_decoded_.duration()));
    } catch (const std::exception& e) {
        audio_info_label_.set_text(std::string("Error: ") + e.what());
        audio_play_btn_.set_sensitive(false);
    }
}

void TabAssetBrowser::audio_compute_waveform() {
    audio_waveform_envelope_.resize(kWaveformCols);
    size_t frames = audio_mono_.size();
    if (frames == 0) return;

    for (int col = 0; col < kWaveformCols; ++col) {
        size_t start = static_cast<size_t>(col) * frames / kWaveformCols;
        size_t end = static_cast<size_t>(col + 1) * frames / kWaveformCols;
        if (end > frames) end = frames;
        if (start >= end) { end = start + 1; if (end > frames) continue; }

        float mn = audio_mono_[start], mx = audio_mono_[start];
        for (size_t i = start + 1; i < end; ++i) {
            if (audio_mono_[i] < mn) mn = audio_mono_[i];
            if (audio_mono_[i] > mx) mx = audio_mono_[i];
        }
        audio_waveform_envelope_[static_cast<size_t>(col)] = {mn, mx};
    }
}

void TabAssetBrowser::audio_compute_spectrogram_async() {
    if (audio_spectrogram_computing_.load()) return;
    audio_spectrogram_computing_.store(true);

    auto mono_copy = audio_mono_;
    auto sample_rate = audio_decoded_.sample_rate;

    audio_spectrogram_thread_ = std::thread([this, mono = std::move(mono_copy), sample_rate]() {
        auto spec_data = compute_spectrogram(mono.data(), mono.size(), sample_rate);
        auto img = render_spectrogram(spec_data);

        Glib::signal_idle().connect_once([this, img = std::move(img)]() {
            if (img.width > 0 && img.height > 0) {
                auto surface = Cairo::ImageSurface::create(
                    Cairo::Surface::Format::ARGB32, img.width, img.height);
                auto* dst = surface->get_data();
                int stride = surface->get_stride();
                for (int y = 0; y < img.height; ++y) {
                    for (int x = 0; x < img.width; ++x) {
                        size_t src_idx = (static_cast<size_t>(y) * static_cast<size_t>(img.width) +
                                          static_cast<size_t>(x)) * 4;
                        uint8_t r = img.rgba[src_idx];
                        uint8_t g = img.rgba[src_idx + 1];
                        uint8_t b = img.rgba[src_idx + 2];
                        uint8_t a = img.rgba[src_idx + 3];
                        size_t dst_idx = static_cast<size_t>(y) * static_cast<size_t>(stride) +
                                         static_cast<size_t>(x) * 4;
                        dst[dst_idx + 0] = b;
                        dst[dst_idx + 1] = g;
                        dst[dst_idx + 2] = r;
                        dst[dst_idx + 3] = a;
                    }
                }
                surface->mark_dirty();
                audio_spectrogram_surface_ = surface;
                audio_spectrogram_area_.queue_draw();
            }
            audio_spectrogram_computing_.store(false);
        });
    });
}

void TabAssetBrowser::audio_draw_waveform(const Cairo::RefPtr<Cairo::Context>& cr,
                                           int width, int height) {
    // Dark background
    cr->set_source_rgb(0.07, 0.07, 0.12);
    cr->rectangle(0, 0, width, height);
    cr->fill();

    if (audio_waveform_envelope_.empty()) return;

    // Grids behind waveform
    draw_time_grid(cr, width, height, audio_decoded_.duration());
    draw_db_grid(cr, width, height);

    // Waveform bars
    double progress = audio_engine_.progress();
    double mid_y = height / 2.0;

    for (int x = 0; x < width; ++x) {
        auto col = static_cast<size_t>(
            static_cast<double>(x) / width * kWaveformCols);
        if (col >= audio_waveform_envelope_.size()) col = audio_waveform_envelope_.size() - 1;

        float mn = audio_waveform_envelope_[col].min_val;
        float mx = audio_waveform_envelope_[col].max_val;

        double y_top = mid_y - mx * mid_y;
        double y_bot = mid_y - mn * mid_y;
        if (y_bot - y_top < 1.0) { y_top = mid_y - 0.5; y_bot = mid_y + 0.5; }

        double frac = static_cast<double>(x) / width;
        if (frac <= progress) {
            cr->set_source_rgb(80.0 / 255.0, 160.0 / 255.0, 1.0);
        } else {
            cr->set_source_rgb(50.0 / 255.0, 110.0 / 255.0, 200.0 / 255.0);
        }

        cr->move_to(x + 0.5, y_top);
        cr->line_to(x + 0.5, y_bot);
        cr->stroke();
    }

    // Playback cursor
    if (audio_engine_.has_audio()) {
        double cursor_x = progress * width;
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->set_line_width(3.0);
        cr->move_to(cursor_x, 0);
        cr->line_to(cursor_x, height);
        cr->stroke();
        cr->set_line_width(1.0);
    }
}

void TabAssetBrowser::audio_draw_spectrogram(const Cairo::RefPtr<Cairo::Context>& cr,
                                              int width, int height) {
    // Dark background
    cr->set_source_rgb(0.07, 0.07, 0.12);
    cr->rectangle(0, 0, width, height);
    cr->fill();

    // Time grid behind spectrogram
    draw_time_grid(cr, width, height, audio_decoded_.duration());

    // Spectrogram image
    if (audio_spectrogram_surface_) {
        cr->save();
        double sx = static_cast<double>(width) / audio_spectrogram_surface_->get_width();
        double sy = static_cast<double>(height) / audio_spectrogram_surface_->get_height();
        cr->scale(sx, sy);
        cr->set_source(audio_spectrogram_surface_, 0, 0);
        cr->paint();
        cr->restore();
    }

    // Playback cursor
    if (audio_engine_.has_audio()) {
        double progress = audio_engine_.progress();
        double cursor_x = progress * width;
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->set_line_width(3.0);
        cr->move_to(cursor_x, 0);
        cr->line_to(cursor_x, height);
        cr->stroke();
    }
}

void TabAssetBrowser::audio_on_play() {
    audio_engine_.play();
    audio_play_btn_.set_sensitive(false);
    audio_pause_btn_.set_sensitive(true);
    audio_stop_btn_.set_sensitive(true);
    audio_start_timer();
}

void TabAssetBrowser::audio_on_pause() {
    audio_engine_.pause();
    audio_play_btn_.set_sensitive(true);
    audio_pause_btn_.set_sensitive(false);
    audio_stop_btn_.set_sensitive(true);
    audio_stop_timer();
}

void TabAssetBrowser::audio_on_stop() {
    audio_engine_.stop();
    audio_play_btn_.set_sensitive(audio_engine_.has_audio());
    audio_pause_btn_.set_sensitive(false);
    audio_stop_btn_.set_sensitive(false);
    audio_stop_timer();

    audio_updating_scale_ = true;
    audio_progress_.set_value(0.0);
    audio_updating_scale_ = false;
    audio_time_label_.set_text("0:00 / " + audio_format_time(audio_decoded_.duration()));
    audio_waveform_area_.queue_draw();
    audio_spectrogram_area_.queue_draw();
}

void TabAssetBrowser::audio_on_seek(double fraction) {
    audio_engine_.seek(fraction);
    audio_updating_scale_ = true;
    audio_progress_.set_value(fraction);
    audio_updating_scale_ = false;

    double pos_sec = fraction * audio_decoded_.duration();
    audio_time_label_.set_text(audio_format_time(pos_sec) + " / " +
                                audio_format_time(audio_decoded_.duration()));
    audio_waveform_area_.queue_draw();
    audio_spectrogram_area_.queue_draw();
}

bool TabAssetBrowser::audio_on_timer() {
    double progress = audio_engine_.progress();

    audio_updating_scale_ = true;
    audio_progress_.set_value(progress);
    audio_updating_scale_ = false;

    double pos_sec = progress * audio_decoded_.duration();
    audio_time_label_.set_text(audio_format_time(pos_sec) + " / " +
                                audio_format_time(audio_decoded_.duration()));

    audio_waveform_area_.queue_draw();
    audio_spectrogram_area_.queue_draw();

    // Check if playback finished
    if (audio_engine_.state() == PlayState::Stopped) {
        audio_play_btn_.set_sensitive(true);
        audio_pause_btn_.set_sensitive(false);
        audio_stop_btn_.set_sensitive(false);
        return false; // stop timer
    }

    return true; // continue
}

void TabAssetBrowser::audio_start_timer() {
    audio_stop_timer();
    // ~30 fps
    audio_timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &TabAssetBrowser::audio_on_timer), 33);
}

void TabAssetBrowser::audio_stop_timer() {
    audio_timer_.disconnect();
}

void TabAssetBrowser::audio_stop_all() {
    audio_stop_timer();
    audio_engine_.stop();
    if (audio_spectrogram_thread_.joinable())
        audio_spectrogram_thread_.join();
}

std::string TabAssetBrowser::audio_format_time(double seconds) {
    if (seconds < 0) seconds = 0;
    int ms = static_cast<int>(seconds * 1000) % 1000;
    int total = static_cast<int>(seconds);
    int sec = total % 60;
    int min = (total / 60) % 60;
    int hr = total / 3600;
    std::ostringstream ss;
    if (hr > 0)
        ss << hr << ":" << std::setw(2) << std::setfill('0') << min << ":"
           << std::setw(2) << std::setfill('0') << sec << "."
           << std::setw(3) << std::setfill('0') << ms;
    else
        ss << min << ":" << std::setw(2) << std::setfill('0') << sec << "."
           << std::setw(3) << std::setfill('0') << ms;
    return ss.str();
}
