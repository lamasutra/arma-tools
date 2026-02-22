#pragma once

#include "audio_decode.h"
#include "audio_engine.h"
#include "config.h"
#include "pbo_index_service.h"
#include "spectrogram.h"

#include <gtkmm.h>
#include <armatools/pboindex.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class TabAudio : public Gtk::Box {
public:
    TabAudio();
    ~TabAudio() override;
    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;

    // --- Audio data ---
    AudioEngine engine_;
    NormalizedAudio decoded_audio_;
    std::vector<float> mono_data_;

    // --- Waveform envelope (precomputed) ---
    static constexpr int kWaveformCols = 2000;
    struct WaveformCol { float min_val; float max_val; };
    std::vector<WaveformCol> waveform_envelope_;

    // --- Spectrogram ---
    Cairo::RefPtr<Cairo::ImageSurface> spectrogram_surface_;
    std::atomic<bool> spectrogram_computing_{false};

    // --- Path row ---
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};

    // --- PBO mode ---
    Gtk::Box switch_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Button search_button_{"Search"};
    Gtk::Spinner search_spinner_;
    Gtk::Label search_count_label_;
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<armatools::pboindex::FindResult> search_results_data_;
    bool pbo_mode_ = false;

    // --- Info section (label + Save WAV) ---
    Gtk::Box info_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::ScrolledWindow info_scroll_;
    Gtk::Label info_label_;
    Gtk::Button save_wav_button_{"Save WAV"};

    // --- Resizable panes: info | waveform | spectrogram ---
    Gtk::Paned paned_top_{Gtk::Orientation::VERTICAL};
    Gtk::Paned paned_bottom_{Gtk::Orientation::VERTICAL};

    // --- Waveform ---
    Gtk::DrawingArea waveform_area_;

    // --- Spectrogram area ---
    Gtk::DrawingArea spectrogram_area_;

    // --- Transport (non-resizable) ---
    Gtk::Scale progress_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label time_label_{"0:00.000 / 0:00.000"};
    Gtk::Box controls_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button play_button_{"Play"};
    Gtk::Button pause_button_{"Pause"};
    Gtk::Button stop_button_{"Stop"};

    // --- Timer ---
    sigc::connection timer_connection_;
    bool updating_scale_ = false;

    // --- Temp file for PBO extraction ---
    std::string temp_audio_path_;
    std::string current_file_path_;

    // --- Spectrogram background thread ---
    std::thread spectrogram_thread_;

    // --- Actions ---
    void on_browse();
    void load_audio(const std::string& path);
    void load_audio_from_memory(const uint8_t* data, size_t size,
                                const std::string& ext,
                                const std::string& display_name);
    void on_loaded(const std::string& info_text);

    void on_play();
    void on_pause();
    void on_stop();
    void on_seek(double fraction);
    void on_save_wav();

    bool on_timer();
    void start_timer();
    void stop_timer();

    void compute_waveform_envelope();
    void compute_spectrogram_async();

    void draw_waveform(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void draw_spectrogram(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    void on_pbo_mode_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
    void load_from_pbo(const armatools::pboindex::FindResult& r);
    void cleanup_temp_file();

    static std::string format_time(double seconds);

    std::string build_ogg_info(const std::string& path);
    std::string build_ogg_info_memory(const uint8_t* data, size_t size);
    std::string build_wss_info(const std::string& path);
    std::string build_wss_info_memory(const uint8_t* data, size_t size);
};
