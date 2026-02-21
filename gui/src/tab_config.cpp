#include "tab_config.h"

#include <filesystem>

TabConfig::TabConfig() : Gtk::Box(Gtk::Orientation::VERTICAL, 8) {
    set_margin(8);

    build_general_tab();
    build_asset_tab();
    build_wrp_tab();
    build_binaries_tab();

    general_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    general_scroll_.set_child(general_box_);
    asset_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    asset_scroll_.set_child(asset_box_);
    wrp_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    wrp_scroll_.set_child(wrp_box_);
    binaries_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    binaries_scroll_.set_child(binaries_box_);

    notebook_.append_page(general_scroll_, "General");
    notebook_.append_page(asset_scroll_, "Asset Browser");
    notebook_.append_page(wrp_scroll_, "Wrp Project");
    notebook_.append_page(binaries_scroll_, "Binaries");
    notebook_.set_expand(true);

    append(notebook_);

    save_button_.set_halign(Gtk::Align::END);
    save_button_.set_margin_top(8);
    save_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabConfig::save_to_config));
    append(save_button_);
}

void TabConfig::set_config(Config* cfg) {
    cfg_ = cfg;
    populate_from_config();
}

void TabConfig::build_general_tab() {
    general_box_.set_margin(8);

    struct PathField { std::string label; std::string placeholder; bool is_file; };
    const std::vector<PathField> fields = {
        {"OFP/CWA Directory", "/path/to/ofp",            false},
        {"Arma 1 Directory",  "/path/to/arma1",          false},
        {"Arma 2 Directory",  "/path/to/arma2",          false},
        {"Arma 3 Directory",  "/path/to/arma3",          false},
        {"Workshop Directory","/path/to/workshop",       false},
        {"A3DB Path",         "/path/to/a3db.sqlite",    true},
        {"Worlds Directory",  "/path/to/worlds",         false},
        {"Project Debug Dir", "/path/to/debug/output",   false},
        {"Drive Root",        "/path/to/P/drive",        false},
        {"FFmpeg Path",       "/path/to/ffmpeg",         true},
    };
    for (const auto& f : fields) {
        auto row = std::unique_ptr<PathRow>(new PathRow());
        row->label.set_text(f.label);
        row->label.set_size_request(150, -1);
        row->label.set_xalign(1.0);
        row->entry.set_hexpand(true);
        row->entry.set_placeholder_text(f.placeholder);
        row->box.append(row->label);
        row->box.append(row->entry);
        row->box.append(row->browse);
        bool is_file = f.is_file;
        row->browse.signal_clicked().connect(
            [this, entry = &row->entry, is_file]() { on_browse_path(*entry, !is_file); });
        general_box_.append(row->box);
        path_rows_.push_back(std::move(row));
    }
}

void TabConfig::build_asset_tab() {
    asset_box_.set_margin(8);
    asset_box_.append(auto_derap_);
    asset_box_.append(on_demand_metadata_);
}

void TabConfig::build_wrp_tab() {
    wrp_box_.set_margin(8);

    const std::vector<std::pair<std::string, std::string>> wrp_fields = {
        {"Offset X", "200000"},
        {"Offset Z", "0"},
        {"Split", "10000"},
    };
    for (const auto& [lbl, placeholder] : wrp_fields) {
        auto row = std::unique_ptr<WrpRow>(new WrpRow());
        row->label.set_text(lbl);
        row->label.set_size_request(100, -1);
        row->label.set_xalign(1.0);
        row->entry.set_hexpand(true);
        row->entry.set_placeholder_text(placeholder);
        row->box.append(row->label);
        row->box.append(row->entry);
        wrp_box_.append(row->box);
        wrp_rows_.push_back(std::move(row));
    }

    // HM Scale combo
    auto hm_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto hm_label = Gtk::make_managed<Gtk::Label>("HM Scale");
    hm_label->set_size_request(100, -1);
    hm_label->set_xalign(1.0);
    hm_scale_combo_.append("1");
    hm_scale_combo_.append("2");
    hm_scale_combo_.append("4");
    hm_scale_combo_.append("8");
    hm_scale_combo_.append("16");
    hm_scale_combo_.set_active(0);
    hm_scale_combo_.set_hexpand(true);
    hm_box->append(*hm_label);
    hm_box->append(hm_scale_combo_);
    wrp_box_.append(*hm_box);

    auto hp_preset_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto hp_preset_label = Gtk::make_managed<Gtk::Label>("HP Preset");
    hp_preset_label->set_size_request(100, -1);
    hp_preset_label->set_xalign(1.0);
    heightpipe_preset_combo_.append("none");
    heightpipe_preset_combo_.append("sharp");
    heightpipe_preset_combo_.append("retain_detail");
    heightpipe_preset_combo_.append("terrain_16x");
    heightpipe_preset_combo_.set_active(3);
    heightpipe_preset_combo_.set_hexpand(true);
    hp_preset_box->append(*hp_preset_label);
    hp_preset_box->append(heightpipe_preset_combo_);
    wrp_box_.append(*hp_preset_box);

    auto hp_seed_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto hp_seed_label = Gtk::make_managed<Gtk::Label>("HP Seed");
    hp_seed_label->set_size_request(100, -1);
    hp_seed_label->set_xalign(1.0);
    heightpipe_seed_entry_.set_hexpand(true);
    heightpipe_seed_entry_.set_placeholder_text("1");
    hp_seed_box->append(*hp_seed_label);
    hp_seed_box->append(heightpipe_seed_entry_);
    wrp_box_.append(*hp_seed_box);

    // Style JSON file picker
    style_label_.set_size_request(100, -1);
    style_label_.set_xalign(1.0);
    style_entry_.set_hexpand(true);
    style_entry_.set_placeholder_text("Optional style JSON file");
    style_browse_.signal_clicked().connect([this]() {
        auto dialog = Gtk::FileDialog::create();
        auto filter = Gtk::FileFilter::create();
        filter->set_name("JSON files");
        filter->add_pattern("*.json");
        auto filters = Gio::ListStore<Gtk::FileFilter>::create();
        filters->append(filter);
        dialog->set_filters(filters);
        auto* window = dynamic_cast<Gtk::Window*>(get_root());
        dialog->open(
            *window,
            [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    auto file = dialog->open_finish(result);
                    if (file) style_entry_.set_text(file->get_path());
                } catch (...) {}
            });
    });
    style_box_.append(style_label_);
    style_box_.append(style_entry_);
    style_box_.append(style_browse_);
    wrp_box_.append(style_box_);

    wrp_box_.append(wrp_use_heightpipe_);
    wrp_box_.append(wrp_extract_p3d_);
#if defined(WRP2PROJECT_WITH_TV4L)
    wrp_box_.append(wrp_empty_layers_);
#endif
}

void TabConfig::build_binaries_tab() {
    binaries_box_.set_margin(8);

    for (const auto& name : used_tool_names()) {
        auto row = std::unique_ptr<BinaryRow>(new BinaryRow());
        row->label.set_text(name);
        row->label.set_size_request(150, -1);
        row->label.set_xalign(1.0);
        row->entry.set_hexpand(true);
        row->browse.signal_clicked().connect(
            [this, entry = &row->entry]() { on_browse_path(*entry, false); });
        row->box.append(row->label);
        row->box.append(row->entry);
        row->box.append(row->browse);
        binaries_box_.append(row->box);
        binary_rows_.push_back(std::move(row));
    }

    search_dir_entry_.set_hexpand(true);
    search_dir_entry_.set_placeholder_text("Directory to search (empty = auto-detect)");
    search_dir_browse_.signal_clicked().connect(
        [this]() { on_browse_path(search_dir_entry_, true); });
    search_fill_box_.set_margin_top(8);
    search_fill_box_.append(search_fill_button_);
    search_fill_box_.append(search_dir_entry_);
    search_fill_box_.append(search_dir_browse_);
    search_fill_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabConfig::on_search_fill));
    binaries_box_.append(search_fill_box_);
}

void TabConfig::populate_from_config() {
    if (!cfg_) return;

    // General paths (order matches labels: OFP, Arma1, Arma2, Arma3, Workshop, ...)
    std::string* paths[] = {
        &cfg_->ofp_dir, &cfg_->arma1_dir, &cfg_->arma2_dir,
        &cfg_->arma3_dir, &cfg_->workshop_dir,
        &cfg_->a3db_path,
        &cfg_->worlds_dir, &cfg_->project_debug_dir, &cfg_->drive_root, &cfg_->ffmpeg_path
    };
    for (size_t i = 0; i < path_rows_.size() && i < std::size(paths); ++i) {
        path_rows_[i]->entry.set_text(*paths[i]);
    }

    // Asset browser
    auto_derap_.set_active(cfg_->asset_browser_defaults.auto_derap);
    on_demand_metadata_.set_active(cfg_->asset_browser_defaults.on_demand_metadata);

    // Wrp project
    std::string* wrp_vals[] = {
        &cfg_->wrp2project_defaults.offset_x, &cfg_->wrp2project_defaults.offset_z,
        &cfg_->wrp2project_defaults.split
    };
    for (size_t i = 0; i < wrp_rows_.size() && i < 3; ++i) {
        wrp_rows_[i]->entry.set_text(*wrp_vals[i]);
    }
    // Set HM scale combo
    auto& scale = cfg_->wrp2project_defaults.hm_scale;
    if (scale == "2") hm_scale_combo_.set_active(1);
    else if (scale == "4") hm_scale_combo_.set_active(2);
    else if (scale == "8") hm_scale_combo_.set_active(3);
    else if (scale == "16") hm_scale_combo_.set_active(4);
    else hm_scale_combo_.set_active(0);

    style_entry_.set_text(cfg_->wrp2project_defaults.style);
    wrp_use_heightpipe_.set_active(cfg_->wrp2project_defaults.use_heightpipe);
    const auto& hp_preset = cfg_->wrp2project_defaults.heightpipe_preset;
    if (hp_preset == "none") heightpipe_preset_combo_.set_active(0);
    else if (hp_preset == "sharp") heightpipe_preset_combo_.set_active(1);
    else if (hp_preset == "retain_detail") heightpipe_preset_combo_.set_active(2);
    else heightpipe_preset_combo_.set_active(3);
    heightpipe_seed_entry_.set_text(
        cfg_->wrp2project_defaults.heightpipe_seed.empty()
            ? "1" : cfg_->wrp2project_defaults.heightpipe_seed);
    wrp_extract_p3d_.set_active(cfg_->wrp2project_defaults.extract_p3d);
#if defined(WRP2PROJECT_WITH_TV4L)
    wrp_empty_layers_.set_active(cfg_->wrp2project_defaults.empty_layers);
#endif

    // Binaries
    const auto& names = used_tool_names();
    for (size_t i = 0; i < binary_rows_.size() && i < names.size(); ++i) {
        auto it = cfg_->binaries.find(names[i]);
        if (it != cfg_->binaries.end()) {
            binary_rows_[i]->entry.set_text(it->second);
        }
    }
}

void TabConfig::save_to_config() {
    if (!cfg_) return;

    std::string* paths[] = {
        &cfg_->ofp_dir, &cfg_->arma1_dir, &cfg_->arma2_dir,
        &cfg_->arma3_dir, &cfg_->workshop_dir,
        &cfg_->a3db_path,
        &cfg_->worlds_dir, &cfg_->project_debug_dir, &cfg_->drive_root, &cfg_->ffmpeg_path
    };
    for (size_t i = 0; i < path_rows_.size() && i < std::size(paths); ++i) {
        *paths[i] = path_rows_[i]->entry.get_text();
    }

    cfg_->asset_browser_defaults.auto_derap = auto_derap_.get_active();
    cfg_->asset_browser_defaults.on_demand_metadata = on_demand_metadata_.get_active();

    std::string* wrp_vals[] = {
        &cfg_->wrp2project_defaults.offset_x, &cfg_->wrp2project_defaults.offset_z,
        &cfg_->wrp2project_defaults.split
    };
    for (size_t i = 0; i < wrp_rows_.size() && i < 3; ++i) {
        *wrp_vals[i] = wrp_rows_[i]->entry.get_text();
    }
    cfg_->wrp2project_defaults.hm_scale = hm_scale_combo_.get_active_text();
    cfg_->wrp2project_defaults.style = style_entry_.get_text();
    cfg_->wrp2project_defaults.use_heightpipe = wrp_use_heightpipe_.get_active();
    cfg_->wrp2project_defaults.heightpipe_preset = heightpipe_preset_combo_.get_active_text();
    cfg_->wrp2project_defaults.heightpipe_seed = heightpipe_seed_entry_.get_text();
    cfg_->wrp2project_defaults.extract_p3d = wrp_extract_p3d_.get_active();
#if defined(WRP2PROJECT_WITH_TV4L)
    cfg_->wrp2project_defaults.empty_layers = wrp_empty_layers_.get_active();
#endif

    const auto& names = used_tool_names();
    for (size_t i = 0; i < binary_rows_.size() && i < names.size(); ++i) {
        auto text = binary_rows_[i]->entry.get_text();
        if (!text.empty()) {
            cfg_->binaries[names[i]] = text;
        } else {
            cfg_->binaries.erase(names[i]);
        }
    }

    save_config(*cfg_);

    // 7d: Notify AppWindow that config was saved
    if (on_saved) {
        on_saved();
    }
}

void TabConfig::on_browse_path(Gtk::Entry& entry, bool directory) {
    auto dialog = Gtk::FileDialog::create();
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    if (directory) {
        dialog->select_folder(
            *window,
            [&entry, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    auto file = dialog->select_folder_finish(result);
                    if (file) entry.set_text(file->get_path());
                } catch (...) {}
            });
    } else {
        dialog->open(
            *window,
            [&entry, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    auto file = dialog->open_finish(result);
                    if (file) entry.set_text(file->get_path());
                } catch (...) {}
            });
    }
}

void TabConfig::on_search_fill() {
    auto search_dir = std::string(search_dir_entry_.get_text());
    const auto& names = used_tool_names();
    for (size_t i = 0; i < binary_rows_.size() && i < names.size(); ++i) {
        if (binary_rows_[i]->entry.get_text().empty()) {
            if (!search_dir.empty()) {
                auto candidate = std::filesystem::path(search_dir) / names[i];
                if (std::filesystem::exists(candidate)) {
                    binary_rows_[i]->entry.set_text(candidate.string());
                    continue;
                }
            }
            auto path = find_binary(names[i]);
            if (!path.empty()) {
                binary_rows_[i]->entry.set_text(path);
            }
        }
    }
}
