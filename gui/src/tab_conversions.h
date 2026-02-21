#pragma once

#include "config.h"

#include <gtkmm.h>
#include <thread>

class TabConversions : public Gtk::Box {
public:
    TabConversions();
    ~TabConversions() override;
    void set_config(Config* cfg);

private:
    Config* cfg_ = nullptr;

    Gtk::Box mode_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label mode_label_{"Mode:"};
    Gtk::ComboBoxText mode_combo_;

    Gtk::Box input_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label input_label_{"Input ASC:"};
    Gtk::Entry input_entry_;
    Gtk::Button input_browse_{"Browse..."};

    Gtk::Box output_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label output_label_{"Output GeoTIFF:"};
    Gtk::Entry output_entry_;
    Gtk::Button output_browse_{"Browse..."};

    Gtk::Button convert_button_{"Convert"};
    Gtk::Label status_label_;
    Gtk::ScrolledWindow log_scroll_;
    Gtk::TextView log_view_;

    std::thread worker_;

    void on_mode_changed();
    void on_input_browse();
    void on_output_browse();
    void on_convert();
    void append_log(const std::string& text);

    void convert_asc_to_geotiff(const std::string& input, const std::string& output);
    void convert_paa_to_png(const std::string& input, const std::string& output);
    void convert_png_to_paa(const std::string& input, const std::string& output);
    void convert_paa_to_tga(const std::string& input, const std::string& output);
    void convert_tga_to_paa(const std::string& input, const std::string& output);
};
