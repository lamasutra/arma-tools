#include "tab_p3d_convert.h"
#include "pbo_util.h"

#include <filesystem>

namespace fs = std::filesystem;

TabP3dConvert::TabP3dConvert() : Gtk::Box(Gtk::Orientation::VERTICAL, 8) {
    set_margin(8);

    // Input row
    input_label_.set_size_request(60, -1);
    input_entry_.set_hexpand(true);
    input_entry_.set_placeholder_text("P3D file or folder (batch)...");
    input_box_.append(input_label_);
    input_box_.append(input_entry_);
    input_box_.append(input_browse_file_);
    input_box_.append(input_browse_dir_);
    append(input_box_);

    // Output row
    output_label_.set_size_request(60, -1);
    output_entry_.set_hexpand(true);
    output_entry_.set_placeholder_text("Output folder (batch) or leave empty for in-place...");
    output_box_.append(output_label_);
    output_box_.append(output_entry_);
    output_box_.append(output_browse_);
    append(output_box_);

    // Convert button
    convert_button_.set_halign(Gtk::Align::START);
    append(convert_button_);
    append(status_label_);

    // Log
    log_view_.set_editable(false);
    log_view_.set_monospace(true);
    log_scroll_.set_vexpand(true);
    log_scroll_.set_child(log_view_);
    append(log_scroll_);

    // Signals
    input_browse_file_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dConvert::on_input_browse_file));
    input_browse_dir_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dConvert::on_input_browse_dir));
    output_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dConvert::on_output_browse));
    convert_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabP3dConvert::on_convert));
}

TabP3dConvert::~TabP3dConvert() {
    if (worker_.joinable()) worker_.join();
}

void TabP3dConvert::set_config(Config* cfg) { cfg_ = cfg; }

void TabP3dConvert::on_input_browse_file() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("P3D files");
    filter->add_pattern("*.p3d");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->open(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->open_finish(result);
                if (file) input_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabP3dConvert::on_input_browse_dir() {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->select_folder_finish(result);
                if (file) input_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabP3dConvert::on_output_browse() {
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

void TabP3dConvert::on_convert() {
    if (!cfg_) return;

    auto input = std::string(input_entry_.get_text());
    if (input.empty()) {
        status_label_.set_text("Please specify an input P3D file or folder.");
        return;
    }

    auto tool = resolve_tool_path(*cfg_, "p3d_odol2mlod");
    if (tool.empty()) {
        status_label_.set_text("Error: p3d_odol2mlod binary not found.");
        return;
    }

    auto output = std::string(output_entry_.get_text());
    std::vector<std::string> args;
    args.push_back(input);
    if (!output.empty()) args.push_back(output);
    args = apply_tool_verbosity(cfg_, args, false);

    std::string display_cmd = tool;
    for (const auto& a : args) display_cmd += " " + a;

    status_label_.set_text("Converting...");
    convert_button_.set_sensitive(false);
    log_view_.get_buffer()->set_text("Running: " + display_cmd + "\n\n");

    if (worker_.joinable()) worker_.join();

    worker_ = std::thread([this, tool, args]() {
        auto result = run_subprocess(tool, args);
        Glib::signal_idle().connect_once(
            sigc::bind(sigc::mem_fun(*this, &TabP3dConvert::on_conversion_finished),
                       result));
    });
}

void TabP3dConvert::on_conversion_finished(SubprocessResult result) {
    auto tbuf = log_view_.get_buffer();
    tbuf->insert(tbuf->end(), result.output);
    if (result.status == 0) {
        status_label_.set_text("Conversion complete.");
    } else {
        status_label_.set_text("Conversion failed (exit " + std::to_string(result.status) + ").");
    }
    convert_button_.set_sensitive(true);
}
