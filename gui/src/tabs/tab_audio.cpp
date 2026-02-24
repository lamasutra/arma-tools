#include "tab_audio.h"
#include "audio_draw_util.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/ogg.h>
#include <armatools/wss.h>
#include <armatools/pboindex.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

TabAudio::TabAudio() : Gtk::Box(Gtk::Orientation::VERTICAL, 4) {
    auto make_icon_button = [](Gtk::Button& b, const char* icon, const char* tip) {
        b.set_label("");
        b.set_icon_name(icon);
        b.set_has_frame(false);
        b.set_tooltip_text(tip);
    };
    make_icon_button(browse_button_, "document-open-symbolic", "Browse audio file");
    make_icon_button(search_button_, "system-search-symbolic", "Search indexed PBOs for audio");

    set_margin(8);

    // --- Path row ---
    pbo_label_.set_margin_end(2);
    path_box_.append(pbo_label_);
    path_box_.append(switch_box_);
    switch_box_.set_valign(Gtk::Align::CENTER);
    switch_box_.set_vexpand(false);
    switch_box_.append(pbo_switch_);
    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("Audio file (.ogg, .wss, .wav)...");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    search_button_.set_visible(false);
    path_box_.append(search_button_);
    search_spinner_.set_visible(false);
    path_box_.append(search_spinner_);
    search_count_label_.set_visible(false);
    path_box_.append(search_count_label_);
    append(path_box_);

    // --- Search results (PBO mode only) ---
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    append(search_scroll_);

    // --- Info section: scrollable label + Save WAV button ---
    info_label_.set_halign(Gtk::Align::START);
    info_label_.set_valign(Gtk::Align::START);
    info_label_.set_wrap(true);
    info_label_.set_selectable(true);
    info_scroll_.set_child(info_label_);
    info_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    info_scroll_.set_hexpand(true);
    info_scroll_.set_vexpand(true);
    save_wav_button_.set_valign(Gtk::Align::START);
    save_wav_button_.set_visible(false);
    info_box_.append(info_scroll_);
    info_box_.append(save_wav_button_);
    info_box_.set_hexpand(true);
    info_box_.set_vexpand(true);

    // --- Waveform ---
    waveform_area_.set_hexpand(true);
    waveform_area_.set_vexpand(true);
    waveform_area_.set_draw_func(sigc::mem_fun(*this, &TabAudio::draw_waveform));
    auto waveform_click = Gtk::GestureClick::create();
    waveform_click->signal_released().connect(
        [this](int /*n_press*/, double x, double /*y*/) {
            int w = waveform_area_.get_width();
            if (w > 0) on_seek(x / w);
        });
    waveform_area_.add_controller(waveform_click);

    // --- Spectrogram ---
    spectrogram_area_.set_hexpand(true);
    spectrogram_area_.set_vexpand(true);
    spectrogram_area_.set_draw_func(sigc::mem_fun(*this, &TabAudio::draw_spectrogram));
    auto spectro_click = Gtk::GestureClick::create();
    spectro_click->signal_released().connect(
        [this](int /*n_press*/, double x, double /*y*/) {
            int w = spectrogram_area_.get_width();
            if (w > 0) on_seek(x / w);
        });
    spectrogram_area_.add_controller(spectro_click);

    // --- Resizable panes: info | waveform | spectrogram ---
    // paned_top_: info_box_ (top) | paned_bottom_ (bottom)
    // paned_bottom_: waveform (top) | spectrogram (bottom)
    paned_bottom_.set_start_child(waveform_area_);
    paned_bottom_.set_end_child(spectrogram_area_);
    paned_bottom_.set_resize_start_child(true);
    paned_bottom_.set_resize_end_child(true);
    paned_bottom_.set_shrink_start_child(false);
    paned_bottom_.set_shrink_end_child(false);
    paned_bottom_.set_vexpand(true);
    paned_bottom_.add_css_class("audio-split");

    paned_top_.set_start_child(info_box_);
    paned_top_.set_end_child(paned_bottom_);
    paned_top_.set_resize_start_child(false);
    paned_top_.set_resize_end_child(true);
    paned_top_.set_shrink_start_child(false);
    paned_top_.set_shrink_end_child(false);
    paned_top_.set_vexpand(true);
    paned_top_.set_position(100);
    paned_top_.add_css_class("audio-split");
    append(paned_top_);

    // Style comes from global resource CSS.
    paned_bottom_.set_wide_handle(true);
    paned_top_.set_wide_handle(true);

    // --- Progress scale (above controls, full width) ---
    progress_scale_.set_range(0.0, 1.0);
    progress_scale_.set_increments(0.001, 0.01);
    progress_scale_.set_draw_value(false);
    progress_scale_.set_hexpand(true);
    progress_scale_.signal_value_changed().connect([this]() {
        if (!updating_scale_) {
            on_seek(progress_scale_.get_value());
        }
    });
    append(progress_scale_);

    // --- Controls row: [Play][Pause][Stop] ---- time (right) ---
    controls_box_.set_valign(Gtk::Align::CENTER);
    controls_box_.append(play_button_);
    controls_box_.append(pause_button_);
    controls_box_.append(stop_button_);
    time_label_.set_halign(Gtk::Align::END);
    time_label_.set_hexpand(true);
    controls_box_.append(time_label_);
    append(controls_box_);

    // Initial button state
    pause_button_.set_sensitive(false);
    stop_button_.set_sensitive(false);
    play_button_.set_sensitive(false);

    // --- Signals ---
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_audio(path_entry_.get_text());
    });
    play_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_play));
    pause_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_pause));
    stop_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_stop));
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabAudio::on_pbo_mode_changed));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabAudio::on_search_result_selected));
    save_wav_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabAudio::on_save_wav));
}

TabAudio::~TabAudio() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    stop_timer();
    engine_.stop();
    if (spectrogram_thread_.joinable()) spectrogram_thread_.join();
    cleanup_temp_file();
}

void TabAudio::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabAudio::set_config(Config* cfg) {
    cfg_ = cfg;
    db_.reset();

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
    });
}

void TabAudio::on_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Audio files");
    filter->add_pattern("*.ogg");
    filter->add_pattern("*.wss");
    filter->add_pattern("*.wav");
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
                    load_audio(file->get_path());
                }
            } catch (...) {}
        });
}

void TabAudio::load_audio(const std::string& path) {
    if (path.empty()) return;

    engine_.stop();
    stop_timer();

    // Wait for any previous spectrogram computation.
    if (spectrogram_thread_.joinable()) spectrogram_thread_.join();

    // Clear state
    waveform_envelope_.clear();
    spectrogram_surface_.reset();
    save_wav_button_.set_visible(false);
    current_file_path_.clear();
    waveform_area_.queue_draw();
    spectrogram_area_.queue_draw();

    try {
        decoded_audio_ = decode_file(path);
        current_file_path_ = path;
        auto ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Build comprehensive info from the original file.
        std::string info = "File: " + fs::path(path).filename().string() + "\n";
        if (ext == ".ogg")
            info += build_ogg_info(path);
        else if (ext == ".wss" || ext == ".wav")
            info += build_wss_info(path);

        on_loaded(info);
    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, std::string("Audio decode failed: ") + e.what());
        play_button_.set_sensitive(false);
    }
}

void TabAudio::load_audio_from_memory(const uint8_t* data, size_t size,
                                       const std::string& ext,
                                       const std::string& display_name) {
    engine_.stop();
    stop_timer();

    if (spectrogram_thread_.joinable()) spectrogram_thread_.join();

    waveform_envelope_.clear();
    spectrogram_surface_.reset();
    save_wav_button_.set_visible(false);
    current_file_path_.clear();
    waveform_area_.queue_draw();
    spectrogram_area_.queue_draw();

    try {
        decoded_audio_ = decode_memory(data, size, ext);

        std::string lower_ext = ext;
        std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), ::tolower);

        std::string info;
        if (lower_ext == ".ogg")
            info = build_ogg_info_memory(data, size);
        else if (lower_ext == ".wss" || lower_ext == ".wav")
            info = build_wss_info_memory(data, size);
        else
            info = "File: " + display_name;

        // Prepend filename.
        info = "File: " + display_name + "\n" + info;
        on_loaded(info);
    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        app_log(LogLevel::Error, std::string("Audio decode failed: ") + e.what());
        play_button_.set_sensitive(false);
    }
}

void TabAudio::on_loaded(const std::string& info_text) {
    info_label_.set_text(info_text);

    // Load into engine
    engine_.load(decoded_audio_);

    // Compute mono for visualizations
    mono_data_ = mix_to_mono(decoded_audio_);

    // Compute waveform envelope
    compute_waveform_envelope();
    waveform_area_.queue_draw();

    // Compute spectrogram in background
    compute_spectrogram_async();

    // Enable buttons
    play_button_.set_sensitive(true);
    pause_button_.set_sensitive(false);
    stop_button_.set_sensitive(false);
    save_wav_button_.set_visible(true);

    // Reset scale + time
    updating_scale_ = true;
    progress_scale_.set_value(0.0);
    updating_scale_ = false;
    time_label_.set_text("0:00 / " + format_time(decoded_audio_.duration()));
}

void TabAudio::compute_waveform_envelope() {
    waveform_envelope_.resize(kWaveformCols);
    size_t frames = mono_data_.size();
    if (frames == 0) return;

    for (int col = 0; col < kWaveformCols; ++col) {
        size_t start = static_cast<size_t>(col) * frames / kWaveformCols;
        size_t end = static_cast<size_t>(col + 1) * frames / kWaveformCols;
        if (end > frames) end = frames;
        if (start >= end) { end = start + 1; if (end > frames) continue; }

        float mn = mono_data_[start], mx = mono_data_[start];
        for (size_t i = start + 1; i < end; ++i) {
            if (mono_data_[i] < mn) mn = mono_data_[i];
            if (mono_data_[i] > mx) mx = mono_data_[i];
        }
        waveform_envelope_[static_cast<size_t>(col)] = {mn, mx};
    }
}

void TabAudio::compute_spectrogram_async() {
    if (spectrogram_computing_.load()) return;
    spectrogram_computing_.store(true);

    // Copy data for thread safety.
    auto mono_copy = mono_data_;
    auto sample_rate = decoded_audio_.sample_rate;

    spectrogram_thread_ = std::thread([this, mono = std::move(mono_copy), sample_rate]() {
        auto data = compute_spectrogram(mono.data(), mono.size(), sample_rate);
        auto img = render_spectrogram(data);

        Glib::signal_idle().connect_once([this, img = std::move(img)]() {
            if (img.width > 0 && img.height > 0) {
                // Cairo expects ARGB32 premultiplied. We have RGBA.
                // Convert RGBA → ARGB32 (Cairo native format).
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
                        // ARGB32 in native endian (on little-endian: BGRA bytes)
                        size_t dst_idx = static_cast<size_t>(y) * static_cast<size_t>(stride) +
                                         static_cast<size_t>(x) * 4;
                        dst[dst_idx + 0] = b;
                        dst[dst_idx + 1] = g;
                        dst[dst_idx + 2] = r;
                        dst[dst_idx + 3] = a;
                    }
                }
                surface->mark_dirty();
                spectrogram_surface_ = surface;
                spectrogram_area_.queue_draw();
            }
            spectrogram_computing_.store(false);
        });
    });
}

void TabAudio::draw_waveform(const Cairo::RefPtr<Cairo::Context>& cr,
                              int width, int height) {
    // Dark background
    cr->set_source_rgb(0.07, 0.07, 0.12);
    cr->rectangle(0, 0, width, height);
    cr->fill();

    if (waveform_envelope_.empty()) return;

    // Grids behind waveform
    draw_time_grid(cr, width, height, decoded_audio_.duration());
    draw_db_grid(cr, width, height);

    // Waveform bars
    double progress = engine_.progress();
    double mid_y = height / 2.0;

    for (int x = 0; x < width; ++x) {
        auto col = static_cast<size_t>(
            static_cast<double>(x) / width * kWaveformCols);
        if (col >= waveform_envelope_.size()) col = waveform_envelope_.size() - 1;

        float mn = waveform_envelope_[col].min_val;
        float mx = waveform_envelope_[col].max_val;

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

    // Playback cursor: 3px white (always on top)
    if (engine_.has_audio()) {
        double cursor_x = progress * width;
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->set_line_width(3.0);
        cr->move_to(cursor_x, 0);
        cr->line_to(cursor_x, height);
        cr->stroke();
        cr->set_line_width(1.0);
    }
}

void TabAudio::draw_spectrogram(const Cairo::RefPtr<Cairo::Context>& cr,
                                 int width, int height) {
    // Dark background
    cr->set_source_rgb(0.07, 0.07, 0.12);
    cr->rectangle(0, 0, width, height);
    cr->fill();

    // Time grid behind spectrogram
    draw_time_grid(cr, width, height, decoded_audio_.duration());

    // Spectrogram image on top of grid
    if (spectrogram_surface_) {
        cr->save();
        double sx = static_cast<double>(width) / spectrogram_surface_->get_width();
        double sy = static_cast<double>(height) / spectrogram_surface_->get_height();
        cr->scale(sx, sy);
        cr->set_source(spectrogram_surface_, 0, 0);
        cr->paint();
        cr->restore();
    }

    // Playback cursor
    if (engine_.has_audio()) {
        double progress = engine_.progress();
        double cursor_x = progress * width;
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->set_line_width(3.0);
        cr->move_to(cursor_x, 0);
        cr->line_to(cursor_x, height);
        cr->stroke();
    }
}

void TabAudio::on_play() {
    engine_.play();
    play_button_.set_sensitive(false);
    pause_button_.set_sensitive(true);
    stop_button_.set_sensitive(true);
    start_timer();
}

void TabAudio::on_pause() {
    engine_.pause();
    play_button_.set_sensitive(true);
    pause_button_.set_sensitive(false);
    stop_button_.set_sensitive(true);
    stop_timer();
}

void TabAudio::on_stop() {
    engine_.stop();
    play_button_.set_sensitive(engine_.has_audio());
    pause_button_.set_sensitive(false);
    stop_button_.set_sensitive(false);
    stop_timer();

    updating_scale_ = true;
    progress_scale_.set_value(0.0);
    updating_scale_ = false;
    time_label_.set_text("0:00 / " + format_time(decoded_audio_.duration()));
    waveform_area_.queue_draw();
    spectrogram_area_.queue_draw();
}

void TabAudio::on_seek(double fraction) {
    engine_.seek(fraction);
    updating_scale_ = true;
    progress_scale_.set_value(fraction);
    updating_scale_ = false;

    double pos_sec = fraction * decoded_audio_.duration();
    time_label_.set_text(format_time(pos_sec) + " / " +
                         format_time(decoded_audio_.duration()));
    waveform_area_.queue_draw();
    spectrogram_area_.queue_draw();
}

bool TabAudio::on_timer() {
    double progress = engine_.progress();

    updating_scale_ = true;
    progress_scale_.set_value(progress);
    updating_scale_ = false;

    double pos_sec = progress * decoded_audio_.duration();
    time_label_.set_text(format_time(pos_sec) + " / " +
                         format_time(decoded_audio_.duration()));

    waveform_area_.queue_draw();
    spectrogram_area_.queue_draw();

    // Check if playback finished
    if (engine_.state() == PlayState::Stopped) {
        play_button_.set_sensitive(true);
        pause_button_.set_sensitive(false);
        stop_button_.set_sensitive(false);
        return false; // stop timer
    }

    return true; // continue
}

void TabAudio::start_timer() {
    stop_timer();
    // ~30 fps
    timer_connection_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &TabAudio::on_timer), 33);
}

void TabAudio::stop_timer() {
    timer_connection_.disconnect();
}

std::string TabAudio::format_time(double seconds) {
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

void TabAudio::on_save_wav() {
    if (decoded_audio_.samples.empty()) return;

    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("WAV files");
    filter->add_pattern("*.wav");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    if (!current_file_path_.empty()) {
        auto stem = fs::path(current_file_path_).stem().string();
        dialog->set_initial_name(stem + ".wav");
    }

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (!file) return;

                auto out_path = file->get_path();
                auto& audio = decoded_audio_;

                auto channels = static_cast<uint16_t>(audio.channels);
                uint32_t sample_rate = audio.sample_rate;
                uint16_t bits_per_sample = 16;
                uint16_t block_align = channels * (bits_per_sample / 8);
                uint32_t byte_rate = sample_rate * block_align;
                auto data_size = static_cast<uint32_t>(audio.samples.size() * 2);
                uint32_t file_size = 36 + data_size;

                std::ofstream out(out_path, std::ios::binary);
                if (!out.is_open()) {
                    info_label_.set_text("Error: Cannot create file");
                    return;
                }

                auto write_u16 = [&](uint16_t v) {
                    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF),
                                    static_cast<uint8_t>((v >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char*>(b), 2);
                };
                auto write_u32 = [&](uint32_t v) {
                    uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF),
                                    static_cast<uint8_t>((v >> 8) & 0xFF),
                                    static_cast<uint8_t>((v >> 16) & 0xFF),
                                    static_cast<uint8_t>((v >> 24) & 0xFF)};
                    out.write(reinterpret_cast<const char*>(b), 4);
                };

                out.write("RIFF", 4);
                write_u32(file_size);
                out.write("WAVE", 4);
                out.write("fmt ", 4);
                write_u32(16);
                write_u16(1);
                write_u16(channels);
                write_u32(sample_rate);
                write_u32(byte_rate);
                write_u16(block_align);
                write_u16(bits_per_sample);
                out.write("data", 4);
                write_u32(data_size);

                for (int16_t sample : audio.samples) {
                    uint8_t b[2] = {static_cast<uint8_t>(sample & 0xFF),
                                    static_cast<uint8_t>((sample >> 8) & 0xFF)};
                    out.write(reinterpret_cast<const char*>(b), 2);
                }

                out.close();
                app_log(LogLevel::Info, "Saved WAV: " + out_path);

            } catch (const std::exception& e) {
                info_label_.set_text(std::string("Save error: ") + e.what());
                app_log(LogLevel::Error, std::string("WAV save failed: ") + e.what());
            } catch (...) {}
        });
}

// ---------------------------------------------------------------------------
// File info builders (comprehensive, matching Go version)
// ---------------------------------------------------------------------------

std::string TabAudio::build_ogg_info(const std::string& path) {
    std::ostringstream info;
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return "Format: OGG Vorbis";
        auto hdr = armatools::ogg::read_header(f);

        info << "Format: OGG Vorbis\n";
        info << "Sample rate: " << hdr.sample_rate << " Hz\n";
        info << "Channels: " << hdr.channels << "\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
        if (!hdr.encoder.empty())
            info << "\nEncoder: " << hdr.encoder;
        info << "\nCodebooks: " << hdr.codebooks.size();
        info << "\nFloor type: " << hdr.floor_type;
        for (const auto& c : hdr.comments)
            info << "\n" << c;

        // Warnings
        if (armatools::ogg::is_pre_one_encoder(hdr.encoder))
            info << "\nWARNING: pre-1.0 encoder (" << hdr.encoder << ")";
        if (hdr.floor_type == 0 && !hdr.codebooks.empty())
            info << "\nWARNING: uses floor type 0";
        for (size_t i = 0; i < hdr.codebooks.size(); ++i) {
            const auto& cb = hdr.codebooks[i];
            if (cb.lookup_type == 1 &&
                armatools::ogg::lookup1_values_precision_risk(cb.entries, cb.dimensions))
                info << "\nWARNING: codebook " << i
                     << ": lookup1Values precision risk (entries="
                     << cb.entries << ", dims=" << cb.dimensions << ")";
        }
    } catch (...) {
        info << "Format: OGG Vorbis\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
    }
    return info.str();
}

std::string TabAudio::build_ogg_info_memory(const uint8_t* data, size_t size) {
    std::ostringstream info;
    try {
        std::string str(reinterpret_cast<const char*>(data), size);
        std::istringstream stream(str);
        auto hdr = armatools::ogg::read_header(stream);

        info << "Format: OGG Vorbis\n";
        info << "Sample rate: " << hdr.sample_rate << " Hz\n";
        info << "Channels: " << hdr.channels << "\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
        if (!hdr.encoder.empty())
            info << "\nEncoder: " << hdr.encoder;
        info << "\nCodebooks: " << hdr.codebooks.size();
        info << "\nFloor type: " << hdr.floor_type;
        for (const auto& c : hdr.comments)
            info << "\n" << c;

        if (armatools::ogg::is_pre_one_encoder(hdr.encoder))
            info << "\nWARNING: pre-1.0 encoder (" << hdr.encoder << ")";
        if (hdr.floor_type == 0 && !hdr.codebooks.empty())
            info << "\nWARNING: uses floor type 0";
        for (size_t i = 0; i < hdr.codebooks.size(); ++i) {
            const auto& cb = hdr.codebooks[i];
            if (cb.lookup_type == 1 &&
                armatools::ogg::lookup1_values_precision_risk(cb.entries, cb.dimensions))
                info << "\nWARNING: codebook " << i
                     << ": lookup1Values precision risk (entries="
                     << cb.entries << ", dims=" << cb.dimensions << ")";
        }
    } catch (...) {
        info << "Format: OGG Vorbis\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
    }
    return info.str();
}

std::string TabAudio::build_wss_info(const std::string& path) {
    std::ostringstream info;
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return "Format: WSS/WAV";
        auto audio = armatools::wss::read(f);

        info << "Format: " << audio.format << " " << audio.bits_per_sample << "-bit\n";
        info << "Sample rate: " << audio.sample_rate << " Hz\n";
        info << "Channels: " << audio.channels << "\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << audio.duration << " s";
    } catch (...) {
        info << "Format: WSS/WAV\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
    }
    return info.str();
}

std::string TabAudio::build_wss_info_memory(const uint8_t* data, size_t size) {
    std::ostringstream info;
    try {
        std::string str(reinterpret_cast<const char*>(data), size);
        std::istringstream stream(str);
        auto audio = armatools::wss::read(stream);

        info << "Format: " << audio.format << " " << audio.bits_per_sample << "-bit\n";
        info << "Sample rate: " << audio.sample_rate << " Hz\n";
        info << "Channels: " << audio.channels << "\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << audio.duration << " s";
    } catch (...) {
        info << "Format: WSS/WAV\n";
        info << "Duration: " << std::fixed << std::setprecision(2)
             << decoded_audio_.duration() << " s";
    }
    return info.str();
}

void TabAudio::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search in PBO...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        search_scroll_.set_visible(false);
        search_count_label_.set_visible(false);
    } else {
        path_entry_.set_placeholder_text("Audio file (.ogg, .wss, .wav)...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        search_scroll_.set_visible(false);
        search_count_label_.set_visible(false);
    }
}

void TabAudio::on_search() {
    auto query = std::string(path_entry_.get_text());
    if (query.empty()) return;

    if (!db_) {
        search_count_label_.set_text("No PBO index");
        search_count_label_.set_visible(true);
        return;
    }

    // Show spinner
    search_spinner_.set_visible(true);
    search_spinner_.set_spinning(true);
    search_count_label_.set_visible(false);

    // Clear previous results
    while (auto* row = search_results_.get_row_at_index(0))
        search_results_.remove(*row);
    search_results_data_.clear();

    bool has_dot = query.find('.') != std::string::npos;
    if (has_dot) {
        // User specified an extension — search as-is.
        search_results_data_ = db_->find_files("*" + query + "*");
    } else {
        // No extension — search all audio extensions.
        auto ogg_results = db_->find_files("*" + query + "*.ogg");
        auto wss_results = db_->find_files("*" + query + "*.wss");
        auto wav_results = db_->find_files("*" + query + "*.wav");

        search_results_data_ = ogg_results;
        search_results_data_.insert(search_results_data_.end(),
                                    wss_results.begin(), wss_results.end());
        search_results_data_.insert(search_results_data_.end(),
                                    wav_results.begin(), wav_results.end());
    }

    for (const auto& r : search_results_data_) {
        auto display = r.prefix + "/" + r.file_path;
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        search_results_.append(*label);
    }

    // Hide spinner, show count
    search_spinner_.set_spinning(false);
    search_spinner_.set_visible(false);
    search_count_label_.set_text(std::to_string(search_results_data_.size()) + " files");
    search_count_label_.set_visible(true);

    // Show/hide results list
    search_scroll_.set_visible(!search_results_data_.empty());
}

void TabAudio::on_search_result_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto idx = static_cast<size_t>(row->get_index());
    if (idx >= search_results_data_.size()) return;
    load_from_pbo(search_results_data_[idx]);
}

void TabAudio::load_from_pbo(const armatools::pboindex::FindResult& r) {
    cleanup_temp_file();

    auto data = extract_from_pbo(r.pbo_path, r.file_path);
    if (data.empty()) {
        info_label_.set_text("Error: Could not extract from PBO");
        return;
    }

    auto ext = fs::path(r.file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto display_name = fs::path(r.file_path).filename().string();

    load_audio_from_memory(data.data(), data.size(), ext, display_name);

    // Keep the PBO path for save dialog suggestions.
    current_file_path_ = r.file_path;

    app_log(LogLevel::Info, "Loaded audio from PBO: " + r.prefix + "/" + r.file_path);
}

void TabAudio::cleanup_temp_file() {
    if (!temp_audio_path_.empty()) {
        std::error_code ec;
        fs::remove(temp_audio_path_, ec);
        temp_audio_path_.clear();
    }
}
