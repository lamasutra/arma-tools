#pragma once

#include "audio_decode.h"
#include "audio_engine.h"
#include "config.h"
#include "model_view_panel.h"
#include "pbo_index_service.h"
#include "render_domain/rvmat_preview_widget.h"
#include "spectrogram.h"

#include <armatools/pboindex.h>

#include <atomic>
#include <array>
#include <gtkmm.h>
#include <memory>
#include <optional>
#include <sigc++/connection.h>
#include <string>
#include <thread>
#include <vector>

class TabAssetBrowser : public Gtk::Paned {
public:
    TabAssetBrowser();
    ~TabAssetBrowser() override;
    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);
    void set_model_loader_service(const std::shared_ptr<P3dModelLoaderService>& service);
    void set_texture_loader_service(const std::shared_ptr<TexturesLoaderService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;
    std::string current_path_;
    std::vector<armatools::pboindex::FindResult> search_results_;
    std::vector<armatools::pboindex::DirEntry> current_entries_;
    std::string active_search_pattern_;
    bool browse_is_search_ = false;
    bool has_more_results_ = false;
    bool loading_more_results_ = false;
    size_t current_offset_ = 0;
    static constexpr size_t kPageSize = 500;
    sigc::connection scroll_value_conn_;

    // --- Audio state ---
    AudioEngine audio_engine_;
    NormalizedAudio audio_decoded_;
    std::vector<float> audio_mono_;

    static constexpr int kWaveformCols = 2000;
    struct WaveformCol { float min_val; float max_val; };
    std::vector<WaveformCol> audio_waveform_envelope_;

    Cairo::RefPtr<Cairo::ImageSurface> audio_spectrogram_surface_;
    std::atomic<bool> audio_spectrogram_computing_{false};
    std::thread audio_spectrogram_thread_;

    // Left panel
    Gtk::Box left_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box toolbar_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button build_button_{"Build DB"};
    Gtk::Button update_button_{"Update DB"};
    Gtk::Button stats_button_{"Stats"};
    Gtk::Box source_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label source_label_{"Source:"};
    Gtk::ComboBoxText source_combo_;
    std::string current_source_; // "" = all, otherwise "arma3", "ofp", etc.
    bool source_combo_updating_ = false;
    std::jthread nav_thread_;
    std::atomic<unsigned> nav_generation_{0};
    Gtk::Box search_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry search_entry_;
    Gtk::Button search_button_{"Search"};
    Gtk::Label breadcrumb_label_{"/"};
    Gtk::ScrolledWindow list_scroll_;
    Gtk::ListBox dir_list_;

    // Right panel
    Gtk::Box right_box_{Gtk::Orientation::VERTICAL, 8};
    Gtk::Label file_info_label_;
    Gtk::ScrolledWindow info_scroll_;
    Gtk::TextView info_view_;
    Gtk::ScrolledWindow preview_scroll_;
    Gtk::Picture preview_picture_;
    Gtk::Paned rvmat_paned_{Gtk::Orientation::HORIZONTAL};
    Gtk::ScrolledWindow rvmat_info_scroll_;
    Gtk::TextView rvmat_info_view_;
    Gtk::Box rvmat_preview_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box rvmat_preview_toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::ToggleButton rvmat_shape_sphere_;
    Gtk::ToggleButton rvmat_shape_tile_;
    bool rvmat_shape_updating_ = false;
    Gtk::ToggleButton rvmat_view_final_;
    Gtk::ToggleButton rvmat_view_albedo_;
    Gtk::ToggleButton rvmat_view_normal_;
    Gtk::ToggleButton rvmat_view_spec_;
    Gtk::ToggleButton rvmat_view_ao_;
    bool rvmat_view_updating_ = false;
    Gtk::ToggleButton rvmat_text_parsed_;
    Gtk::ToggleButton rvmat_text_source_;
    bool rvmat_text_updating_ = false;
    std::string rvmat_text_parsed_cache_;
    std::string rvmat_text_source_cache_;
    render_domain::RvmatPreviewWidget rvmat_preview_;
    ModelViewPanel model_panel_;

    // --- Audio panel (embedded player) ---
    Gtk::Box audio_panel_{Gtk::Orientation::VERTICAL, 4};
    Gtk::ScrolledWindow audio_info_scroll_;
    Gtk::Label audio_info_label_;
    Gtk::Paned audio_paned_{Gtk::Orientation::VERTICAL};
    Gtk::DrawingArea audio_waveform_area_;
    Gtk::DrawingArea audio_spectrogram_area_;
    Gtk::Scale audio_progress_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box audio_controls_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button audio_play_btn_{"Play"};
    Gtk::Button audio_pause_btn_{"Pause"};
    Gtk::Button audio_stop_btn_{"Stop"};
    Gtk::Label audio_time_label_{"0:00.000 / 0:00.000"};
    sigc::connection audio_timer_;
    bool audio_updating_scale_ = false;

    // Extract row
    Gtk::Box extract_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button extract_button_{"Extract File"};
    Gtk::Button extract_drive_button_{"Extract to Drive"};
    Gtk::Label status_label_;

    void on_build_db();
    void on_update_db();
    void on_stats();
    void on_search();
    void on_source_changed();
    void refresh_source_combo();
    void on_row_activated(Gtk::ListBoxRow* row);
    void on_row_selected(Gtk::ListBoxRow* row);
    void on_extract();
    void on_extract_to_drive();

    void open_db();
    void navigate(const std::string& path);
    void populate_list(const std::vector<armatools::pboindex::DirEntry>& entries);
    void show_search_results(const std::vector<armatools::pboindex::FindResult>& results);
    void append_search_results_page(const std::vector<armatools::pboindex::FindResult>& results,
                                    bool reset);
    void append_directory_page(const std::vector<armatools::pboindex::DirEntry>& entries,
                               bool reset);
    void load_next_search_page(unsigned gen, bool reset);
    void load_next_directory_page(unsigned gen, bool reset);
    void try_load_next_page();
    void show_file_info(const armatools::pboindex::FindResult& file);
    void preview_p3d(const armatools::pboindex::FindResult& file);
    void preview_paa(const armatools::pboindex::FindResult& file);
    void preview_audio(const armatools::pboindex::FindResult& file);
    void preview_config(const armatools::pboindex::FindResult& file);
    void preview_rvmat(const armatools::pboindex::FindResult& file);
    void preview_jpg(const armatools::pboindex::FindResult& file);
    void preview_text(const armatools::pboindex::FindResult& file);

    struct DecodedTexture {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels;
    };
    std::optional<DecodedTexture> load_preview_texture_asset(
        const armatools::pboindex::FindResult& context_file,
        const std::string& texture_path);

    // Audio player methods
    void audio_load_from_memory(const uint8_t* data, size_t size,
                                const std::string& ext,
                                const std::string& display_name);
    void audio_on_play();
    void audio_on_pause();
    void audio_on_stop();
    void audio_on_seek(double fraction);
    bool audio_on_timer();
    void audio_start_timer();
    void audio_stop_timer();
    void audio_compute_waveform();
    void audio_compute_spectrogram_async();
    void audio_draw_waveform(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void audio_draw_spectrogram(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void audio_stop_all();

    // Helper to get the currently selected file (if any)
    bool get_selected_file(armatools::pboindex::FindResult& out);

    static std::string icon_for_extension(const std::string& ext);
    static std::string audio_format_time(double seconds);
};
