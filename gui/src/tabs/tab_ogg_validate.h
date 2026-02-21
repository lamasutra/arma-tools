#pragma once

#include "config.h"

#include <gtkmm.h>
#include <thread>

class TabOggValidate : public Gtk::Box {
public:
    TabOggValidate();
    ~TabOggValidate() override;
    void set_config(Config* cfg);

private:
    Config* cfg_ = nullptr;
    std::thread worker_;

    Gtk::Box path_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Entry path_entry_;
    Gtk::Button browse_button_{"Browse..."};
    Gtk::Button browse_dir_button_{"Folder..."};
    Gtk::Button validate_button_{"Validate"};

    Gtk::ScrolledWindow results_scroll_;
    Gtk::TextView results_view_;
    Gtk::Label status_label_;

    void on_browse_file();
    void on_browse_dir();
    void on_validate();
};
