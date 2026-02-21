#include "tab_ogg_validate.h"
#include "pbo_util.h"

TabOggValidate::TabOggValidate() : Gtk::Box(Gtk::Orientation::VERTICAL, 8) {
    set_margin(8);

    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("OGG file, PBO, or directory...");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    path_box_.append(browse_dir_button_);
    path_box_.append(validate_button_);
    append(path_box_);

    append(status_label_);

    results_view_.set_editable(false);
    results_view_.set_monospace(true);
    results_scroll_.set_vexpand(true);
    results_scroll_.set_child(results_view_);
    append(results_scroll_);

    // Signals
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabOggValidate::on_browse_file));
    browse_dir_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabOggValidate::on_browse_dir));
    validate_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabOggValidate::on_validate));
}

void TabOggValidate::set_config(Config* cfg) { cfg_ = cfg; }

TabOggValidate::~TabOggValidate() {
    if (worker_.joinable()) worker_.join();
}

void TabOggValidate::on_browse_file() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("OGG / PBO files");
    filter->add_pattern("*.ogg");
    filter->add_pattern("*.pbo");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->open(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->open_finish(result);
                if (file) path_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabOggValidate::on_browse_dir() {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->select_folder_finish(result);
                if (file) path_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabOggValidate::on_validate() {
    if (!cfg_) return;

    auto input = std::string(path_entry_.get_text());
    if (input.empty()) {
        status_label_.set_text("Please specify a file or directory.");
        return;
    }

    auto tool = resolve_tool_path(*cfg_, "ogg_validate");
    if (tool.empty()) {
        status_label_.set_text("Error: ogg_validate binary not found.");
        return;
    }

    status_label_.set_text("Validating...");
    validate_button_.set_sensitive(false);
    results_view_.get_buffer()->set_text("");

    if (worker_.joinable()) worker_.join();

    worker_ = std::thread([this, tool, input]() {
        auto args = apply_tool_verbosity(cfg_, {"-r", "--warn", input}, false);
        auto res = run_subprocess(tool, args);

        Glib::signal_idle().connect_once([this, res]() {
            results_view_.get_buffer()->set_text(res.output);
            if (res.output.empty() && res.status == 0) {
                status_label_.set_text("Validation passed - no issues found.");
            } else if (res.status == 0) {
                status_label_.set_text("Validation complete.");
            } else if (res.status < 0) {
                status_label_.set_text("Error: Failed to run process.");
            } else {
                status_label_.set_text("Validation found issues (exit " + std::to_string(res.status) + ").");
            }
            validate_button_.set_sensitive(true);
        });
    });
}
