#include "tab_config_viewer.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/config.h>
#include <armatools/pboindex.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

TabConfigViewer::TabConfigViewer() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    set_margin(8);

    // ---- Left panel ----
    left_box_.set_margin(4);

    // PBO mode switch
    pbo_label_.set_margin_end(2);
    path_box_.append(pbo_label_);
    pbo_switch_.add_css_class("compact-switch");
    path_box_.append(pbo_switch_);

    // Path row
    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("config.bin / config.cpp / .rvmat file...");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    search_button_.set_visible(false);
    path_box_.append(search_button_);
    left_box_.append(path_box_);

    // Search results (PBO mode only)
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    left_box_.append(search_scroll_);

    // Info label
    info_label_.set_halign(Gtk::Align::START);
    info_label_.set_wrap(true);
    info_label_.set_selectable(true);
    left_box_.append(info_label_);

    // Tree view for config hierarchy
    tree_store_ = Gtk::TreeStore::create(tree_columns_);
    tree_view_.set_model(tree_store_);
    tree_view_.append_column("Name", tree_columns_.col_name);
    tree_view_.append_column("Value", tree_columns_.col_value);
    tree_view_.append_column("Type", tree_columns_.col_type);
    // Make columns resizable
    for (int i = 0; i < 3; ++i) {
        auto* col = tree_view_.get_column(i);
        if (col) {
            col->set_resizable(true);
            col->set_min_width(60);
        }
    }
    tree_view_.set_headers_visible(true);
    tree_view_.set_enable_tree_lines(true);
    tree_scroll_.set_child(tree_view_);
    tree_scroll_.set_vexpand(true);
    tree_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    left_box_.append(tree_scroll_);

    set_start_child(left_box_);
    set_resize_start_child(true);
    set_shrink_start_child(false);

    // ---- Right panel ----
    right_box_.set_margin(4);

    // Text view
    text_view_.set_editable(false);
    text_view_.set_monospace(true);
    text_view_.set_wrap_mode(Gtk::WrapMode::NONE);
    text_scroll_.set_vexpand(true);
    text_scroll_.set_hexpand(true);
    text_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    text_scroll_.set_child(text_view_);
    right_box_.append(text_scroll_);

    // Setup syntax highlighting tags
    setup_tags();

    // Search bar at bottom
    text_search_entry_.set_hexpand(true);
    text_search_entry_.set_placeholder_text("Search in config text (Ctrl+F)...");
    match_count_label_.set_halign(Gtk::Align::END);
    search_bar_box_.append(text_search_entry_);
    search_bar_box_.append(match_count_label_);
    right_box_.append(search_bar_box_);

    set_end_child(right_box_);
    set_resize_end_child(true);
    set_shrink_end_child(false);

    // Set a reasonable default split position
    set_position(350);

    // Signals
    browse_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabConfigViewer::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_file(path_entry_.get_text());
    });
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabConfigViewer::on_pbo_mode_changed));
    search_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabConfigViewer::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabConfigViewer::on_search_result_selected));
    text_search_entry_.signal_search_changed().connect(
        sigc::mem_fun(*this, &TabConfigViewer::on_text_search_changed));

    // Ctrl+F shortcut to focus the search entry
    auto ctrl = Gtk::EventControllerKey::create();
    ctrl->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
            if (keyval == GDK_KEY_f &&
                (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{}) {
                text_search_entry_.grab_focus();
                return true;
            }
            return false;
        }, false);
    add_controller(ctrl);
}

TabConfigViewer::~TabConfigViewer() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
}

void TabConfigViewer::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabConfigViewer::set_config(Config* cfg) {
    cfg_ = cfg;
    db_.reset();

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
    });
}

void TabConfigViewer::on_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Config files");
    filter->add_pattern("*.bin");
    filter->add_pattern("*.cpp");
    filter->add_pattern("*.hpp");
    filter->add_pattern("*.rvmat");
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
                    load_file(file->get_path());
                }
            } catch (...) {}
        });
}

void TabConfigViewer::load_file(const std::string& path) {
    if (path.empty()) return;

    try {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            info_label_.set_text("Error: Cannot open file");
            return;
        }

        auto ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        load_config_data(f, ext, fs::path(path).filename().string());

    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        text_view_.get_buffer()->set_text("");
        tree_store_->clear();
        has_config_ = false;
    }
}

void TabConfigViewer::load_config_data(std::istream& stream,
                                        const std::string& ext,
                                        const std::string& display_name) {
    bool is_binary = false;

    // Try rapified (binary) first for .bin/.rvmat, text for .cpp/.hpp
    if (ext == ".bin" || ext == ".rvmat") {
        current_cfg_ = armatools::config::read(stream);
        is_binary = true;
    } else {
        current_cfg_ = armatools::config::parse_text(stream);
    }
    has_config_ = true;

    // Count entries recursively
    std::function<int(const armatools::config::ConfigClass&)> count_entries;
    count_entries = [&](const armatools::config::ConfigClass& cls) -> int {
        int n = static_cast<int>(cls.entries.size());
        for (const auto& e : cls.entries) {
            if (auto* c = std::get_if<armatools::config::ClassEntryOwned>(&e.entry)) {
                if (c->cls) n += count_entries(*c->cls);
            }
        }
        return n;
    };
    int total = count_entries(current_cfg_.root);

    info_label_.set_text(
        display_name + " - " +
        std::string(is_binary ? "Rapified" : "Text") +
        " config - " + std::to_string(total) + " entries");

    // Write as text
    std::ostringstream out;
    armatools::config::write_text(out, current_cfg_);
    auto buf = text_view_.get_buffer();
    buf->set_text(out.str());

    // Apply syntax highlighting
    apply_highlighting();

    // Populate tree view
    tree_store_->clear();
    populate_tree(current_cfg_.root);
    tree_view_.expand_all();
}

// ---- PBO mode ----

void TabConfigViewer::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search in PBO (*.bin, *.rvmat)...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        search_scroll_.set_visible(true);
    } else {
        path_entry_.set_placeholder_text("config.bin / config.cpp / .rvmat file...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        search_scroll_.set_visible(false);
    }
}

void TabConfigViewer::on_search() {
    auto query = std::string(path_entry_.get_text());
    if (query.empty() || !db_) return;

    while (auto* row = search_results_.get_row_at_index(0))
        search_results_.remove(*row);
    search_results_data_.clear();

    auto bin_results = db_->find_files("*" + query + "*.bin");
    auto rvmat_results = db_->find_files("*" + query + "*.rvmat");

    search_results_data_ = bin_results;
    search_results_data_.insert(search_results_data_.end(),
                                rvmat_results.begin(), rvmat_results.end());

    for (const auto& r : search_results_data_) {
        auto display = r.prefix + "/" + r.file_path;
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        search_results_.append(*label);
    }
}

void TabConfigViewer::on_search_result_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto idx = static_cast<size_t>(row->get_index());
    if (idx >= search_results_data_.size()) return;
    load_from_pbo(search_results_data_[idx]);
}

void TabConfigViewer::load_from_pbo(const armatools::pboindex::FindResult& r) {
    auto data = extract_from_pbo(r.pbo_path, r.file_path);
    if (data.empty()) {
        info_label_.set_text("Error: Could not extract from PBO");
        return;
    }

    auto ext = fs::path(r.file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    try {
        std::string data_str(data.begin(), data.end());
        std::istringstream stream(data_str);

        auto display_name = r.prefix + "/" + r.file_path;
        load_config_data(stream, ext, display_name);

        app_log(LogLevel::Info, "Loaded config from PBO: " + display_name);

    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        text_view_.get_buffer()->set_text("");
        tree_store_->clear();
        has_config_ = false;
    }
}

// ---- Tree view population ----

void TabConfigViewer::populate_tree(const armatools::config::ConfigClass& cls,
                                     Gtk::TreeModel::Row* parent) {
    for (const auto& entry : cls.entries) {
        Gtk::TreeModel::Row row;
        if (parent) {
            row = *(tree_store_->append(parent->children()));
        } else {
            row = *(tree_store_->append());
        }

        row[tree_columns_.col_name] = entry.name;

        if (auto* s = std::get_if<armatools::config::StringEntry>(&entry.entry)) {
            row[tree_columns_.col_value] = s->value;
            row[tree_columns_.col_type] = "string";
        } else if (auto* f = std::get_if<armatools::config::FloatEntry>(&entry.entry)) {
            row[tree_columns_.col_value] = std::to_string(f->value);
            row[tree_columns_.col_type] = "float";
        } else if (auto* i = std::get_if<armatools::config::IntEntry>(&entry.entry)) {
            row[tree_columns_.col_value] = std::to_string(i->value);
            row[tree_columns_.col_type] = "int";
        } else if (auto* a = std::get_if<armatools::config::ArrayEntry>(&entry.entry)) {
            row[tree_columns_.col_value] = "[" + std::to_string(a->elements.size()) + " elements]";
            row[tree_columns_.col_type] = a->expansion ? "array+=" : "array";
        } else if (auto* c = std::get_if<armatools::config::ClassEntryOwned>(&entry.entry)) {
            if (c->cls) {
                auto parent_info = c->cls->parent.empty()
                    ? std::string{} : " : " + c->cls->parent;
                if (c->cls->deletion) {
                    row[tree_columns_.col_value] = "(deleted)";
                    row[tree_columns_.col_type] = "delete";
                } else if (c->cls->external) {
                    row[tree_columns_.col_value] = "(external)";
                    row[tree_columns_.col_type] = "class (ext)";
                } else {
                    row[tree_columns_.col_value] = parent_info;
                    row[tree_columns_.col_type] = "class";
                }
                populate_tree(*c->cls, &row);
            } else {
                row[tree_columns_.col_type] = "class (null)";
            }
        }
    }
}

// ---- Syntax highlighting ----

void TabConfigViewer::setup_tags() {
    auto buf = text_view_.get_buffer();
    auto tag_table = buf->get_tag_table();

    tag_keyword_ = buf->create_tag("keyword");
    tag_keyword_->property_foreground() = "#CC0000";
    tag_keyword_->property_weight() = Pango::Weight::BOLD;

    tag_string_ = buf->create_tag("string");
    tag_string_->property_foreground() = "#4E9A06";

    tag_number_ = buf->create_tag("number");
    tag_number_->property_foreground() = "#3465A4";

    tag_comment_ = buf->create_tag("comment");
    tag_comment_->property_foreground() = "#888888";
    tag_comment_->property_style() = Pango::Style::ITALIC;

    tag_search_match_ = buf->create_tag("search_match");
    tag_search_match_->property_background() = "#FFFF00";
}

void TabConfigViewer::apply_highlighting() {
    auto buf = text_view_.get_buffer();
    auto text = buf->get_text();
    if (text.empty()) return;

    // Remove existing highlighting tags (but not search_match)
    buf->remove_tag(tag_keyword_, buf->begin(), buf->end());
    buf->remove_tag(tag_string_, buf->begin(), buf->end());
    buf->remove_tag(tag_number_, buf->begin(), buf->end());
    buf->remove_tag(tag_comment_, buf->begin(), buf->end());

    auto raw = text.raw();

    // Comments: lines starting with // (possibly with leading whitespace)
    {
        std::regex re(R"(//[^\n]*)");
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            auto start_iter = buf->get_iter_at_offset(static_cast<int>(it->position()));
            auto end_iter = buf->get_iter_at_offset(
                static_cast<int>(it->position() + it->length()));
            buf->apply_tag(tag_comment_, start_iter, end_iter);
        }
    }

    // Strings: text in double quotes
    {
        std::regex re(R"("(?:[^"\\]|\\.)*")");
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            auto start_iter = buf->get_iter_at_offset(static_cast<int>(it->position()));
            auto end_iter = buf->get_iter_at_offset(
                static_cast<int>(it->position() + it->length()));
            // Only apply if not already inside a comment
            if (!start_iter.has_tag(tag_comment_)) {
                buf->apply_tag(tag_string_, start_iter, end_iter);
            }
        }
    }

    // Keywords: class, delete (as whole words)
    {
        std::regex re(R"(\b(class|delete)\b)");
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            auto start_iter = buf->get_iter_at_offset(static_cast<int>(it->position()));
            auto end_iter = buf->get_iter_at_offset(
                static_cast<int>(it->position() + it->length()));
            if (!start_iter.has_tag(tag_comment_) && !start_iter.has_tag(tag_string_)) {
                buf->apply_tag(tag_keyword_, start_iter, end_iter);
            }
        }
    }

    // Numbers: integer and float literals (not inside strings/comments)
    {
        std::regex re(R"(\b-?\d+(\.\d+)?(e[+-]?\d+)?\b)");
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            auto start_iter = buf->get_iter_at_offset(static_cast<int>(it->position()));
            auto end_iter = buf->get_iter_at_offset(
                static_cast<int>(it->position() + it->length()));
            if (!start_iter.has_tag(tag_comment_) && !start_iter.has_tag(tag_string_)) {
                buf->apply_tag(tag_number_, start_iter, end_iter);
            }
        }
    }
}

// ---- Text search ----

void TabConfigViewer::on_text_search_changed() {
    auto buf = text_view_.get_buffer();

    // Remove previous search highlights
    buf->remove_tag(tag_search_match_, buf->begin(), buf->end());
    match_count_label_.set_text("");

    auto query = std::string(text_search_entry_.get_text());
    if (query.empty()) return;

    auto text = buf->get_text().raw();

    // Case-insensitive search
    std::string query_lower = query;
    std::string text_lower = text;
    std::transform(query_lower.begin(), query_lower.end(),
                   query_lower.begin(), ::tolower);
    std::transform(text_lower.begin(), text_lower.end(),
                   text_lower.begin(), ::tolower);

    int count = 0;
    size_t pos = 0;
    bool first = true;

    while ((pos = text_lower.find(query_lower, pos)) != std::string::npos) {
        auto start_iter = buf->get_iter_at_offset(static_cast<int>(pos));
        auto end_iter = buf->get_iter_at_offset(
            static_cast<int>(pos + query.size()));
        buf->apply_tag(tag_search_match_, start_iter, end_iter);

        // Scroll to first match
        if (first) {
            text_view_.scroll_to(start_iter, 0.1);
            first = false;
        }

        ++count;
        pos += query.size();
    }

    match_count_label_.set_text(std::to_string(count) + " matches");
}
