#pragma once

#include "config.h"

#include <thread>

#include <gtkmm.h>

class TabP3dConvert : public Gtk::Box {
public:
    TabP3dConvert();
    ~TabP3dConvert() override;
    void set_config(Config* cfg);

private:
    Config* cfg_ = nullptr;

    Gtk::Box input_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label input_label_{"Input:"};
    Gtk::Entry input_entry_;
    Gtk::Button input_browse_file_{"File..."};
    Gtk::Button input_browse_dir_{"Folder..."};

    Gtk::Box output_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label output_label_{"Output:"};
    Gtk::Entry output_entry_;
    Gtk::Button output_browse_{"Browse..."};

    Gtk::Button convert_button_{"Convert ODOL to MLOD"};
    Gtk::Label status_label_;
    Gtk::ScrolledWindow log_scroll_;
    Gtk::TextView log_view_;

    void on_input_browse_file();
    void on_input_browse_dir();
    void on_output_browse();
    void on_convert();

    std::thread worker_;
};
