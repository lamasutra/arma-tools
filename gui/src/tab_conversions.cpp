#include "tab_conversions.h"
#include "pbo_util.h"

#include <armatools/paa.h>
#include <armatools/tga.h>
#include <gdkmm/pixbuf.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

TabConversions::TabConversions() : Gtk::Box(Gtk::Orientation::VERTICAL, 8) {
    set_margin(8);

    // Mode selector row
    mode_label_.set_size_request(100, -1);
    mode_combo_.append("asc2tif",  "ASC \xe2\x86\x92 GeoTIFF");
    mode_combo_.append("paa2png",  "PAA \xe2\x86\x92 PNG");
    mode_combo_.append("png2paa",  "PNG \xe2\x86\x92 PAA");
    mode_combo_.append("paa2tga",  "PAA \xe2\x86\x92 TGA");
    mode_combo_.append("tga2paa",  "TGA \xe2\x86\x92 PAA");
    mode_combo_.set_active_id("asc2tif");
    mode_combo_.set_hexpand(true);
    mode_box_.append(mode_label_);
    mode_box_.append(mode_combo_);
    append(mode_box_);

    // Input row
    input_label_.set_size_request(100, -1);
    input_entry_.set_hexpand(true);
    input_entry_.set_placeholder_text("Input .asc file...");
    input_box_.append(input_label_);
    input_box_.append(input_entry_);
    input_box_.append(input_browse_);
    append(input_box_);

    // Output row
    output_label_.set_size_request(100, -1);
    output_entry_.set_hexpand(true);
    output_entry_.set_placeholder_text("Output .tif file...");
    output_box_.append(output_label_);
    output_box_.append(output_entry_);
    output_box_.append(output_browse_);
    append(output_box_);

    // Convert button
    convert_button_.set_halign(Gtk::Align::START);
    append(convert_button_);

    // Status
    append(status_label_);

    // Log output
    log_view_.set_editable(false);
    log_view_.set_monospace(true);
    log_scroll_.set_vexpand(true);
    log_scroll_.set_child(log_view_);
    append(log_scroll_);

    // Signals
    mode_combo_.signal_changed().connect(sigc::mem_fun(*this, &TabConversions::on_mode_changed));
    input_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabConversions::on_input_browse));
    output_browse_.signal_clicked().connect(sigc::mem_fun(*this, &TabConversions::on_output_browse));
    convert_button_.signal_clicked().connect(sigc::mem_fun(*this, &TabConversions::on_convert));

    // Auto-suggest output when input changes
    input_entry_.signal_changed().connect([this]() {
        auto in = input_entry_.get_text();
        if (!in.empty() && output_entry_.get_text().empty()) {
            fs::path p{std::string(in)};
            auto mode = mode_combo_.get_active_id();
            if (mode == "asc2tif")       p.replace_extension(".tif");
            else if (mode == "paa2png")  p.replace_extension(".png");
            else if (mode == "png2paa")  p.replace_extension(".paa");
            else if (mode == "paa2tga")  p.replace_extension(".tga");
            else if (mode == "tga2paa")  p.replace_extension(".paa");
            output_entry_.set_text(p.string());
        }
    });
}

TabConversions::~TabConversions() {
    if (worker_.joinable())
        worker_.join();
}

void TabConversions::set_config(Config* cfg) {
    cfg_ = cfg;
}

void TabConversions::on_mode_changed() {
    auto mode = mode_combo_.get_active_id();
    // Clear entries when mode changes
    input_entry_.set_text("");
    output_entry_.set_text("");

    if (mode == "asc2tif") {
        input_label_.set_text("Input ASC:");
        output_label_.set_text("Output GeoTIFF:");
        input_entry_.set_placeholder_text("Input .asc file...");
        output_entry_.set_placeholder_text("Output .tif file...");
    } else if (mode == "paa2png") {
        input_label_.set_text("Input PAA:");
        output_label_.set_text("Output PNG:");
        input_entry_.set_placeholder_text("Input .paa file...");
        output_entry_.set_placeholder_text("Output .png file...");
    } else if (mode == "png2paa") {
        input_label_.set_text("Input PNG:");
        output_label_.set_text("Output PAA:");
        input_entry_.set_placeholder_text("Input .png file...");
        output_entry_.set_placeholder_text("Output .paa file...");
    } else if (mode == "paa2tga") {
        input_label_.set_text("Input PAA:");
        output_label_.set_text("Output TGA:");
        input_entry_.set_placeholder_text("Input .paa file...");
        output_entry_.set_placeholder_text("Output .tga file...");
    } else if (mode == "tga2paa") {
        input_label_.set_text("Input TGA:");
        output_label_.set_text("Output PAA:");
        input_entry_.set_placeholder_text("Input .tga file...");
        output_entry_.set_placeholder_text("Output .paa file...");
    }
}

void TabConversions::on_input_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    auto mode = mode_combo_.get_active_id();

    if (mode == "asc2tif") {
        filter->set_name("ASC files");
        filter->add_pattern("*.asc");
    } else if (mode == "paa2png" || mode == "paa2tga") {
        filter->set_name("PAA files");
        filter->add_pattern("*.paa");
        filter->add_pattern("*.pac");
    } else if (mode == "png2paa") {
        filter->set_name("PNG files");
        filter->add_pattern("*.png");
    } else if (mode == "tga2paa") {
        filter->set_name("TGA files");
        filter->add_pattern("*.tga");
    }

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

void TabConversions::on_output_browse() {
    auto dialog = Gtk::FileDialog::create();
    auto filter = Gtk::FileFilter::create();
    auto mode = mode_combo_.get_active_id();

    if (mode == "asc2tif") {
        filter->set_name("TIFF files");
        filter->add_pattern("*.tif");
        filter->add_pattern("*.tiff");
    } else if (mode == "paa2png") {
        filter->set_name("PNG files");
        filter->add_pattern("*.png");
    } else if (mode == "png2paa" || mode == "tga2paa") {
        filter->set_name("PAA files");
        filter->add_pattern("*.paa");
    } else if (mode == "paa2tga") {
        filter->set_name("TGA files");
        filter->add_pattern("*.tga");
    }

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    dialog->save(
        *window,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->save_finish(result);
                if (file) output_entry_.set_text(file->get_path());
            } catch (...) {}
        });
}

void TabConversions::on_convert() {
    if (!cfg_) return;

    auto input = std::string(input_entry_.get_text());
    auto output = std::string(output_entry_.get_text());
    if (input.empty() || output.empty()) {
        status_label_.set_text("Please specify input and output paths.");
        return;
    }

    // Join any previous worker before launching a new one
    if (worker_.joinable())
        worker_.join();

    auto mode = std::string(mode_combo_.get_active_id());

    status_label_.set_text("Converting...");
    convert_button_.set_sensitive(false);

    if (mode == "asc2tif") {
        convert_asc_to_geotiff(input, output);
    } else if (mode == "paa2png") {
        convert_paa_to_png(input, output);
    } else if (mode == "png2paa") {
        convert_png_to_paa(input, output);
    } else if (mode == "paa2tga") {
        convert_paa_to_tga(input, output);
    } else if (mode == "tga2paa") {
        convert_tga_to_paa(input, output);
    }
}

void TabConversions::convert_asc_to_geotiff(const std::string& input,
                                             const std::string& output) {
    auto tool = resolve_tool_path(*cfg_, "asc2tiff");
    if (tool.empty()) {
        status_label_.set_text("Error: asc2tiff binary not found.");
        convert_button_.set_sensitive(true);
        return;
    }

    append_log("Running: " + tool + " " + input + " " + output + "\n");

    worker_ = std::thread([this, tool, input, output]() {
        auto res = run_subprocess(tool, {input, output});

        Glib::signal_idle().connect_once([this, res]() {
            append_log(res.output);
            if (res.status == 0) {
                status_label_.set_text("Conversion complete.");
            } else {
                status_label_.set_text("Conversion failed (exit " +
                                       std::to_string(res.status) + ").");
            }
            convert_button_.set_sensitive(true);
        });
    });
}

void TabConversions::convert_paa_to_png(const std::string& input,
                                         const std::string& output) {
    append_log("Converting PAA -> PNG: " + input + " -> " + output + "\n");

    worker_ = std::thread([this, input, output]() {
        try {
            std::ifstream ifs(input, std::ios::binary);
            if (!ifs) throw std::runtime_error("Cannot open input file: " + input);

            auto [img, hdr] = armatools::paa::decode(ifs);
            ifs.close();

            // Create a GdkPixbuf from the RGBA data and save as PNG
            auto pixbuf = Gdk::Pixbuf::create_from_data(
                img.pixels.data(),
                Gdk::Colorspace::RGB,
                true,   // has_alpha
                8,      // bits_per_sample
                img.width,
                img.height,
                img.width * 4  // rowstride
            );
            pixbuf->save(output, "png");

            Glib::signal_idle().connect_once([this, hdr]() {
                append_log("Decoded PAA (" + hdr.format + ", " +
                           std::to_string(hdr.width) + "x" +
                           std::to_string(hdr.height) + ")\n");
                status_label_.set_text("Conversion complete.");
                convert_button_.set_sensitive(true);
            });
        } catch (const std::exception& e) {
            std::string err = e.what();
            Glib::signal_idle().connect_once([this, err]() {
                append_log("Error: " + err + "\n");
                status_label_.set_text("Conversion failed.");
                convert_button_.set_sensitive(true);
            });
        }
    });
}

void TabConversions::convert_png_to_paa(const std::string& input,
                                         const std::string& output) {
    append_log("Converting PNG -> PAA: " + input + " -> " + output + "\n");

    worker_ = std::thread([this, input, output]() {
        try {
            auto pixbuf = Gdk::Pixbuf::create_from_file(input);
            if (!pixbuf) throw std::runtime_error("Cannot load PNG: " + input);

            // Ensure RGBA
            auto rgba = pixbuf->add_alpha(false, 0, 0, 0);

            armatools::paa::Image img;
            img.width = rgba->get_width();
            img.height = rgba->get_height();

            int rowstride = rgba->get_rowstride();
            const uint8_t* src = rgba->get_pixels();
            img.pixels.resize(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4);

            for (int y = 0; y < img.height; ++y) {
                const uint8_t* row = src + y * rowstride;
                std::memcpy(img.pixels.data() + y * img.width * 4, row,
                            static_cast<size_t>(img.width) * 4);
            }

            std::ofstream ofs(output, std::ios::binary);
            if (!ofs) throw std::runtime_error("Cannot open output file: " + output);

            auto hdr = armatools::paa::encode(ofs, img, "auto");
            ofs.close();

            Glib::signal_idle().connect_once([this, img]() {
                append_log("Encoded PAA (" + std::to_string(img.width) + "x" +
                           std::to_string(img.height) + ")\n");
                status_label_.set_text("Conversion complete.");
                convert_button_.set_sensitive(true);
            });
        } catch (const std::exception& e) {
            std::string err = e.what();
            Glib::signal_idle().connect_once([this, err]() {
                append_log("Error: " + err + "\n");
                status_label_.set_text("Conversion failed.");
                convert_button_.set_sensitive(true);
            });
        }
    });
}

void TabConversions::convert_paa_to_tga(const std::string& input,
                                         const std::string& output) {
    append_log("Converting PAA -> TGA: " + input + " -> " + output + "\n");

    worker_ = std::thread([this, input, output]() {
        try {
            std::ifstream ifs(input, std::ios::binary);
            if (!ifs) throw std::runtime_error("Cannot open input file: " + input);

            auto [paa_img, hdr] = armatools::paa::decode(ifs);
            ifs.close();

            // Copy PAA image data to TGA image struct
            armatools::tga::Image tga_img;
            tga_img.width = paa_img.width;
            tga_img.height = paa_img.height;
            tga_img.pixels = std::move(paa_img.pixels);

            std::ofstream ofs(output, std::ios::binary);
            if (!ofs) throw std::runtime_error("Cannot open output file: " + output);

            armatools::tga::encode(ofs, tga_img);
            ofs.close();

            Glib::signal_idle().connect_once([this, hdr]() {
                append_log("Decoded PAA (" + hdr.format + ", " +
                           std::to_string(hdr.width) + "x" +
                           std::to_string(hdr.height) + ") -> TGA\n");
                status_label_.set_text("Conversion complete.");
                convert_button_.set_sensitive(true);
            });
        } catch (const std::exception& e) {
            std::string err = e.what();
            Glib::signal_idle().connect_once([this, err]() {
                append_log("Error: " + err + "\n");
                status_label_.set_text("Conversion failed.");
                convert_button_.set_sensitive(true);
            });
        }
    });
}

void TabConversions::convert_tga_to_paa(const std::string& input,
                                         const std::string& output) {
    append_log("Converting TGA -> PAA: " + input + " -> " + output + "\n");

    worker_ = std::thread([this, input, output]() {
        try {
            std::ifstream ifs(input, std::ios::binary);
            if (!ifs) throw std::runtime_error("Cannot open input file: " + input);

            auto tga_img = armatools::tga::decode(ifs);
            ifs.close();

            // Copy TGA image data to PAA image struct
            armatools::paa::Image paa_img;
            paa_img.width = tga_img.width;
            paa_img.height = tga_img.height;
            paa_img.pixels = std::move(tga_img.pixels);

            std::ofstream ofs(output, std::ios::binary);
            if (!ofs) throw std::runtime_error("Cannot open output file: " + output);

            auto hdr = armatools::paa::encode(ofs, paa_img, "auto");
            ofs.close();

            Glib::signal_idle().connect_once([this, paa_img]() {
                append_log("Encoded PAA (" + std::to_string(paa_img.width) + "x" +
                           std::to_string(paa_img.height) + ")\n");
                status_label_.set_text("Conversion complete.");
                convert_button_.set_sensitive(true);
            });
        } catch (const std::exception& e) {
            std::string err = e.what();
            Glib::signal_idle().connect_once([this, err]() {
                append_log("Error: " + err + "\n");
                status_label_.set_text("Conversion failed.");
                convert_button_.set_sensitive(true);
            });
        }
    });
}

void TabConversions::append_log(const std::string& text) {
    auto buf = log_view_.get_buffer();
    buf->insert(buf->end(), text);
    // Auto-scroll to end
    auto mark = buf->create_mark(buf->end());
    log_view_.scroll_to(mark);
}
