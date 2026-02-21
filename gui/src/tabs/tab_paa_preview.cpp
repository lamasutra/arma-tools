#include "tab_paa_preview.h"
#include "config.h"
#include "log_panel.h"
#include "pbo_util.h"

#include <armatools/paa.h>
#include <armatools/pboindex.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>

TabPaaPreview::TabPaaPreview() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    // Left panel: path + info
    info_box_.set_margin(8);
    info_box_.set_size_request(150, -1);

    // PBO mode switch
    pbo_label_.set_margin_end(2);
    path_box_.append(pbo_label_);
    pbo_switch_.add_css_class("compact-switch");
    path_box_.append(pbo_switch_);

    path_entry_.set_hexpand(true);
    path_entry_.set_placeholder_text("PAA/PAC file path...");
    path_box_.append(path_entry_);
    path_box_.append(browse_button_);
    search_button_.set_visible(false);
    path_box_.append(search_button_);
    info_box_.append(path_box_);

    // Search results (PBO mode only)
    search_results_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    search_scroll_.set_child(search_results_);
    search_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    search_scroll_.set_max_content_height(200);
    search_scroll_.set_propagate_natural_height(true);
    search_scroll_.set_visible(false);
    info_box_.append(search_scroll_);

    // Toolbar
    toolbar_.set_margin_top(4);
    toolbar_.set_margin_bottom(4);
    zoom_fit_button_.set_tooltip_text("Zoom to fit image in view");
    zoom_100_button_.set_tooltip_text("Zoom to 100% (actual pixels)");
    alpha_button_.set_tooltip_text("Show alpha channel as grayscale");
    save_png_button_.set_tooltip_text("Save decoded image as PNG");

    // Mip level selector
    mip_combo_.append("Mip 0 (largest)");
    mip_combo_.set_active(0);
    mip_combo_.set_tooltip_text("Mip level selection (only mip 0 available currently)");
    mip_combo_.set_sensitive(false);

    toolbar_.append(zoom_fit_button_);
    toolbar_.append(zoom_100_button_);
    toolbar_.append(alpha_button_);
    toolbar_.append(save_png_button_);
    toolbar_.append(mip_combo_);
    info_box_.append(toolbar_);

    info_label_.set_halign(Gtk::Align::START);
    info_label_.set_valign(Gtk::Align::START);
    info_label_.set_wrap(true);
    info_box_.append(info_label_);

    set_start_child(info_box_);
    set_position(280);

    // Right panel: DrawingArea with zoom/pan
    draw_area_.set_hexpand(true);
    draw_area_.set_vexpand(true);
    draw_area_.set_draw_func(sigc::mem_fun(*this, &TabPaaPreview::on_draw));
    set_end_child(draw_area_);

    // Scroll controller for zoom
    auto scroll_ctrl = Gtk::EventControllerScroll::create();
    scroll_ctrl->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_ctrl->signal_scroll().connect(
        [this](double /*dx*/, double dy) -> bool {
            if (decoded_width_ == 0) return false;

            // Get cursor position relative to widget
            // We use the last known pointer position from the motion controller
            double cursor_x = draw_area_.get_width() / 2.0;
            double cursor_y = draw_area_.get_height() / 2.0;

            double old_zoom = zoom_level_;
            double factor = (dy < 0) ? 1.1 : (1.0 / 1.1);
            zoom_level_ = std::clamp(zoom_level_ * factor, 0.01, 100.0);

            // Adjust pan so zoom is centered on cursor
            double ratio = zoom_level_ / old_zoom;
            pan_x_ = cursor_x - ratio * (cursor_x - pan_x_);
            pan_y_ = cursor_y - ratio * (cursor_y - pan_y_);

            draw_area_.queue_draw();
            return true;
        },
        false);
    draw_area_.add_controller(scroll_ctrl);

    // Motion controller to track cursor position for scroll-zoom centering
    auto motion_ctrl = Gtk::EventControllerMotion::create();
    double* cursor_xy = new double[2]{0, 0};  // shared state
    // We'll store cursor position in the scroll handler via a lambda capture trick.
    // Actually, let's use a cleaner approach: store in members via the motion controller.
    // But we don't have extra members for cursor. Let's just use widget center for simplicity
    // and add proper cursor tracking:

    // We need the cursor position in the scroll callback. Let's refactor:
    // Store cursor pos in local shared state captured by both lambdas.
    delete[] cursor_xy;

    // Use a shared_ptr for cursor position
    auto cursor_pos = std::make_shared<std::pair<double, double>>(0.0, 0.0);

    auto motion_ctrl2 = Gtk::EventControllerMotion::create();
    motion_ctrl2->signal_motion().connect(
        [cursor_pos](double x, double y) {
            cursor_pos->first = x;
            cursor_pos->second = y;
        });
    draw_area_.add_controller(motion_ctrl2);

    // Re-setup scroll controller with cursor awareness
    draw_area_.remove_controller(scroll_ctrl);
    auto scroll_ctrl2 = Gtk::EventControllerScroll::create();
    scroll_ctrl2->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll_ctrl2->signal_scroll().connect(
        [this, cursor_pos](double /*dx*/, double dy) -> bool {
            if (decoded_width_ == 0) return false;

            double cursor_x = cursor_pos->first;
            double cursor_y = cursor_pos->second;

            double old_zoom = zoom_level_;
            double factor = (dy < 0) ? 1.1 : (1.0 / 1.1);
            zoom_level_ = std::clamp(zoom_level_ * factor, 0.01, 100.0);

            double ratio = zoom_level_ / old_zoom;
            pan_x_ = cursor_x - ratio * (cursor_x - pan_x_);
            pan_y_ = cursor_y - ratio * (cursor_y - pan_y_);

            draw_area_.queue_draw();
            return true;
        },
        false);
    draw_area_.add_controller(scroll_ctrl2);

    // Drag controller for panning (left or middle mouse button)
    auto drag_ctrl = Gtk::GestureDrag::create();
    drag_ctrl->set_button(0);  // any button
    drag_ctrl->signal_drag_begin().connect(
        [this](double x, double y) {
            dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            drag_start_pan_x_ = pan_x_;
            drag_start_pan_y_ = pan_y_;
        });
    drag_ctrl->signal_drag_update().connect(
        [this](double offset_x, double offset_y) {
            if (!dragging_) return;
            pan_x_ = drag_start_pan_x_ + offset_x;
            pan_y_ = drag_start_pan_y_ + offset_y;
            draw_area_.queue_draw();
        });
    drag_ctrl->signal_drag_end().connect(
        [this](double /*offset_x*/, double /*offset_y*/) {
            dragging_ = false;
        });
    draw_area_.add_controller(drag_ctrl);

    // Toolbar signals
    zoom_fit_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPaaPreview::zoom_fit));
    zoom_100_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPaaPreview::zoom_100));
    alpha_button_.signal_toggled().connect(sigc::mem_fun(*this, &TabPaaPreview::on_alpha_toggled));
    save_png_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPaaPreview::on_save_png));

    // Signals
    browse_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPaaPreview::on_browse));
    path_entry_.signal_activate().connect([this]() {
        if (pbo_mode_) on_search();
        else load_file(path_entry_.get_text());
    });
    pbo_switch_.property_active().signal_changed().connect(
        sigc::mem_fun(*this, &TabPaaPreview::on_pbo_mode_changed));
    search_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabPaaPreview::on_search));
    search_results_.signal_row_selected().connect(
        sigc::mem_fun(*this, &TabPaaPreview::on_search_result_selected));
}

TabPaaPreview::~TabPaaPreview() {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
}

void TabPaaPreview::set_pbo_index_service(const std::shared_ptr<PboIndexService>& service) {
    if (pbo_index_service_) pbo_index_service_->unsubscribe(this);
    pbo_index_service_ = service;
}

void TabPaaPreview::set_config(Config* cfg) {
    cfg_ = cfg;
    db_.reset();

    if (!pbo_index_service_) return;
    pbo_index_service_->subscribe(this, [this](const PboIndexService::Snapshot& snap) {
        if (!cfg_ || cfg_->a3db_path != snap.db_path) return;
        db_ = snap.db;
    });
}

void TabPaaPreview::on_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PAA/PAC files");
    filter->add_pattern("*.paa");
    filter->add_pattern("*.pac");
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
                    auto path = file->get_path();
                    path_entry_.set_text(path);
                    load_file(path);
                }
            } catch (...) {}
        });
}

void TabPaaPreview::store_decoded(const std::vector<uint8_t>& pixels, int w, int h) {
    decoded_pixels_ = pixels;
    decoded_width_ = w;
    decoded_height_ = h;
    alpha_button_.set_active(false);
    update_display_texture();
    zoom_fit();
}

void TabPaaPreview::update_display_texture() {
    if (decoded_width_ == 0 || decoded_height_ == 0 || decoded_pixels_.empty()) {
        display_texture_.reset();
        draw_area_.queue_draw();
        return;
    }

    std::vector<uint8_t> pixels;
    if (alpha_button_.get_active()) {
        // Alpha-as-grayscale: R=G=B=A, A=255
        pixels.resize(decoded_pixels_.size());
        for (size_t i = 0; i < decoded_pixels_.size(); i += 4) {
            uint8_t a = decoded_pixels_[i + 3];
            pixels[i + 0] = a;
            pixels[i + 1] = a;
            pixels[i + 2] = a;
            pixels[i + 3] = 255;
        }
    } else {
        pixels = decoded_pixels_;
    }

    auto pixbuf = Gdk::Pixbuf::create_from_data(
        pixels.data(),
        Gdk::Colorspace::RGB,
        true, 8,
        decoded_width_, decoded_height_,
        decoded_width_ * 4
    );
    auto copy = pixbuf->copy();
    display_texture_ = Gdk::Texture::create_for_pixbuf(copy);
    draw_area_.queue_draw();
}

void TabPaaPreview::update_info(const std::string& prefix,
                                 const std::string& format,
                                 int hdr_w, int hdr_h,
                                 int dec_w, int dec_h) {
    std::ostringstream info;
    if (!prefix.empty())
        info << "Source: " << prefix << "\n";
    info << "Format: " << format << "\n"
         << "Dimensions: " << hdr_w << " x " << hdr_h << "\n"
         << "Decoded: " << dec_w << " x " << dec_h << "\n"
         << "Mipmaps: N/A\n";

    if (current_file_size_ > 0) {
        info << "File size: " << current_file_size_ << " bytes\n";
        if (hdr_w > 0 && hdr_h > 0) {
            double avg_texel = static_cast<double>(current_file_size_) /
                               (static_cast<double>(hdr_w) * hdr_h);
            info << "Avg texel size: " << std::fixed << std::setprecision(2)
                 << avg_texel << " bytes";
        }
    }

    info_label_.set_text(info.str());
}

void TabPaaPreview::load_file(const std::string& path) {
    if (path.empty()) return;
    current_path_ = path;

    try {
        // Get file size
        std::error_code ec;
        current_file_size_ = std::filesystem::file_size(path, ec);
        if (ec) current_file_size_ = 0;

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            info_label_.set_text("Error: Cannot open file");
            return;
        }

        auto [img, hdr] = armatools::paa::decode(f);

        update_info("", hdr.format, hdr.width, hdr.height, img.width, img.height);
        store_decoded(img.pixels, img.width, img.height);

    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        display_texture_.reset();
        draw_area_.queue_draw();
    }
}

void TabPaaPreview::on_draw(const Cairo::RefPtr<Cairo::Context>& cr,
                             int /*width*/, int /*height*/) {
    // Dark background
    cr->set_source_rgb(0.15, 0.15, 0.15);
    cr->paint();

    if (!display_texture_ || decoded_width_ == 0) return;

    // Draw checkerboard pattern behind image (for alpha visibility)
    cr->save();
    cr->translate(pan_x_, pan_y_);
    cr->scale(zoom_level_, zoom_level_);

    int img_w = display_texture_->get_width();
    int img_h = display_texture_->get_height();

    // Checkerboard
    const int checker_size = 8;
    for (int cy = 0; cy < img_h; cy += checker_size) {
        for (int cx = 0; cx < img_w; cx += checker_size) {
            bool light = ((cx / checker_size) + (cy / checker_size)) % 2 == 0;
            cr->set_source_rgb(light ? 0.8 : 0.6, light ? 0.8 : 0.6, light ? 0.8 : 0.6);
            cr->rectangle(cx, cy,
                          std::min(checker_size, img_w - cx),
                          std::min(checker_size, img_h - cy));
            cr->fill();
        }
    }

    // Paint the texture via a Gdk::Pixbuf -> Cairo::ImageSurface approach
    // Create surface from the decoded pixels
    std::vector<uint8_t> surface_data;
    if (alpha_button_.get_active()) {
        surface_data.resize(static_cast<size_t>(decoded_width_) * static_cast<size_t>(decoded_height_) * 4);
        for (size_t i = 0; i < decoded_pixels_.size(); i += 4) {
            uint8_t a = decoded_pixels_[i + 3];
            // Cairo ARGB32 is premultiplied, in native byte order (little-endian: BGRA)
            surface_data[i + 0] = a;   // B
            surface_data[i + 1] = a;   // G
            surface_data[i + 2] = a;   // R
            surface_data[i + 3] = 255; // A
        }
    } else {
        surface_data.resize(static_cast<size_t>(decoded_width_) * static_cast<size_t>(decoded_height_) * 4);
        for (size_t i = 0; i < decoded_pixels_.size(); i += 4) {
            uint8_t r = decoded_pixels_[i + 0];
            uint8_t g = decoded_pixels_[i + 1];
            uint8_t b = decoded_pixels_[i + 2];
            uint8_t a = decoded_pixels_[i + 3];
            // Cairo ARGB32 premultiplied, little-endian: BGRA
            surface_data[i + 0] = static_cast<uint8_t>(b * a / 255);
            surface_data[i + 1] = static_cast<uint8_t>(g * a / 255);
            surface_data[i + 2] = static_cast<uint8_t>(r * a / 255);
            surface_data[i + 3] = a;
        }
    }

    auto surface = Cairo::ImageSurface::create(
        surface_data.data(),
        Cairo::Surface::Format::ARGB32,
        decoded_width_, decoded_height_,
        decoded_width_ * 4
    );

    cr->set_source(surface, 0, 0);
    cr->rectangle(0, 0, img_w, img_h);
    cr->fill();

    cr->restore();
}

void TabPaaPreview::zoom_fit() {
    if (decoded_width_ == 0 || decoded_height_ == 0) return;

    int w = draw_area_.get_width();
    int h = draw_area_.get_height();
    if (w <= 0 || h <= 0) return;

    double zx = static_cast<double>(w) / decoded_width_;
    double zy = static_cast<double>(h) / decoded_height_;
    zoom_level_ = std::min(zx, zy);

    // Center the image
    pan_x_ = (w - decoded_width_ * zoom_level_) / 2.0;
    pan_y_ = (h - decoded_height_ * zoom_level_) / 2.0;

    draw_area_.queue_draw();
}

void TabPaaPreview::zoom_100() {
    if (decoded_width_ == 0) return;

    zoom_level_ = 1.0;

    int w = draw_area_.get_width();
    int h = draw_area_.get_height();
    pan_x_ = (w - decoded_width_) / 2.0;
    pan_y_ = (h - decoded_height_) / 2.0;

    draw_area_.queue_draw();
}

void TabPaaPreview::on_alpha_toggled() {
    update_display_texture();
}

void TabPaaPreview::on_save_png() {
    if (decoded_width_ == 0 || decoded_pixels_.empty()) return;

    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PNG files");
    filter->add_pattern("*.png");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    // Suggest a default filename based on current path
    if (!current_path_.empty()) {
        auto stem = std::filesystem::path(current_path_).stem().string();
        dialog->set_initial_name(stem + ".png");
    } else {
        dialog->set_initial_name("image.png");
    }

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (file) {
                    auto path = file->get_path();

                    auto pixbuf = Gdk::Pixbuf::create_from_data(
                        decoded_pixels_.data(),
                        Gdk::Colorspace::RGB,
                        true, 8,
                        decoded_width_, decoded_height_,
                        decoded_width_ * 4
                    );
                    auto copy = pixbuf->copy();
                    copy->save(path, "png");

                    app_log(LogLevel::Info, "Saved PNG: " + path);
                }
            } catch (const std::exception& e) {
                app_log(LogLevel::Error, std::string("Save PNG failed: ") + e.what());
            } catch (...) {}
        });
}

void TabPaaPreview::on_pbo_mode_changed() {
    pbo_mode_ = pbo_switch_.get_active();
    path_entry_.set_text("");

    if (pbo_mode_) {
        path_entry_.set_placeholder_text("Search in PBO...");
        browse_button_.set_visible(false);
        search_button_.set_visible(true);
        search_scroll_.set_visible(true);
    } else {
        path_entry_.set_placeholder_text("PAA/PAC file path...");
        browse_button_.set_visible(true);
        search_button_.set_visible(false);
        search_scroll_.set_visible(false);
    }
}

void TabPaaPreview::on_search() {
    auto query = std::string(path_entry_.get_text());
    if (query.empty() || !db_) return;

    while (auto* row = search_results_.get_row_at_index(0))
        search_results_.remove(*row);
    search_results_data_.clear();

    auto paa_results = db_->find_files("*" + query + "*.paa");
    auto pac_results = db_->find_files("*" + query + "*.pac");
    search_results_data_ = paa_results;
    search_results_data_.insert(search_results_data_.end(),
                                pac_results.begin(), pac_results.end());

    for (const auto& r : search_results_data_) {
        auto display = r.prefix + "/" + r.file_path;
        auto* label = Gtk::make_managed<Gtk::Label>(display);
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        search_results_.append(*label);
    }
}

void TabPaaPreview::on_search_result_selected(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto idx = static_cast<size_t>(row->get_index());
    if (idx >= search_results_data_.size()) return;
    load_from_pbo(search_results_data_[idx]);
}

void TabPaaPreview::load_from_pbo(const armatools::pboindex::FindResult& r) {
    auto data = extract_from_pbo(r.pbo_path, r.file_path);
    if (data.empty()) {
        info_label_.set_text("Error: Could not extract from PBO");
        display_texture_.reset();
        draw_area_.queue_draw();
        return;
    }

    current_path_ = r.prefix + "/" + r.file_path;
    current_file_size_ = data.size();

    try {
        std::string str(data.begin(), data.end());
        std::istringstream stream(str);
        auto [img, hdr] = armatools::paa::decode(stream);

        update_info(r.prefix + "/" + r.file_path, hdr.format,
                    hdr.width, hdr.height, img.width, img.height);
        store_decoded(img.pixels, img.width, img.height);

        app_log(LogLevel::Info, "Loaded PAA from PBO: " + r.prefix + "/" + r.file_path);

    } catch (const std::exception& e) {
        info_label_.set_text(std::string("Error: ") + e.what());
        display_texture_.reset();
        draw_area_.queue_draw();
    }
}
