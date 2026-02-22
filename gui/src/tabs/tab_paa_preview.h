#pragma once

#include "pbo_index_service.h"

#include <gtkmm.h>
#include <armatools/pboindex.h>

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct Config;

class TabPaaPreview : public Gtk::Paned {
public:
    TabPaaPreview();
    ~TabPaaPreview() override;
    void load_file(const std::string& path);
    void set_config(Config* cfg);
    void set_pbo_index_service(const std::shared_ptr<PboIndexService>& service);

private:
    Config* cfg_ = nullptr;
    std::shared_ptr<PboIndexService> pbo_index_service_;
    std::shared_ptr<armatools::pboindex::DB> db_;

    // Left panel: info
    Gtk::Box info_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};
    Gtk::Label info_label_;

    // Toolbar
    Gtk::Box toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button zoom_fit_button_{"Zoom Fit"};
    Gtk::Button zoom_100_button_{"100%"};
    Gtk::ToggleButton alpha_button_{"Alpha"};
    Gtk::Button save_png_button_{"Save PNG"};
    Gtk::ComboBoxText mip_combo_;

    // Right panel: image via DrawingArea with zoom/pan
    Gtk::DrawingArea draw_area_;

    // Decoded image data
    std::vector<uint8_t> decoded_pixels_;
    int decoded_width_ = 0;
    int decoded_height_ = 0;
    Glib::RefPtr<Gdk::Texture> display_texture_;

    // Zoom/pan state
    double zoom_level_ = 1.0;
    double pan_x_ = 0.0;
    double pan_y_ = 0.0;

    // Drag state
    bool dragging_ = false;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    double drag_start_pan_x_ = 0.0;
    double drag_start_pan_y_ = 0.0;

    // Current file path (for save)
    std::string current_path_;
    // Current file size
    std::uintmax_t current_file_size_ = 0;

    // PBO mode UI
    Gtk::Box switch_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Switch pbo_switch_;
    Gtk::Label pbo_label_{"PBO"};
    Gtk::Button search_button_{"Search"};
    Gtk::ScrolledWindow search_scroll_;
    Gtk::ListBox search_results_;
    std::vector<armatools::pboindex::FindResult> search_results_data_;
    bool pbo_mode_ = false;

    void on_browse();
    void on_pbo_mode_changed();
    void on_search();
    void on_search_result_selected(Gtk::ListBoxRow* row);
    void load_from_pbo(const armatools::pboindex::FindResult& r);

    // New helpers
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void update_display_texture();
    void zoom_fit();
    void zoom_100();
    void on_alpha_toggled();
    void on_save_png();
    void store_decoded(const std::vector<uint8_t>& pixels, int w, int h);
    void update_info(const std::string& prefix, const std::string& format,
                     int hdr_w, int hdr_h, int dec_w, int dec_h);
};
