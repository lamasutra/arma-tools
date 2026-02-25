#include "tab_obj_replace.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/armapath.h>
#include <armatools/p3d.h>
#include <armatools/pboindex.h>
#include <armatools/wrp.h>

#include <gtk/gtk.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

bool is_model_matched(const std::string& new_model) {
    auto lower = new_model;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower != "unmatched";
}

bool is_model_multi_match(const std::string& new_model) {
    return new_model.find(';') != std::string::npos;
}

std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

void log_async_dialog_error(const std::string& action, const std::exception& e) {
    app_log(LogLevel::Warning, "ObjReplace " + action + " failed: " + std::string(e.what()));
}

void log_async_dialog_error(const std::string& action) {
    app_log(LogLevel::Warning, "ObjReplace " + action + " failed: unknown error");
}

struct TextureExtractStats {
    int existing = 0;
    int extracted = 0;
    int missing = 0;
    int failed = 0;
};

TextureExtractStats extract_textures_to_drive(
    armatools::pboindex::DB& db,
    const std::string& drive_root,
    const std::set<std::string>& textures) {

    TextureExtractStats stats{};
    for (const auto& tex : textures) {
        if (tex.empty() || armatools::armapath::is_procedural_texture(tex)) continue;

        const auto normalized = armatools::armapath::to_os(tex);
        const auto dest = fs::path(drive_root) / normalized;
        std::error_code ec;
        if (fs::exists(dest, ec)) {
            stats.existing++;
            continue;
        }

        const std::string pattern = "*" + normalized.filename().string();
        std::vector<armatools::pboindex::FindResult> results;
        try {
            results = db.find_files(pattern);
        } catch (const std::exception& e) {
            app_log(LogLevel::Debug, "ObjReplace texture lookup failed for " + tex + ": " + e.what());
            stats.missing++;
            continue;
        } catch (...) {
            stats.missing++;
            continue;
        }
        if (results.empty()) {
            stats.missing++;
            continue;
        }

        auto data = extract_from_pbo(results[0].pbo_path, results[0].file_path);
        if (data.empty()) {
            stats.failed++;
            continue;
        }

        fs::create_directories(dest.parent_path(), ec);
        if (ec) {
            stats.failed++;
            continue;
        }
        std::ofstream out(dest, std::ios::binary);
        if (!out.is_open()) {
            stats.failed++;
            continue;
        }
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out.good()) {
            stats.failed++;
            continue;
        }
        stats.extracted++;
    }
    return stats;
}

} // namespace

// -- ObjReplEntry --

bool ObjReplEntry::is_matched() const {
    return is_model_matched(new_model);
}

bool ObjReplEntry::is_multi_match() const {
    return is_model_multi_match(new_model);
}

// -- ObjReplRow --

ObjReplRow::ObjReplRow(uint64_t id, uint32_t display_index,
                       std::string old_model, std::string new_model, int count)
    : Glib::ObjectBase("ObjReplRow"),
      Glib::Object(),
      id_(id),
      display_index_(display_index),
      old_model_(std::move(old_model)),
      new_model_(std::move(new_model)),
      count_(count) {}

ObjReplRow::Ptr ObjReplRow::create(uint64_t id, uint32_t display_index,
                                   std::string old_model, std::string new_model, int count) {
    return Glib::make_refptr_for_instance<ObjReplRow>(
        new ObjReplRow(id, display_index, std::move(old_model), std::move(new_model), count));
}

bool ObjReplRow::is_matched() const {
    return is_model_matched(new_model_);
}

bool ObjReplRow::is_multi_match() const {
    return is_model_multi_match(new_model_);
}

// -- Helpers --

namespace {

using RowPtr = Glib::RefPtr<ObjReplRow>;
using RowTextGetter = std::function<std::string(const ObjReplRow&)>;
using RowCompare = std::function<int(const ObjReplRow&, const ObjReplRow&)>;

RowPtr row_from_list_item(const Glib::RefPtr<Gtk::ListItem>& item) {
    if (!item) return {};
    return std::dynamic_pointer_cast<ObjReplRow>(item->get_item());
}

RowPtr row_from_gitem(gconstpointer item) {
    if (!item) return {};
    auto obj = Glib::wrap(G_OBJECT(const_cast<gpointer>(item)), true);
    return std::dynamic_pointer_cast<ObjReplRow>(obj);
}

Glib::RefPtr<Gtk::ColumnViewColumn> add_text_column(
    Gtk::ColumnView& view,
    const std::string& title,
    const RowTextGetter& getter,
    const RowCompare& sorter,
    Gtk::Align align,
    bool ellipsize,
    bool expand,
    int fixed_width,
    const char* css_class = nullptr) {
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([align, ellipsize, css_class](
                                        const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_halign(align);
        if (ellipsize)
            label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        if (css_class)
            label->add_css_class(css_class);
        item->set_child(*label);
    });
    factory->signal_bind().connect([getter](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto row = row_from_list_item(item);
        if (!row) return;
        auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
        if (label) label->set_text(getter(*row));
    });

    auto col = Gtk::ColumnViewColumn::create(title, factory);
    if (fixed_width > 0)
        col->set_fixed_width(fixed_width);
    col->set_resizable(true);
    if (expand)
        col->set_expand(true);

    if (sorter) {
        auto cmp_ptr = new RowCompare(sorter);
        auto* sorter_c = gtk_custom_sorter_new(
            [](gconstpointer a, gconstpointer b, gpointer user_data) -> int {
                auto* cmp_cb = static_cast<RowCompare*>(user_data);
                auto row_a = row_from_gitem(a);
                auto row_b = row_from_gitem(b);
                if (!row_a || !row_b) return 0;
                return (*cmp_cb)(*row_a, *row_b);
            },
            cmp_ptr,
            [](gpointer data) { delete static_cast<RowCompare*>(data); });
        col->set_sorter(Glib::wrap(GTK_SORTER(sorter_c)));
    }

    view.append_column(col);
    return col;
}

Glib::RefPtr<Gtk::ColumnViewColumn> add_int_column(
    Gtk::ColumnView& view,
    const std::string& title,
    const std::function<int(const ObjReplRow&)>& getter,
    const RowCompare& sorter,
    Gtk::Align align,
    int fixed_width) {
    return add_text_column(
        view, title,
        [getter](const ObjReplRow& row) {
            auto value = getter(row);
            return value > 0 ? std::to_string(value) : "";
        },
        sorter, align, false, false, fixed_width);
}

// Helper lambdas moved below for use in constructor
// auto dialog_navigate_impl = [](const std::string& path) {};

} // namespace

// -- TabObjReplace --

TabObjReplace::TabObjReplace() : Gtk::Box(Gtk::Orientation::VERTICAL) {
    set_margin(8);

    // Toolbar rows
    toolbar_.set_margin_bottom(4);

    repl_entry_.set_hexpand(true);
    repl_entry_.set_placeholder_text("Replacement mapping file (TSV)...");
    repl_row_.append(repl_label_);
    repl_row_.append(repl_entry_);
    repl_row_.append(repl_browse_);
    repl_row_.append(repl_load_);
    toolbar_.append(repl_row_);

    wrp_entry_.set_hexpand(true);
    wrp_entry_.set_placeholder_text("WRP file for instance counts...");
    wrp_row_.append(wrp_label_);
    wrp_row_.append(wrp_entry_);
    wrp_row_.append(wrp_browse_);
    wrp_row_.append(wrp_load_);
    toolbar_.append(wrp_row_);

    filter_entry_.set_hexpand(true);
    filter_entry_.set_placeholder_text("Filter models...");
    filter_row_.append(filter_label_);
    filter_row_.append(filter_entry_);
    filter_row_.append(set_unmatched_button_);
    filter_row_.append(auto_match_button_);
    filter_row_.append(save_button_);
    filter_row_.append(save_as_button_);
    toolbar_.append(filter_row_);

    // append(toolbar_);

    // -- ColumnView setup --
    table_model_ = Gio::ListStore<ObjReplRow>::create();

    // Filter (using C API — GtkCustomFilter not wrapped in gtkmm 4.10)
    table_filter_c_ = gtk_custom_filter_new(
        [](gpointer item, gpointer user_data) -> gboolean {
            auto* self = static_cast<TabObjReplace*>(user_data);
            auto row = row_from_gitem(item);
            if (!row) return FALSE;

            auto filter_text = std::string(self->filter_entry_.get_text());
            if (filter_text.empty()) return TRUE;

            std::transform(filter_text.begin(), filter_text.end(), filter_text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            auto old_lower = row->old_model();
            std::transform(old_lower.begin(), old_lower.end(), old_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto new_lower = row->new_model();
            std::transform(new_lower.begin(), new_lower.end(), new_lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return (old_lower.find(filter_text) != std::string::npos ||
                    new_lower.find(filter_text) != std::string::npos) ? TRUE : FALSE;
        },
        this, nullptr);

    filter_model_ = Gtk::FilterListModel::create(
        table_model_, Glib::wrap(GTK_FILTER(table_filter_c_)));

    add_text_column(
        table_view_, "#",
        [](const ObjReplRow& row) { return std::to_string(row.display_index()); },
        [](const ObjReplRow& a, const ObjReplRow& b) {
            return (a.display_index() < b.display_index())
                       ? -1
                       : (a.display_index() > b.display_index()) ? 1 : 0;
        },
        Gtk::Align::END, false, false, 60, "dim-label");

    add_text_column(
        table_view_, "St",
        [](const ObjReplRow& row) {
            if (row.is_multi_match()) return std::string("?");
            if (row.is_matched()) return std::string("+");
            return std::string("-");
        },
        [](const ObjReplRow& a, const ObjReplRow& b) {
            return static_cast<int>(a.is_matched()) - static_cast<int>(b.is_matched());
        },
        Gtk::Align::CENTER, false, false, 50);

    add_text_column(
        table_view_, "Old Model",
        [](const ObjReplRow& row) { return row.old_model(); },
        [](const ObjReplRow& a, const ObjReplRow& b) {
            return a.old_model().compare(b.old_model());
        },
        Gtk::Align::START, true, true, 0);

    add_text_column(
        table_view_, "New Model",
        [](const ObjReplRow& row) { return row.new_model(); },
        [](const ObjReplRow& a, const ObjReplRow& b) {
            return a.new_model().compare(b.new_model());
        },
        Gtk::Align::START, true, true, 0);

    add_int_column(
        table_view_, "Count",
        [](const ObjReplRow& row) { return row.count(); },
        [](const ObjReplRow& a, const ObjReplRow& b) {
            return a.count() - b.count();
        },
        Gtk::Align::END, 80);

    // Sort + selection model
    sort_model_ = Gtk::SortListModel::create(filter_model_, table_view_.get_sorter());
    table_selection_ = Gtk::SingleSelection::create(sort_model_);
    table_selection_->set_autoselect(false);
    table_selection_->set_can_unselect(true);
    table_view_.set_model(table_selection_);
    table_view_.set_show_column_separators(true);
    table_view_.set_show_row_separators(true);

    table_scroll_.set_child(table_view_);
    table_scroll_.set_vexpand(true);
    table_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    table_scroll_.set_propagate_natural_width(false);
    table_box_.append(table_scroll_);

    toolbar_and_table_paned_.set_start_child(toolbar_);
    toolbar_and_table_paned_.set_resize_start_child(true);
    toolbar_and_table_paned_.set_shrink_start_child(false);
    toolbar_and_table_paned_.set_end_child(table_box_);
    toolbar_and_table_paned_.set_resize_end_child(false);
    toolbar_and_table_paned_.set_shrink_end_child(false);
    toolbar_.set_size_request(400, 200);
    table_box_.set_size_request(800, 200);
    main_paned_.set_start_child(toolbar_and_table_paned_);
    main_paned_.set_resize_start_child(true);
    main_paned_.set_shrink_start_child(false);

    // Preview area
    status_label_.set_halign(Gtk::Align::START);
    status_label_.set_margin(4);
    status_label_.set_hexpand(true);
    preview_toolbar_.append(status_label_);
    auto_extract_textures_check_.set_tooltip_text("Extract missing model textures to drive root before preview");
    preview_toolbar_.append(auto_extract_textures_check_);
    sync_button_.set_tooltip_text("Synchronize camera rotation between old and new model");
    preview_toolbar_.append(sync_button_);
    preview_box_.append(preview_toolbar_);

    left_label_.set_halign(Gtk::Align::START);
    left_label_.set_margin(2);
    left_preview_box_.append(left_label_);
    left_preview_box_.append(left_model_panel_);
    left_model_panel_.set_vexpand(true);
    left_model_panel_.set_hexpand(true);

    right_label_.set_halign(Gtk::Align::START);
    right_label_.set_margin(2);
    right_preview_box_.append(right_label_);
    right_preview_box_.append(right_model_panel_);
    right_model_panel_.set_vexpand(true);
    right_model_panel_.set_hexpand(true);

    preview_paned_.set_start_child(left_preview_box_);
    preview_paned_.set_end_child(right_preview_box_);
    preview_paned_.set_resize_start_child(true);
    preview_paned_.set_resize_end_child(true);
    preview_paned_.set_shrink_start_child(false);
    preview_paned_.set_shrink_end_child(false);
    preview_paned_.set_vexpand(true);
    preview_box_.append(preview_paned_);

    main_paned_.set_end_child(preview_box_);
    main_paned_.set_resize_end_child(true);
    main_paned_.set_shrink_end_child(false);
    main_paned_.set_vexpand(true);

    append(main_paned_);

    // Set initial paned position after realization
    main_paned_.signal_realize().connect([this]() {
        Glib::signal_idle().connect_once([this]() {
            main_paned_.set_position(main_paned_.get_height() / 2);
        });
    });

    // Signals
    repl_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_repl_browse));
    repl_load_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_repl_load));
    wrp_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_wrp_browse));
    wrp_load_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_wrp_load));
    filter_entry_.signal_changed().connect(sigc::mem_fun(*this, &TabObjReplace::on_filter_changed));
    set_unmatched_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabObjReplace::on_set_unmatched_to));
    auto_match_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &TabObjReplace::on_auto_match));
    save_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_save));
    save_as_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabObjReplace::on_save_as));

    // Camera sync toggle
    sync_button_.signal_toggled().connect(
        sigc::mem_fun(*this, &TabObjReplace::on_sync_toggled));

    // Selection change -> preview
    table_selection_->property_selected().signal_changed().connect(
        sigc::mem_fun(*this, &TabObjReplace::on_selection_changed));

    // Double-click / Enter -> edit dialog
    table_view_.signal_activate().connect(
        sigc::mem_fun(*this, &TabObjReplace::on_table_activate));

    // Disable save buttons initially
    save_button_.set_sensitive(false);
    save_as_button_.set_sensitive(false);

    auto_extract_textures_check_.signal_toggled().connect([this]() {
        if (!cfg_) return;
        cfg_->obj_replace_defaults.auto_extract_textures = auto_extract_textures_check_.get_active();
        save_config(*cfg_);
    });
}

TabObjReplace::~TabObjReplace() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    loading_ = false;
    if (worker_.joinable()) worker_.join();
    if (auto_extract_thread_.joinable()) auto_extract_thread_.join();
}

void TabObjReplace::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabObjReplace::set_model_loader_service(
    const std::shared_ptr<P3dModelLoaderService>& service) {
    model_loader_shared_ = service;
    left_model_panel_.set_model_loader_service(service);
    right_model_panel_.set_model_loader_service(service);
}

void TabObjReplace::set_texture_loader_service(
    const std::shared_ptr<LodTexturesLoaderService>& service) {
    texture_loader_shared_ = service;
    left_model_panel_.set_texture_loader_service(service);
    right_model_panel_.set_texture_loader_service(service);
}

void TabObjReplace::set_config(Config* cfg) {
    cfg_ = cfg;
    db_.reset();
    index_.reset();

    left_model_panel_.set_config(cfg_);
    left_model_panel_.set_pboindex(nullptr, nullptr);
    left_model_panel_.set_model_loader_service(model_loader_shared_);
    left_model_panel_.set_texture_loader_service(texture_loader_shared_);
    right_model_panel_.set_config(cfg_);
    right_model_panel_.set_pboindex(nullptr, nullptr);
    right_model_panel_.set_model_loader_service(model_loader_shared_);
    right_model_panel_.set_texture_loader_service(texture_loader_shared_);

    // Restore last-used paths
    if (cfg_) {
        if (!cfg_->obj_replace_defaults.last_replacement_file.empty())
            repl_entry_.set_text(cfg_->obj_replace_defaults.last_replacement_file);
        if (!cfg_->obj_replace_defaults.last_wrp_file.empty())
            wrp_entry_.set_text(cfg_->obj_replace_defaults.last_wrp_file);
        auto_extract_textures_check_.set_active(cfg_->obj_replace_defaults.auto_extract_textures);
    }

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
        index_ = snap.index;
        left_model_panel_.set_pboindex(db_.get(), index_.get());
        right_model_panel_.set_pboindex(db_.get(), index_.get());
        if (!snap.error.empty()) {
            app_log(LogLevel::Warning, "ObjReplace: Failed to open PBO index: " + snap.error);
        } else if (db_ && index_) {
            app_log(LogLevel::Info, "ObjReplace: PBO index loaded ("
                    + std::to_string(snap.prefix_count) + " prefixes)");
        }
    });
}

ObjReplEntry* TabObjReplace::entry_from_id(uint64_t id) {
    auto it = entry_index_by_id_.find(id);
    if (it == entry_index_by_id_.end()) return nullptr;
    if (it->second >= entries_.size()) return nullptr;
    return &entries_[it->second];
}

const ObjReplEntry* TabObjReplace::entry_from_id(uint64_t id) const {
    auto it = entry_index_by_id_.find(id);
    if (it == entry_index_by_id_.end()) return nullptr;
    if (it->second >= entries_.size()) return nullptr;
    return &entries_[it->second];
}

uint64_t TabObjReplace::next_entry_id() {
    return next_entry_id_++;
}

void TabObjReplace::rebuild_entry_index() {
    entry_index_by_id_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        entry_index_by_id_[entries_[i].id] = i;
    }
}

// -- Unsaved changes confirmation --

void TabObjReplace::check_unsaved_changes(std::function<void()> proceed_callback) {
    if (!dirty_) {
        proceed_callback();
        return;
    }

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    if (!window) {
        proceed_callback();
        return;
    }

    auto* dialog = new Gtk::Window();
    dialog->set_title("Unsaved Changes");
    dialog->set_transient_for(*window);
    dialog->set_modal(true);
    dialog->set_default_size(400, -1);
    dialog->set_resizable(false);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    vbox->set_margin(16);

    auto* msg_label = Gtk::make_managed<Gtk::Label>("You have unsaved changes.\nSave changes before loading?");
    msg_label->set_halign(Gtk::Align::START);
    msg_label->set_wrap(true);
    vbox->append(*msg_label);

    auto* button_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    button_box->set_halign(Gtk::Align::END);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    auto* discard_btn = Gtk::make_managed<Gtk::Button>("Discard");
    auto* save_btn = Gtk::make_managed<Gtk::Button>("Save");
    save_btn->add_css_class("suggested-action");
    discard_btn->add_css_class("destructive-action");

    button_box->append(*cancel_btn);
    button_box->append(*discard_btn);
    button_box->append(*save_btn);
    vbox->append(*button_box);

    dialog->set_child(*vbox);

    auto dialog_closed = std::make_shared<std::atomic<bool>>(false);
    auto close_dialog = [dialog, dialog_closed]() {
        if (dialog_closed->exchange(true)) return;
        dialog->close();
        Glib::signal_idle().connect_once([dialog]() { delete dialog; });
    };
    dialog->signal_close_request().connect([close_dialog]() {
        close_dialog();
        return false;
    }, false);

    cancel_btn->signal_clicked().connect(close_dialog);

    discard_btn->signal_clicked().connect([close_dialog, proceed_callback]() {
        close_dialog();
        proceed_callback();
    });

    save_btn->signal_clicked().connect([this, close_dialog, proceed_callback]() {
        close_dialog();
        on_save();
        proceed_callback();
    });

    dialog->set_hide_on_close(true);
    dialog->present();
}

// -- File dialogs --

void TabObjReplace::on_repl_browse() {
    auto browse_action = [this]() {
        auto dialog = Gtk::FileDialog::create();
        auto filter = Gtk::FileFilter::create();
        filter->set_name("TSV files");
        filter->add_pattern("*.tsv");
        filter->add_pattern("*.txt");
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
                        repl_entry_.set_text(file->get_path());
                        load_replacement_file(file->get_path());
                    }
                } catch (const std::exception& e) {
                    log_async_dialog_error("replacement file open", e);
                } catch (...) {
                    log_async_dialog_error("replacement file open");
                }
            });
    };

    check_unsaved_changes(browse_action);
}

void TabObjReplace::on_repl_load() {
    auto path = std::string(repl_entry_.get_text());
    if (path.empty()) return;

    check_unsaved_changes([this, path]() {
        load_replacement_file(path);
    });
}

void TabObjReplace::on_wrp_browse() {
    auto browse_action = [this]() {
        auto dialog = Gtk::FileDialog::create();
        auto filter = Gtk::FileFilter::create();
        filter->set_name("WRP files");
        filter->add_pattern("*.wrp");
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
                        wrp_entry_.set_text(file->get_path());
                        load_wrp_file(file->get_path());
                    }
                } catch (const std::exception& e) {
                    log_async_dialog_error("WRP file open", e);
                } catch (...) {
                    log_async_dialog_error("WRP file open");
                }
            });
    };

    check_unsaved_changes(browse_action);
}

void TabObjReplace::on_wrp_load() {
    auto path = std::string(wrp_entry_.get_text());
    if (path.empty()) return;

    check_unsaved_changes([this, path]() {
        load_wrp_file(path);
    });
}

// -- Replacement file I/O --

void TabObjReplace::load_replacement_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        app_log(LogLevel::Error, "Cannot open replacement file: " + path);
        return;
    }

    entries_.clear();
    entry_index_by_id_.clear();
    next_entry_id_ = 1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;

        ObjReplEntry entry;
        entry.id = next_entry_id();
        entry.old_model = trim_copy(line.substr(0, tab));
        entry.new_model = trim_copy(line.substr(tab + 1));

        if (!entry.old_model.empty())
            entries_.push_back(std::move(entry));
    }

    current_file_ = path;
    dirty_ = false;
    save_button_.set_sensitive(true);
    save_as_button_.set_sensitive(true);

    if (cfg_) {
        cfg_->obj_replace_defaults.last_replacement_file = path;
        save_config(*cfg_);
    }

    int matched = 0, unmatched = 0, multi = 0;
    for (const auto& e : entries_) {
        if (e.is_multi_match()) multi++;
        else if (e.is_matched()) matched++;
        else unmatched++;
    }
    app_log(LogLevel::Info, "Loaded " + std::to_string(entries_.size())
            + " entries from " + path
            + " (" + std::to_string(matched) + " matched, "
            + std::to_string(unmatched) + " unmatched"
            + (multi > 0 ? ", " + std::to_string(multi) + " multi-match" : "")
            + ")");

    refresh_all();
}

void TabObjReplace::save_replacement_file(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        app_log(LogLevel::Error, "Cannot write replacement file: " + path);
        return;
    }

    f << "# Object replacement mapping\n";
    for (const auto& e : entries_) {
        f << e.old_model << "\t" << e.new_model << "\n";
    }

    current_file_ = path;
    dirty_ = false;

    if (cfg_) {
        cfg_->obj_replace_defaults.last_replacement_file = path;
        save_config(*cfg_);
    }

    app_log(LogLevel::Info, "Saved " + std::to_string(entries_.size())
            + " entries to " + path);
    update_status_label();
}

void TabObjReplace::on_save() {
    if (current_file_.empty()) {
        on_save_as();
        return;
    }
    save_replacement_file(current_file_);
}

void TabObjReplace::on_save_as() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("TSV files");
    filter->add_pattern("*.tsv");
    filter->add_pattern("*.txt");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (file) {
                    save_replacement_file(file->get_path());
                }
            } catch (const std::exception& e) {
                log_async_dialog_error("replacement file save", e);
            } catch (...) {
                log_async_dialog_error("replacement file save");
            }
        });
}

// -- WRP loading --

void TabObjReplace::load_wrp_file(const std::string& path) {
    if (loading_) return;
    loading_ = true;

    app_log(LogLevel::Info, "Loading WRP for object counts: " + path);
    status_label_.set_text("Loading WRP...");

    if (worker_.joinable()) worker_.join();

    worker_ = std::thread([this, path]() {
        std::map<std::string, int> counts;
        std::string error;

        try {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) {
                error = "Cannot open WRP file: " + path;
            } else {
                armatools::wrp::Options opts;
                auto wd = armatools::wrp::read(f, opts);

                for (const auto& obj : wd.objects) {
                    auto key = armatools::armapath::to_slash_lower(obj.model_name);
                    counts[key]++;
                }
            }
        } catch (const std::exception& e) {
            error = std::string("WRP parse error: ") + e.what();
        }

        Glib::signal_idle().connect_once([this, counts = std::move(counts),
                                          error = std::move(error), path]() {
            if (!error.empty()) {
                app_log(LogLevel::Error, error);
                status_label_.set_text(error);
                loading_ = false;
                return;
            }

            std::map<std::string, size_t> existing;
            for (size_t i = 0; i < entries_.size(); ++i) {
                auto key = armatools::armapath::to_slash_lower(entries_[i].old_model);
                existing[key] = i;
            }

            int updated = 0, added = 0;
            for (const auto& [model, count] : counts) {
                auto it = existing.find(model);
                if (it != existing.end()) {
                    entries_[it->second].count = count;
                    updated++;
                } else {
                    ObjReplEntry entry;
                    entry.id = next_entry_id();
                    entry.old_model = model;
                    entry.new_model = "unmatched";
                    entry.count = count;
                    entries_.push_back(std::move(entry));
                    added++;
                }
            }

            if (added > 0) dirty_ = true;

            if (cfg_) {
                cfg_->obj_replace_defaults.last_wrp_file = path;
                save_config(*cfg_);
            }

            app_log(LogLevel::Info, "WRP loaded: " + std::to_string(counts.size())
                    + " models, " + std::to_string(updated) + " updated, "
                    + std::to_string(added) + " new unmatched");

            refresh_all();
            loading_ = false;
        });
    });
}

// -- Batch operations --

void TabObjReplace::on_set_unmatched_to() {
    if (entries_.empty()) return;

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    if (!window) return;

    auto* dialog = new Gtk::Window();
    dialog->set_title("Set Unmatched To...");
    dialog->set_transient_for(*window);
    dialog->set_modal(true);
    dialog->set_default_size(450, -1);
    dialog->set_resizable(false);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    vbox->set_margin(16);

    auto* msg_label = Gtk::make_managed<Gtk::Label>(
        "Enter the new model path to assign to all unmatched entries:");
    msg_label->set_halign(Gtk::Align::START);
    msg_label->set_wrap(true);
    vbox->append(*msg_label);

    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("e.g. ca/buildings/placeholder.p3d");
    entry->set_hexpand(true);
    vbox->append(*entry);

    auto* button_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    button_box->set_halign(Gtk::Align::END);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    auto* apply_btn = Gtk::make_managed<Gtk::Button>("Apply");
    apply_btn->add_css_class("suggested-action");

    button_box->append(*cancel_btn);
    button_box->append(*apply_btn);
    vbox->append(*button_box);

    dialog->set_child(*vbox);

    auto dialog_closed = std::make_shared<std::atomic<bool>>(false);
    auto close_dialog = [dialog, dialog_closed]() {
        if (dialog_closed->exchange(true)) return;
        dialog->close();
        Glib::signal_idle().connect_once([dialog]() { delete dialog; });
    };
    dialog->signal_close_request().connect([close_dialog]() {
        close_dialog();
        return false;
    }, false);

    cancel_btn->signal_clicked().connect(close_dialog);

    apply_btn->signal_clicked().connect([this, close_dialog, entry]() {
        auto new_value = trim_copy(std::string(entry->get_text()));
        if (new_value.empty()) {
            close_dialog();
            return;
        }
        if (new_value.find(';') != std::string::npos ||
            new_value.find('\n') != std::string::npos ||
            new_value.find('\r') != std::string::npos) {
            app_log(LogLevel::Warning,
                    "Set Unmatched: invalid model path (contains ';' or newline)");
            return;
        }

        int changed = 0;
        for (auto& e : entries_) {
            if (!e.is_matched()) {
                e.new_model = new_value;
                changed++;
            }
        }

        if (changed > 0) {
            dirty_ = true;
            app_log(LogLevel::Info, "Set " + std::to_string(changed)
                    + " unmatched entries to: " + new_value);
            refresh_all();
        }

        close_dialog();
    });

    dialog->set_hide_on_close(true);
    dialog->present();
}

void TabObjReplace::on_auto_match() {
    if (entries_.empty() || !db_) {
        app_log(LogLevel::Warning, "Auto-Match requires a PBO index database");
        return;
    }
    if (loading_) return;
    loading_ = true;

    auto_match_button_.set_sensitive(false);
    status_label_.set_text("Auto-matching...");

    // Collect strictly unmatched entries (skip matched and multi-match)
    struct AutoMatchWork {
        uint64_t id;
        std::string old_path; // normalized old model path
        std::string filename; // just the filename for searching
    };
    std::vector<AutoMatchWork> work;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].is_matched() || entries_[i].is_multi_match()) continue;
        auto normalized = armatools::armapath::to_slash_lower(entries_[i].old_model);
        auto filename = fs::path(normalized).filename().string();
        if (!filename.empty())
            work.push_back({entries_[i].id, std::move(normalized), std::move(filename)});
    }

    if (work.empty()) {
        app_log(LogLevel::Info, "Auto-Match: no unmatched entries to process");
        auto_match_button_.set_sensitive(true);
        loading_ = false;
        update_status_label();
        return;
    }

    if (worker_.joinable()) worker_.join();

    auto total_work = work.size();
    status_label_.set_text("Auto-matching 0/" + std::to_string(total_work) + "...");

    auto db = db_;
    worker_ = std::thread([this, db = std::move(db), work = std::move(work), total_work]() {
        if (!db) {
            Glib::signal_idle().connect_once([this]() {
                app_log(LogLevel::Warning, "Auto-Match cancelled: PBO index is not available");
                auto_match_button_.set_sensitive(true);
                loading_ = false;
                update_status_label();
            });
            return;
        }

        // Run all DB queries on the worker thread.
        // Each match is: entry_index -> semicolon-joined candidate paths.
        // Single match: stored directly. Multiple matches: joined with ";".
        std::vector<std::pair<uint64_t, std::string>> matches;
        int single_count = 0, multi_count = 0;
        auto last_update = std::chrono::steady_clock::now();

        for (size_t wi = 0; wi < work.size(); ++wi) {
            if (!loading_) break;  // cancelled
            const auto& w = work[wi];

            // Throttled progress update (~10 Hz)
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_update).count() >= 100) {
                last_update = now;
                auto progress_wi = wi;
                auto matched_so_far = matches.size();
                Glib::signal_idle().connect_once(
                    [this, progress_wi, total_work, matched_so_far]() {
                        status_label_.set_text(
                            "Auto-matching " + std::to_string(progress_wi + 1) + "/"
                            + std::to_string(total_work)
                            + " (" + std::to_string(matched_so_far) + " matched)...");
                    });
            }

            std::vector<armatools::pboindex::FindResult> results;
            try {
                results = db->find_files("*" + w.filename);
            } catch (const std::exception& e) {
                auto msg = "Auto-Match: lookup failed for '" + w.filename + "': " + e.what();
                Glib::signal_idle().connect_once([msg]() {
                    app_log(LogLevel::Warning, msg);
                });
                continue;
            } catch (...) {
                auto msg = "Auto-Match: lookup failed for '" + w.filename + "'";
                Glib::signal_idle().connect_once([msg]() {
                    app_log(LogLevel::Warning, msg);
                });
                continue;
            }

            // Collect all non-self candidates
            std::vector<std::string> candidates;
            for (const auto& r : results) {
                auto file_path = armatools::armapath::to_slash_lower(r.file_path);
                auto prefix = armatools::armapath::to_slash_lower(r.prefix);
                auto candidate = prefix.empty() ? file_path : prefix + "/" + file_path;
                // Skip self-matches
                if (candidate == w.old_path) continue;
                if (candidate.ends_with("/" + w.old_path)) continue;
                if (w.old_path.ends_with("/" + candidate)) continue;
                if (file_path == w.old_path) continue;
                candidates.push_back(std::move(candidate));
            }

            if (candidates.empty()) {
                if (!results.empty()) {
                    auto msg = "Auto-Match: all " + std::to_string(results.size())
                               + " results for '" + w.filename + "' were self-matches"
                               + " (old: " + w.old_path + ")";
                    Glib::signal_idle().connect_once([msg]() {
                        app_log(LogLevel::Debug, msg);
                    });
                }
                continue;
            }

            // Join all candidates with ";"
            std::string joined;
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (i > 0) joined += ';';
                joined += candidates[i];
            }

            if (candidates.size() == 1) single_count++;
            else multi_count++;

            matches.emplace_back(w.id, std::move(joined));
        }

        Glib::signal_idle().connect_once(
            [this, matches = std::move(matches), single_count, multi_count]() {
                for (const auto& [id, new_model] : matches) {
                    auto* entry = entry_from_id(id);
                    if (entry && !entry->is_matched()) {
                        entry->new_model = new_model;
                    }
                }

                if (!matches.empty()) {
                    dirty_ = true;
                    std::string msg = "Auto-Match: " + std::to_string(matches.size())
                                      + " entries matched";
                    if (multi_count > 0)
                        msg += " (" + std::to_string(multi_count)
                               + " with multiple candidates — use Edit to select)";
                    app_log(LogLevel::Info, msg);
                    refresh_all();
                } else {
                    app_log(LogLevel::Info, "Auto-Match: no new matches found");
                    update_status_label();
                }

                auto_match_button_.set_sensitive(true);
                loading_ = false;
            });
    });
}

// -- Table management --

void TabObjReplace::rebuild_model() {
    table_model_->remove_all();
    rebuild_entry_index();
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        table_model_->append(ObjReplRow::create(
            entry.id,
            static_cast<uint32_t>(i + 1),
            entry.old_model,
            entry.new_model,
            entry.count));
    }
}

void TabObjReplace::refresh_all() {
    rebuild_model();
    update_status_label();
}

void TabObjReplace::update_status_label() {
    int matched = 0, unmatched = 0, total_count = 0;
    for (const auto& e : entries_) {
        if (e.is_matched()) matched++;
        else unmatched++;
        total_count += e.count;
    }
    std::ostringstream ss;
    ss << entries_.size() << " entries, "
       << matched << " matched, "
       << unmatched << " unmatched";
    if (total_count > 0)
        ss << ", " << total_count << " total instances";
    if (dirty_)
        ss << " [modified]";
    status_label_.set_text(ss.str());
}

void TabObjReplace::on_filter_changed() {
    gtk_filter_changed(GTK_FILTER(table_filter_c_), GTK_FILTER_CHANGE_DIFFERENT);
}

// -- Row interaction --

void TabObjReplace::on_selection_changed() {
    auto pos = table_selection_->get_selected();
    if (pos == GTK_INVALID_LIST_POSITION) return;

    auto item = sort_model_->get_object(pos);
    auto row = std::dynamic_pointer_cast<ObjReplRow>(item);
    if (!row) return;
    show_preview(row->old_model(), row->new_model());
}

void TabObjReplace::on_table_activate(guint position) {
    auto item = sort_model_->get_object(position);
    auto row = std::dynamic_pointer_cast<ObjReplRow>(item);
    if (!row) return;
    show_edit_dialog(row->id());
}

// -- Preview --

void TabObjReplace::show_preview(const std::string& old_model,
                                  const std::string& new_model) {
    load_p3d_into_panel(left_model_panel_, left_label_, old_model);

    if (new_model.empty() || !is_model_matched(new_model)) {
        right_model_panel_.clear();
        right_label_.set_text("(unmatched)");
    } else {
        load_p3d_into_panel(right_model_panel_, right_label_, new_model);
    }
}

void TabObjReplace::start_auto_extract_worker(std::set<std::string> textures,
                                              std::string drive_root) {
    if (textures.empty()) {
        auto_extract_busy_ = false;
        return;
    }
    if (!db_) {
        auto_extract_busy_ = false;
        status_label_.set_text("Auto-extract skipped: A3DB not loaded.");
        return;
    }

    if (auto_extract_thread_.joinable()) auto_extract_thread_.join();

    auto_extract_busy_ = true;
    auto db = db_;
    auto_extract_thread_ = std::thread([this, db = std::move(db), drive_root = std::move(drive_root),
                                        textures = std::move(textures)]() mutable {
        if (!db) {
            Glib::signal_idle().connect_once([this]() {
                status_label_.set_text("Auto-extract skipped: A3DB not loaded.");
                auto_extract_busy_ = false;
            });
            return;
        }
        const auto stats = extract_textures_to_drive(*db, drive_root, textures);
        Glib::signal_idle().connect_once([this, stats, drive_root]() {
            std::set<std::string> pending;
            std::string pending_drive_root;
            {
                std::lock_guard<std::mutex> lock(auto_extract_mutex_);
                pending.swap(auto_extract_pending_textures_);
                pending_drive_root = auto_extract_pending_drive_root_;
                auto_extract_pending_drive_root_.clear();
            }

            std::ostringstream ss;
            ss << "Auto-extract: " << stats.extracted << " extracted, "
               << stats.existing << " existing, "
               << stats.missing << " missing, "
               << stats.failed << " failed.";
            status_label_.set_text(ss.str());
            app_log(LogLevel::Info, "ObjReplace " + ss.str());

            if (!pending.empty()) {
                status_label_.set_text("Auto-extracting queued textures...");
                start_auto_extract_worker(std::move(pending),
                                          pending_drive_root.empty() ? drive_root
                                                                     : pending_drive_root);
                return;
            }
            auto_extract_busy_ = false;
        });
    });
}

void TabObjReplace::enqueue_auto_extract_textures(const std::set<std::string>& textures) {
    if (textures.empty()) return;
    if (!cfg_ || cfg_->drive_root.empty()) {
        status_label_.set_text("Auto-extract skipped: drive_root not configured.");
        return;
    }

    const auto drive_root = cfg_->drive_root;
    bool start_now = false;
    {
        std::lock_guard<std::mutex> lock(auto_extract_mutex_);
        if (auto_extract_busy_.load()) {
            auto_extract_pending_textures_.insert(textures.begin(), textures.end());
            auto_extract_pending_drive_root_ = drive_root;
        } else {
            start_now = true;
        }
    }

    if (!start_now) {
        status_label_.set_text("Auto-extract queued...");
        return;
    }

    status_label_.set_text("Auto-extracting textures...");
    start_auto_extract_worker(std::set<std::string>(textures.begin(), textures.end()),
                              drive_root);
}

void TabObjReplace::load_p3d_into_panel(ModelViewPanel& panel, Gtk::Label& label,
                                         const std::string& model_path) {
    panel.clear();
    label.set_text(model_path);

    if (model_path.empty()) return;
    if (!model_loader_shared_) {
        app_log(LogLevel::Warning, "ObjReplace: model loader service not configured");
        label.set_text(model_path + " (model loader not configured)");
        return;
    }

    auto maybe_auto_extract = [this](const auto& p3d) {
        if (!auto_extract_textures_check_.get_active()) return;

        std::set<std::string> textures;
        for (const auto& lod : p3d.lods) {
            for (const auto& t : lod.textures) textures.insert(t);
        }
        enqueue_auto_extract_textures(textures);
    };
    try {
        auto p3d = model_loader_shared_->load_p3d(model_path);
        maybe_auto_extract(p3d);
        panel.load_p3d(model_path);
    } catch (const std::exception& e) {
        app_log(LogLevel::Warning, "ObjReplace preview load failed: " + std::string(e.what()));
        label.set_text(model_path + " (not found)");
    }
}

// -- Camera sync --

void TabObjReplace::on_sync_toggled() {
    left_cam_conn_.disconnect();
    right_cam_conn_.disconnect();

    if (sync_button_.get_active()) {
        left_cam_conn_ = left_model_panel_.gl_view().signal_camera_changed().connect(
            sigc::mem_fun(*this, &TabObjReplace::sync_camera_left_to_right));
        right_cam_conn_ = right_model_panel_.gl_view().signal_camera_changed().connect(
            sigc::mem_fun(*this, &TabObjReplace::sync_camera_right_to_left));
    }
}

void TabObjReplace::sync_camera_left_to_right() {
    right_model_panel_.gl_view().set_camera_state(
        left_model_panel_.gl_view().get_camera_state());
}

void TabObjReplace::sync_camera_right_to_left() {
    left_model_panel_.gl_view().set_camera_state(
        right_model_panel_.gl_view().get_camera_state());
}

// -- Edit dialog (comprehensive browser with 3D preview) --

void TabObjReplace::show_edit_dialog(uint64_t row_id) {
    const auto* entry = entry_from_id(row_id);
    if (!entry) return;
    const auto entry_snapshot = *entry;

    auto* parent_window = dynamic_cast<Gtk::Window*>(get_root());
    if (!parent_window) return;

    // -- Dialog state (shared via shared_ptr so lambdas can capture it) --
    struct DialogState {
        std::string current_path;
        std::string current_source;
        std::string selected_p3d_path; // full virtual path of selected .p3d
        std::vector<armatools::pboindex::DirEntry> dir_entries;
        std::vector<armatools::pboindex::FindResult> search_results;
        bool showing_search = false;
        std::jthread nav_thread;
        std::jthread search_thread;
        std::atomic<unsigned> search_gen{0};
        std::atomic<unsigned> nav_gen{0};
        std::atomic<unsigned> preview_gen{0};
        std::atomic<bool> alive{true}; // set to false when dialog closes
    };
    auto state = std::make_shared<DialogState>();

    auto* dialog = new Gtk::Window();
    dialog->set_title("Edit Replacement: " + entry_snapshot.old_model);
    dialog->set_transient_for(*parent_window);
    dialog->set_modal(true);
    dialog->set_default_size(1200, 800);

    // === Top-level layout: vertical box with paned + button bar ===
    auto* root_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    // === Main paned: left (old preview) | right (browser + new preview) ===
    auto* main_paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
    main_paned->set_vexpand(true);
    main_paned->set_wide_handle(true);

    // --- Left half: old model preview ---
    auto* left_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    left_box->set_margin(8);

    auto* old_title = Gtk::make_managed<Gtk::Label>("Old Model");
    old_title->set_halign(Gtk::Align::START);
    old_title->add_css_class("heading");
    left_box->append(*old_title);

    auto* old_panel = Gtk::make_managed<ModelViewPanel>();
    old_panel->set_config(cfg_);
    old_panel->set_pboindex(db_.get(), index_.get());
    old_panel->set_model_loader_service(model_loader_shared_);
    old_panel->set_texture_loader_service(texture_loader_shared_);
    old_panel->set_vexpand(true);
    old_panel->set_hexpand(true);
    left_box->append(*old_panel);

    auto* old_path_label = Gtk::make_managed<Gtk::Label>(entry_snapshot.old_model);
    old_path_label->set_halign(Gtk::Align::START);
    old_path_label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    old_path_label->set_selectable(true);
    left_box->append(*old_path_label);

    main_paned->set_start_child(*left_box);
    main_paned->set_resize_start_child(true);
    main_paned->set_shrink_start_child(false);

    // --- Right half: browser (top) + new model preview (bottom) ---
    auto* right_paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::VERTICAL);
    right_paned->set_wide_handle(true);

    // -- Right top: P3D browser --
    auto* browser_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    browser_box->set_margin(8);

    // Search bar
    auto* search_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* search_label = Gtk::make_managed<Gtk::Label>("Search:");
    auto* search_entry = Gtk::make_managed<Gtk::Entry>();
    search_entry->set_hexpand(true);
    search_entry->set_placeholder_text("Search *.p3d files...");
    auto* search_btn = Gtk::make_managed<Gtk::Button>("Search");
    auto* clear_btn = Gtk::make_managed<Gtk::Button>("Clear");
    search_row->append(*search_label);
    search_row->append(*search_entry);
    search_row->append(*search_btn);
    search_row->append(*clear_btn);
    browser_box->append(*search_row);

    // Source filter
    auto* source_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* source_label = Gtk::make_managed<Gtk::Label>("Source:");
    auto* source_combo = Gtk::make_managed<Gtk::ComboBoxText>();
    source_combo->append("", "All");
    if (db_) {
        static const std::unordered_map<std::string, std::string> source_labels = {
            {"arma3", "Arma 3"}, {"workshop", "Workshop"}, {"ofp", "OFP/CWA"},
            {"arma1", "Arma 1"}, {"arma2", "Arma 2"}, {"custom", "Custom"},
        };
        auto sources = db_->query_sources();
        for (const auto& src : sources) {
            auto it = source_labels.find(src);
            source_combo->append(src, it != source_labels.end() ? it->second : src);
        }
    }
    source_combo->set_active_id("");
    source_combo->set_hexpand(true);
    source_row->append(*source_label);
    source_row->append(*source_combo);
    browser_box->append(*source_row);

    // Breadcrumb
    auto* breadcrumb = Gtk::make_managed<Gtk::Label>("/");
    breadcrumb->set_halign(Gtk::Align::START);
    breadcrumb->set_ellipsize(Pango::EllipsizeMode::END);
    browser_box->append(*breadcrumb);

    // Directory list
    auto* list_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    auto* dir_list = Gtk::make_managed<Gtk::ListBox>();
    list_scroll->set_child(*dir_list);
    list_scroll->set_vexpand(true);
    browser_box->append(*list_scroll);

    auto* browser_status = Gtk::make_managed<Gtk::Label>("");
    browser_status->set_halign(Gtk::Align::START);
    browser_box->append(*browser_status);

    right_paned->set_start_child(*browser_box);
    right_paned->set_resize_start_child(true);
    right_paned->set_shrink_start_child(false);

    // -- Right bottom: new model preview --
    auto* new_preview_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    new_preview_box->set_margin(8);

    auto* new_title = Gtk::make_managed<Gtk::Label>("New Model");
    new_title->set_halign(Gtk::Align::START);
    new_title->add_css_class("heading");
    new_preview_box->append(*new_title);

    // Combo for multi-match selection (hidden by default)
    auto* match_combo = Gtk::make_managed<Gtk::ComboBoxText>();
    match_combo->set_visible(false);
    match_combo->set_tooltip_text("Select replacement model from multiple candidates");
    new_preview_box->append(*match_combo);

    auto* new_panel = Gtk::make_managed<ModelViewPanel>();
    new_panel->set_config(cfg_);
    new_panel->set_pboindex(db_.get(), index_.get());
    new_panel->set_model_loader_service(model_loader_shared_);
    new_panel->set_texture_loader_service(texture_loader_shared_);
    new_panel->set_vexpand(true);
    new_panel->set_hexpand(true);
    new_preview_box->append(*new_panel);

    auto* new_path_label = Gtk::make_managed<Gtk::Label>(
        entry_snapshot.is_matched() ? entry_snapshot.new_model : "(unmatched)");
    new_path_label->set_halign(Gtk::Align::START);
    new_path_label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    new_path_label->set_selectable(true);
    new_preview_box->append(*new_path_label);

    right_paned->set_end_child(*new_preview_box);
    right_paned->set_resize_end_child(true);
    right_paned->set_shrink_end_child(false);

    main_paned->set_end_child(*right_paned);
    main_paned->set_resize_end_child(true);
    main_paned->set_shrink_end_child(false);

    root_box->append(*main_paned);

    // === Bottom button bar ===
    auto* button_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_margin(8);

    auto* unmatched_btn = Gtk::make_managed<Gtk::Button>("Set Unmatched");
    unmatched_btn->add_css_class("destructive-action");
    button_bar->append(*unmatched_btn);

    // Camera sync toggle
    auto* sync_btn = Gtk::make_managed<Gtk::ToggleButton>("Sync Cameras");
    sync_btn->set_tooltip_text("Synchronize camera rotation between old and new preview");
    button_bar->append(*sync_btn);

    // Spacer
    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    button_bar->append(*spacer);

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
    auto* apply_btn = Gtk::make_managed<Gtk::Button>("Apply Match");
    apply_btn->add_css_class("suggested-action");
    apply_btn->set_sensitive(false);

    button_bar->append(*cancel_btn);
    button_bar->append(*apply_btn);
    root_box->append(*button_bar);

    dialog->set_child(*root_box);

    // === Helper lambdas ===

    // Populate the ListBox from dir_entries
    auto populate_list = [state, dir_list, breadcrumb]() {
        dir_list->set_visible(false);
        dir_list->unselect_all();
        while (auto* row = dir_list->get_row_at_index(0))
            dir_list->remove(*row);

        // ".." entry if not at root
        if (!state->current_path.empty()) {
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name("go-up-symbolic");
            auto* label = Gtk::make_managed<Gtk::Label>("..");
            label->set_halign(Gtk::Align::START);
            box->append(*icon);
            box->append(*label);
            dir_list->append(*box);
        }

        for (const auto& de : state->dir_entries) {
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            auto* icon = Gtk::make_managed<Gtk::Image>();
            if (de.is_dir) {
                icon->set_from_icon_name("folder-symbolic");
            } else {
                auto ext = fs::path(de.name).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                icon->set_from_icon_name(
                    ext == ".p3d" ? "emblem-system-symbolic" : "text-x-generic-symbolic");
            }
            auto* label = Gtk::make_managed<Gtk::Label>(de.name);
            label->set_halign(Gtk::Align::START);
            label->set_hexpand(true);
            box->append(*icon);
            box->append(*label);

            if (!de.is_dir && !de.files.empty()) {
                auto sz = std::to_string(de.files[0].data_size) + " B";
                auto* size_label = Gtk::make_managed<Gtk::Label>(sz);
                size_label->add_css_class("dim-label");
                box->append(*size_label);
            }

            dir_list->append(*box);
        }

        breadcrumb->set_text(state->current_path.empty() ? "/" : state->current_path);
        dir_list->set_visible(true);
    };

    // Navigate to a directory path (async — DB query runs in background thread)
    std::function<void(const std::string&)> dialog_navigate;
    dialog_navigate = [this, state, populate_list, browser_status,
                       dir_list, breadcrumb](const std::string& path) {
        if (!cfg_ || cfg_->a3db_path.empty()) return;
        state->current_path = path;
        state->showing_search = false;
        state->search_results.clear();
        unsigned gen = ++state->nav_gen;

        // Clear list and show loading feedback
        dir_list->set_visible(false);
        dir_list->unselect_all();
        while (auto* row = dir_list->get_row_at_index(0))
            dir_list->remove(*row);
        dir_list->set_visible(true);
        breadcrumb->set_text(path.empty() ? "/" : path);
        browser_status->set_text("Loading...");

        auto db_path = cfg_->a3db_path;
        auto source = state->current_source;
        if (state->nav_thread.joinable()) {
            state->nav_thread.request_stop();
            state->nav_thread.detach();
        }
        state->nav_thread = std::jthread(
            [state, db_path, path, source, gen,
             populate_list, browser_status](std::stop_token st) {
            if (st.stop_requested()) return;
            try {
                auto db = armatools::pboindex::DB::open(db_path);
                if (st.stop_requested()) return;
                std::vector<armatools::pboindex::DirEntry> entries;
                if (source.empty()) {
                    entries = db.list_dir(path);
                } else {
                    entries = db.list_dir_for_source(path, source);
                }
                if (st.stop_requested()) return;
                Glib::signal_idle().connect_once(
                    [state, entries = std::move(entries), gen,
                     populate_list, browser_status]() {
                        if (!state->alive.load() || gen != state->nav_gen.load()) return;
                        state->dir_entries = std::move(entries);
                        populate_list();
                        browser_status->set_text(
                            std::to_string(state->dir_entries.size()) + " entries");
                    });
            } catch (const std::exception& e) {
                auto msg = std::string(e.what());
                Glib::signal_idle().connect_once(
                    [state, gen, browser_status, msg]() {
                        if (!state->alive.load() || gen != state->nav_gen.load()) return;
                        browser_status->set_text("Error: " + msg);
                    });
            }
        });
    };

    // Preview a P3D file in the new model panel (async — extract+parse in background)
    auto dialog_preview_p3d = [this, new_panel, new_path_label, state, apply_btn](
                                   const armatools::pboindex::FindResult& file) {
        auto full_path = armatools::armapath::to_slash_lower(file.prefix + "/" + file.file_path);
        state->selected_p3d_path = full_path;
        new_path_label->set_text(full_path);
        apply_btn->set_sensitive(true);
        unsigned gen = ++state->preview_gen;
        Glib::signal_idle().connect_once([state, gen, new_panel, full_path]() {
            if (!state->alive.load() || gen != state->preview_gen.load()) return;
            new_panel->load_p3d(full_path);
        });
    };

    // Show search results in ListBox
    auto show_search_results = [state, dir_list, breadcrumb, browser_status]() {
        dir_list->unselect_all();
        while (auto* row = dir_list->get_row_at_index(0))
            dir_list->remove(*row);

        state->current_path.clear();
        state->showing_search = true;
        breadcrumb->set_text("Search results: "
                             + std::to_string(state->search_results.size()) + " files");

        for (const auto& r : state->search_results) {
            auto display = r.prefix + "/" + r.file_path;
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name("emblem-system-symbolic");
            auto* label = Gtk::make_managed<Gtk::Label>(display);
            label->set_halign(Gtk::Align::START);
            label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            label->set_hexpand(true);
            box->append(*icon);
            box->append(*label);

            auto sz = std::to_string(r.data_size) + " B";
            auto* size_label = Gtk::make_managed<Gtk::Label>(sz);
            size_label->add_css_class("dim-label");
            box->append(*size_label);

            dir_list->append(*box);
        }

        browser_status->set_text(std::to_string(state->search_results.size()) + " results");
    };

    // Search for .p3d files
    auto dialog_search = [this, state, search_btn, browser_status,
                          show_search_results](const std::string& pattern) {
        if (!db_ || !cfg_ || cfg_->a3db_path.empty() || pattern.empty()) return;

        // Ensure pattern searches for .p3d files
        std::string search_pattern = pattern;
        {
            auto lower = search_pattern;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(".p3d") == std::string::npos)
                search_pattern += "*.p3d";
        }
        if (search_pattern.front() != '*')
            search_pattern = "*" + search_pattern;

        unsigned gen = ++state->search_gen;
        search_btn->set_sensitive(false);
        browser_status->set_text("Searching...");

        if (state->search_thread.joinable()) {
            state->search_thread.request_stop();
            state->search_thread.detach();
        }

        auto db_path = cfg_->a3db_path;
        auto source = state->current_source;
        state->search_thread = std::jthread(
            [state, db_path, search_pattern, source, gen,
             search_btn, browser_status, show_search_results](std::stop_token st) {
                if (st.stop_requested()) return;
                try {
                    auto db = armatools::pboindex::DB::open(db_path);
                    if (st.stop_requested()) return;
                    auto results = db.find_files(search_pattern, source);
                    if (st.stop_requested()) return;
                    Glib::signal_idle().connect_once(
                        [state, results = std::move(results), gen,
                         search_btn, browser_status, show_search_results]() {
                            if (gen != state->search_gen.load()) return;
                            state->search_results = std::move(results);
                            show_search_results();
                            search_btn->set_sensitive(true);
                        });
                } catch (const std::exception& e) {
                    auto msg = std::string(e.what());
                    Glib::signal_idle().connect_once(
                        [gen, state, search_btn, browser_status, msg]() {
                            if (gen != state->search_gen.load()) return;
                            browser_status->set_text("Search error: " + msg);
                            search_btn->set_sensitive(true);
                        });
                }
            });
    };

    // Row activated in list
    dir_list->signal_row_activated().connect(
        [this, state, dialog_navigate, dialog_preview_p3d](Gtk::ListBoxRow* row) {
            if (!row || !db_) return;
            int idx = row->get_index();

            // Search results mode
            if (state->showing_search) {
                auto si = static_cast<size_t>(idx);
                if (idx >= 0 && si < state->search_results.size()) {
                    auto ext = fs::path(state->search_results[si].file_path)
                                   .extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".p3d")
                        dialog_preview_p3d(state->search_results[si]);
                }
                return;
            }

            // ".." entry
            int offset = state->current_path.empty() ? 0 : 1;
            if (!state->current_path.empty() && idx == 0) {
                auto pos = state->current_path.rfind('/');
                dialog_navigate(pos == std::string::npos
                                    ? ""
                                    : state->current_path.substr(0, pos));
                return;
            }

            auto entry_idx = static_cast<size_t>(idx - offset);
            if (entry_idx >= state->dir_entries.size()) return;

            const auto& de = state->dir_entries[entry_idx];
            if (de.is_dir) {
                dialog_navigate(state->current_path.empty()
                                    ? de.name
                                    : state->current_path + "/" + de.name);
            } else if (!de.files.empty()) {
                auto ext = fs::path(de.name).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".p3d")
                    dialog_preview_p3d(de.files[0]);
            }
        });

    // Single-click selection: preview .p3d files
    dir_list->signal_selected_rows_changed().connect(
        [state, dir_list, dialog_preview_p3d]() {
            auto* row = dir_list->get_selected_row();
            if (!row) return;
            int idx = row->get_index();

            if (state->showing_search) {
                auto si = static_cast<size_t>(idx);
                if (idx >= 0 && si < state->search_results.size()) {
                    auto ext = fs::path(state->search_results[si].file_path)
                                   .extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".p3d")
                        dialog_preview_p3d(state->search_results[si]);
                }
                return;
            }

            int offset = state->current_path.empty() ? 0 : 1;
            auto entry_idx = static_cast<size_t>(idx - offset);
            if (entry_idx >= state->dir_entries.size()) return;

            const auto& de = state->dir_entries[entry_idx];
            if (!de.is_dir && !de.files.empty()) {
                auto ext = fs::path(de.name).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".p3d")
                    dialog_preview_p3d(de.files[0]);
            }
        });

    // Search signals
    search_btn->signal_clicked().connect([search_entry, dialog_search]() {
        dialog_search(std::string(search_entry->get_text()));
    });
    search_entry->signal_activate().connect([search_entry, dialog_search]() {
        dialog_search(std::string(search_entry->get_text()));
    });

    // Clear button: go back to directory browsing
    clear_btn->signal_clicked().connect([state, search_entry, dialog_navigate]() {
        search_entry->set_text("");
        dialog_navigate(state->current_path.empty() ? "" : state->current_path);
    });

    // Source combo change
    source_combo->signal_changed().connect([state, source_combo, dialog_navigate]() {
        state->current_source = std::string(source_combo->get_active_id());
        dialog_navigate("");
    });

    // Camera sync
    sigc::connection left_sync_conn, right_sync_conn;
    sync_btn->signal_toggled().connect(
        [sync_btn, old_panel, new_panel,
         left_sync_conn, right_sync_conn]() mutable {
            left_sync_conn.disconnect();
            right_sync_conn.disconnect();
            if (sync_btn->get_active()) {
                left_sync_conn = old_panel->gl_view().signal_camera_changed().connect(
                    [old_panel, new_panel]() {
                        new_panel->gl_view().set_camera_state(
                            old_panel->gl_view().get_camera_state());
                    });
                right_sync_conn = new_panel->gl_view().signal_camera_changed().connect(
                    [old_panel, new_panel]() {
                        old_panel->gl_view().set_camera_state(
                            new_panel->gl_view().get_camera_state());
                    });
            }
        });

    // === Action buttons ===
    auto close_dialog_guard = std::make_shared<std::atomic<bool>>(false);
    auto close_dialog = [dialog, state, close_dialog_guard]() {
        if (close_dialog_guard->exchange(true)) return;
        // Mark dialog as dead so background threads stop posting to widgets
        state->alive.store(false);
        ++state->search_gen;
        ++state->nav_gen;
        ++state->preview_gen;
        if (state->nav_thread.joinable()) {
            state->nav_thread.request_stop();
            state->nav_thread.detach();
        }
        if (state->search_thread.joinable()) {
            state->search_thread.request_stop();
            state->search_thread.detach();
        }
        dialog->close();
        Glib::signal_idle().connect_once([dialog]() { delete dialog; });
    };
    dialog->signal_close_request().connect([close_dialog]() {
        close_dialog();
        return false;
    }, false);

    cancel_btn->signal_clicked().connect(close_dialog);

    unmatched_btn->signal_clicked().connect(
        [this, row_id, close_dialog]() {
            auto* target_entry = entry_from_id(row_id);
            if (target_entry) {
                target_entry->new_model = "unmatched";
                dirty_ = true;
                refresh_all();
                // Also update the main preview
                on_selection_changed();
            }
            close_dialog();
        });

    apply_btn->signal_clicked().connect(
        [this, row_id, state, close_dialog]() {
            auto* target_entry = entry_from_id(row_id);
            if (target_entry && !state->selected_p3d_path.empty()) {
                target_entry->new_model = state->selected_p3d_path;
                dirty_ = true;
                refresh_all();
                on_selection_changed();
            }
            close_dialog();
        });

    dialog->set_hide_on_close(true);
    dialog->present();

    // === Load initial content (async to keep UI responsive) ===

    // Helper: load a P3D model into a panel in a background thread.
    // Uses the Index for resolution (same logic as load_p3d_into_panel but async).
    auto async_load_panel = [state](
            ModelViewPanel* panel, Gtk::Label* label,
            const std::string& model_path) {
        if (model_path.empty()) return;
        Glib::signal_idle().connect_once([state, panel, label, model_path]() {
            if (!state->alive.load()) return;
            panel->load_p3d(model_path);
            label->set_text(model_path);
        });
    };

    // Load old model preview (async)
    async_load_panel(old_panel, old_path_label, entry_snapshot.old_model);

    if (entry_snapshot.is_multi_match()) {
        // Multi-match: parse candidates from ";"-separated new_model
        std::vector<std::string> candidates;
        {
            std::istringstream ss(entry_snapshot.new_model);
            std::string token;
            while (std::getline(ss, token, ';')) {
                if (!token.empty())
                    candidates.push_back(token);
            }
        }

        // Resolve each candidate to a FindResult for preview
        state->showing_search = true;
        state->search_results.clear();
        for (const auto& c : candidates) {
            armatools::pboindex::FindResult fr;
            if (index_) {
                armatools::pboindex::ResolveResult rr;
                if (index_->resolve(c, rr)) {
                    fr.pbo_path = rr.pbo_path;
                    fr.prefix = rr.prefix;
                    fr.file_path = rr.entry_name;
                    state->search_results.push_back(std::move(fr));
                    continue;
                }
            }
            if (db_) {
                auto normalized = armatools::armapath::to_slash_lower(c);
                auto filename = fs::path(normalized).filename().string();
                std::vector<armatools::pboindex::FindResult> results;
                try {
                    results = db_->find_files("*" + filename);
                } catch (const std::exception& e) {
                    app_log(LogLevel::Warning,
                            "ObjReplace: candidate lookup failed for '" + filename + "': " + e.what());
                    continue;
                } catch (...) {
                    app_log(LogLevel::Warning,
                            "ObjReplace: candidate lookup failed for '" + filename + "'");
                    continue;
                }
                bool found = false;
                for (const auto& r : results) {
                    auto full = armatools::armapath::to_slash_lower(
                        r.prefix.empty() ? r.file_path : r.prefix + "/" + r.file_path);
                    if (full == normalized) {
                        state->search_results.push_back(r);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    fr.prefix = "";
                    fr.file_path = c;
                    state->search_results.push_back(std::move(fr));
                }
            }
        }

        // Display candidates in the browser list
        dir_list->unselect_all();
        while (auto* row = dir_list->get_row_at_index(0))
            dir_list->remove(*row);

        breadcrumb->set_text("Multiple matches — select one (" +
                             std::to_string(candidates.size()) + " candidates):");

        for (const auto& c : candidates) {
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name("emblem-system-symbolic");
            auto* label = Gtk::make_managed<Gtk::Label>(c);
            label->set_halign(Gtk::Align::START);
            label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
            label->set_hexpand(true);
            box->append(*icon);
            box->append(*label);
            dir_list->append(*box);
        }

        browser_status->set_text(std::to_string(candidates.size()) + " candidates");

        // Populate the combo box with candidates
        match_combo->remove_all();
        for (size_t i = 0; i < candidates.size(); ++i) {
            match_combo->append(std::to_string(i), candidates[i]);
        }
        match_combo->set_visible(true);

        // Wire combo change -> preview + select
        match_combo->signal_changed().connect(
            [state, match_combo, new_panel, new_path_label, apply_btn,
             dialog_preview_p3d]() {
                auto id = std::string(match_combo->get_active_id());
                if (id.empty()) return;
                size_t ci = 0;
                try {
                    ci = static_cast<size_t>(std::stoul(id));
                } catch (const std::exception&) {
                    apply_btn->set_sensitive(false);
                    return;
                }
                if (ci >= state->search_results.size()) return;
                dialog_preview_p3d(state->search_results[ci]);
            });

        // Select the first candidate
        if (!candidates.empty()) {
            match_combo->set_active_id("0");
            apply_btn->set_sensitive(true);
        }
    } else if (entry_snapshot.is_matched()) {
        // Single match: load preview and navigate to directory
        auto normalized = armatools::armapath::to_slash_lower(entry_snapshot.new_model);
        state->selected_p3d_path = normalized;
        apply_btn->set_sensitive(true);

        // Load new model preview (async)
        async_load_panel(new_panel, new_path_label, entry_snapshot.new_model);

        // Navigate browser to the directory of the current new_model
        auto slash_pos = normalized.rfind('/');
        if (slash_pos != std::string::npos) {
            dialog_navigate(normalized.substr(0, slash_pos));
        } else {
            dialog_navigate("");
        }
    } else {
        dialog_navigate("");
    }

    // Set initial paned positions after realization
    main_paned->signal_realize().connect([main_paned]() {
        Glib::signal_idle().connect_once([main_paned]() {
            main_paned->set_position(main_paned->get_width() / 3);
        });
    });
    right_paned->signal_realize().connect([right_paned]() {
        Glib::signal_idle().connect_once([right_paned]() {
            right_paned->set_position(right_paned->get_height() / 2);
        });
    });
}
