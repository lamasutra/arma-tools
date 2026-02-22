#include "tab_pbo.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/pbo.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

TabPbo::TabPbo() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    // Left panel
    left_box_.set_margin(8);
    left_box_.set_size_request(200, -1);

    pbo_label_.set_margin_end(2);
    path_box_.append(pbo_label_);

    switch_box_.set_valign(Gtk::Align::CENTER);
    switch_box_.set_vexpand(false);
    switch_box_.append(pbo_switch_);

    path_box_.append(switch_box_);
    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("PBO file path...");
    browse_button_.set_tooltip_text("Browse for a PBO file");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    search_button_.set_visible(false);
    path_box_.append(search_button_);
    search_spinner_.set_visible(false);
    path_box_.append(search_spinner_);
    search_count_label_.set_visible(false);
    path_box_.append(search_count_label_);
    left_box_.append(path_box_);

    // PBO search results (index mode)
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    left_box_.append(search_scroll_);

    // PBO entry list (file mode)
    list_scroll_.set_vexpand(true);
    list_scroll_.set_child(entry_list_);
    left_box_.append(list_scroll_);

    set_start_child(left_box_);
    set_position(380);

    // Right panel
    right_box_.set_margin(8);

    pbo_info_label_.set_halign(Gtk::Align::START);
    pbo_info_label_.set_wrap(true);
    right_box_.append(pbo_info_label_);

    info_view_.set_editable(false);
    info_view_.set_monospace(true);
    info_scroll_.set_vexpand(true);
    info_scroll_.set_child(info_view_);
    right_box_.append(info_scroll_);

    extract_dir_entry_.set_hexpand(true);
    extract_dir_entry_.set_placeholder_text("Extract to directory...");
    extract_browse_.set_tooltip_text("Browse for output directory");
    extract_button_.set_tooltip_text("Extract all files from PBO");
    extract_selected_button_.set_tooltip_text("Extract selected file from PBO");
    extract_box_.append(extract_dir_entry_);
    extract_box_.append(extract_browse_);
    extract_box_.append(extract_button_);
    extract_box_.append(extract_selected_button_);
    right_box_.append(extract_box_);
    right_box_.append(status_label_);

    set_end_child(right_box_);

    // Signals
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPbo::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_pbo(path_entry_.get_text());
    });
    entry_list_.signal_row_selected().connect(sigc::mem_fun(*this, &TabPbo::on_entry_selected));
    extract_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPbo::on_extract));
    extract_selected_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPbo::on_extract_selected));
    extract_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabPbo::on_extract_browse));
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabPbo::on_pbo_mode_changed));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPbo::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabPbo::on_search_result_selected));
}

TabPbo::~TabPbo() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    if (worker_.joinable())
        worker_.join();
}

void TabPbo::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabPbo::set_config(Config* cfg) {
    cfg_ = cfg;
    db_.reset();

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
    });
}

void TabPbo::on_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PBO files");
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
                if (file) {
                    path_entry_.set_text(file->get_path());
                    load_pbo(file->get_path());
                }
            } catch (...) {}
        });
}

void TabPbo::load_pbo(const std::string& path) {
    if (path.empty()) return;

    // Clear
    while (auto* row = entry_list_.get_row_at_index(0))
        entry_list_.remove(*row);
    info_view_.get_buffer()->set_text("");

    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            pbo_info_label_.set_text("Error: Cannot open file");
            return;
        }

        auto pbo = armatools::pbo::read(f);
        app_log(LogLevel::Info, "Loaded PBO: " + path + " (" + std::to_string(pbo.entries.size()) + " entries)");

        // PBO summary
        std::ostringstream summary;
        summary << "Entries: " << pbo.entries.size() << "\n";
        if (!pbo.extensions.empty()) {
            summary << "Extensions:\n";
            for (const auto& [k, v] : pbo.extensions) {
                summary << "  " << k << " = " << v << "\n";
            }
        }
        if (!pbo.checksum.empty()) {
            summary << "SHA1: ";
            for (auto b : pbo.checksum)
                summary << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
            summary << "\n";
        }

        uint64_t total_size = 0;
        for (const auto& e : pbo.entries) total_size += e.original_size;
        summary << std::dec << "Total size: " << (total_size / 1024) << " KB\n";
        pbo_info_label_.set_text(summary.str());

        // Populate file list
        for (const auto& entry : pbo.entries) {
            auto text = entry.filename + "  (" + std::to_string(entry.original_size) + " B)";
            auto* label = Gtk::make_managed<Gtk::Label>(text);
            label->set_halign(Gtk::Align::START);
            entry_list_.append(*label);
        }

        // Auto-suggest extract dir
        if (extract_dir_entry_.get_text().empty()) {
            fs::path p{path};
            extract_dir_entry_.set_text((p.parent_path() / p.stem()).string());
        }

    } catch (const std::exception& e) {
        app_log(LogLevel::Error, std::string("PBO load error: ") + e.what());
        pbo_info_label_.set_text(std::string("Error: ") + e.what());
    }
}

void TabPbo::on_entry_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    int idx = row->get_index();
    auto path = std::string(path_entry_.get_text());
    if (path.empty()) return;

    try {
        std::ifstream f(path, std::ios::binary);
        auto pbo = armatools::pbo::read(f);
        if (idx < 0 || static_cast<size_t>(idx) >= pbo.entries.size()) return;

        const auto& entry = pbo.entries[static_cast<size_t>(idx)];
        std::ostringstream info;
        info << "Filename: " << entry.filename << "\n";
        info << "Original size: " << entry.original_size << " bytes\n";
        info << "Data size: " << entry.data_size << " bytes\n";
        info << "Packing method: " << entry.packing_method << "\n";
        info << "Timestamp: " << entry.timestamp << "\n";
        info << "Data offset: " << entry.data_offset << "\n";
        info_view_.get_buffer()->set_text(info.str());
    } catch (const std::exception& e) {
        info_view_.get_buffer()->set_text(std::string("Error: ") + e.what());
    }
}

void TabPbo::on_extract_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->select_folder(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->select_folder_finish(result);
                if (file) extract_dir_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabPbo::on_extract() {
    auto pbo_path = std::string(path_entry_.get_text());
    auto out_dir = std::string(extract_dir_entry_.get_text());
    if (pbo_path.empty() || out_dir.empty()) {
        status_label_.set_text("Please specify PBO and output directory.");
        return;
    }

    // Use pbo_extract tool via subprocess
    if (!cfg_) return;
    auto tool = resolve_tool_path(*cfg_, "pbo_extract");
    if (tool.empty()) {
        status_label_.set_text("Error: pbo_extract binary not found.");
        return;
    }

    status_label_.set_text("Extracting...");
    extract_button_.set_sensitive(false);
    extract_selected_button_.set_sensitive(false);

    if (worker_.joinable())
        worker_.join();

    worker_ = std::thread([this, tool, pbo_path, out_dir]() {
        auto args = apply_tool_verbosity(cfg_, {pbo_path, out_dir}, false);
        auto result = run_subprocess(tool, args);
        Glib::signal_idle().connect_once([this, result]() {
            if (result.status == 0) {
                app_log(LogLevel::Info, "PBO extraction complete");
                status_label_.set_text("Extraction complete.");
            } else {
                app_log(LogLevel::Error, "PBO extraction failed: " + result.output);
                status_label_.set_text("Extraction failed: " + result.output);
            }
            extract_button_.set_sensitive(true);
            extract_selected_button_.set_sensitive(true);
        });
    });
}

void TabPbo::on_extract_selected() {
    auto pbo_path = std::string(path_entry_.get_text());
    auto out_dir = std::string(extract_dir_entry_.get_text());
    if (pbo_path.empty() || out_dir.empty()) {
        status_label_.set_text("Please specify PBO and output directory.");
        return;
    }

    auto* row = entry_list_.get_selected_row();
    if (!row) {
        status_label_.set_text("No file selected.");
        return;
    }

    int idx = row->get_index();

    // Read entry name from PBO
    std::string entry_name;
    try {
        std::ifstream f(pbo_path, std::ios::binary);
        auto pbo = armatools::pbo::read(f);
        if (idx < 0 || static_cast<size_t>(idx) >= pbo.entries.size()) return;
        entry_name = pbo.entries[static_cast<size_t>(idx)].filename;
    } catch (const std::exception& e) {
        status_label_.set_text(std::string("Error: ") + e.what());
        return;
    }

    if (!cfg_) return;
    auto tool = resolve_tool_path(*cfg_, "pbo_extract");
    if (tool.empty()) {
        status_label_.set_text("Error: pbo_extract binary not found.");
        return;
    }

    status_label_.set_text("Extracting " + entry_name + "...");
    extract_button_.set_sensitive(false);
    extract_selected_button_.set_sensitive(false);

    if (worker_.joinable())
        worker_.join();

    worker_ = std::thread([this, tool, pbo_path, out_dir, entry_name]() {
        auto args = apply_tool_verbosity(cfg_, {pbo_path, out_dir, entry_name}, false);
        auto result = run_subprocess(tool, args);
        Glib::signal_idle().connect_once([this, result, entry_name]() {
            if (result.status == 0) {
                app_log(LogLevel::Info, "Extracted: " + entry_name);
                status_label_.set_text("Extracted: " + entry_name);
            } else {
                app_log(LogLevel::Error, "Extract failed: " + result.output);
                status_label_.set_text("Extract failed: " + result.output);
            }
            extract_button_.set_sensitive(true);
            extract_selected_button_.set_sensitive(true);
        });
    });
}

void TabPbo::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search indexed PBOs...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        search_scroll_.set_visible(false);
        search_count_label_.set_visible(false);
    } else {
        path_entry_.set_placeholder_text("PBO file path...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        search_scroll_.set_visible(false);
        search_count_label_.set_visible(false);
    }
}

void TabPbo::on_search() {
    auto query = std::string(path_entry_.get_text());

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
    search_results_paths_.clear();

    // Get all indexed PBO paths and filter by query.
    auto all_pbos = db_->list_pbo_paths();
    for (const auto& pbo_path : all_pbos) {
        if (!query.empty()) {
            // Case-insensitive substring match on filename.
            auto filename = fs::path(pbo_path).filename().string();
            std::string lower_fn = filename;
            std::string lower_q = query;
            std::transform(lower_fn.begin(), lower_fn.end(), lower_fn.begin(), ::tolower);
            std::transform(lower_q.begin(), lower_q.end(), lower_q.begin(), ::tolower);
            if (lower_fn.find(lower_q) == std::string::npos) continue;
        }
        search_results_paths_.push_back(pbo_path);
    }

    for (const auto& p : search_results_paths_) {
        auto* label = Gtk::make_managed<Gtk::Label>(p);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        search_results_.append(*label);
    }

    // Hide spinner, show count
    search_spinner_.set_spinning(false);
    search_spinner_.set_visible(false);
    search_count_label_.set_text(std::to_string(search_results_paths_.size()) + " PBOs");
    search_count_label_.set_visible(true);

    search_scroll_.set_visible(!search_results_paths_.empty());
}

void TabPbo::on_search_result_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto idx = static_cast<size_t>(row->get_index());
    if (idx >= search_results_paths_.size()) return;

    auto& pbo_path = search_results_paths_[idx];
    path_entry_.set_text(pbo_path);
    load_pbo(pbo_path);
}
