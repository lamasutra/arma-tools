#pragma once

#include <gtkmm.h>

class TabAbout : public Gtk::Box {
public:
    TabAbout();

private:
    Gtk::Image icon_;
    Gtk::Label title_;
    Gtk::Label version_;
    Gtk::Label description_;
};
