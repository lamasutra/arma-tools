#pragma once

#include <cairomm/cairomm.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Format a time value for grid labels.
inline std::string grid_time_label(double t) {
    int total = static_cast<int>(t);
    int min = total / 60;
    int sec = total % 60;
    if (min > 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
        return buf;
    }
    return std::to_string(sec) + "s";
}

// Draw vertical time grid lines with labels.
inline void draw_time_grid(const Cairo::RefPtr<Cairo::Context>& cr,
                            int width, int height, double duration) {
    if (duration <= 0 || width <= 0) return;

    double px_per_sec = width / duration;

    cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                          Cairo::ToyFontFace::Weight::NORMAL);
    cr->set_font_size(10.0);

    // 0.1s grid (dotted, opacity 0.35) — skip if lines would be < 4px apart
    if (px_per_sec * 0.1 >= 4.0) {
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.35);
        cr->set_line_width(1.0);
        std::vector<double> dashes = {2.0, 3.0};
        cr->set_dash(dashes, 0.0);

        double step = 0.1;
        for (double t = step; t < duration; t += step) {
            double frac = t - std::floor(t);
            if (frac < 0.01 || frac > 0.99) continue;

            double x = std::round(t / duration * width) + 0.5;
            cr->move_to(x, 0);
            cr->line_to(x, height);
            cr->stroke();
        }

        cr->unset_dash();
    }

    // 1s grid (solid, opacity 0.5) with time labels
    if (px_per_sec >= 4.0) {
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.5);
        cr->set_line_width(1.0);

        for (double t = 1.0; t < duration; t += 1.0) {
            double x = std::round(t / duration * width) + 0.5;
            cr->move_to(x, 0);
            cr->line_to(x, height);
            cr->stroke();

            // Time label at top
            auto label = grid_time_label(t);
            cr->set_source_rgba(1.0, 1.0, 1.0, 0.6);
            cr->move_to(x + 3, 11);
            cr->show_text(label);
        }
    }
}

// Draw horizontal dB reference lines on waveform.
// Amplitude 1.0 = 0 dB. Lines at -6, -12, -18, -24 dB (mirrored).
inline void draw_db_grid(const Cairo::RefPtr<Cairo::Context>& cr,
                          int width, int height) {
    if (height < 40) return;

    double mid_y = height / 2.0;

    cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                          Cairo::ToyFontFace::Weight::NORMAL);
    cr->set_font_size(9.0);

    static constexpr int db_values[] = {-6, -12, -18, -24};
    for (int db : db_values) {
        double amplitude = std::pow(10.0, db / 20.0); // dB to linear
        double y_offset = amplitude * mid_y;

        double y_top = mid_y - y_offset;
        double y_bot = mid_y + y_offset;

        // Skip if too close to edge or center
        if (y_top < 2 || y_bot > height - 2) continue;

        // Dotted line
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.3);
        cr->set_line_width(1.0);
        std::vector<double> dashes = {2.0, 3.0};
        cr->set_dash(dashes, 0.0);

        cr->move_to(0, std::round(y_top) + 0.5);
        cr->line_to(width, std::round(y_top) + 0.5);
        cr->stroke();

        cr->move_to(0, std::round(y_bot) + 0.5);
        cr->line_to(width, std::round(y_bot) + 0.5);
        cr->stroke();

        cr->unset_dash();

        // Label on the right
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%ddB", db);
        Cairo::TextExtents ext;
        cr->get_text_extents(buf, ext);
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.5);
        cr->move_to(width - ext.width - 3, std::round(y_top) - 2);
        cr->show_text(buf);
    }

    // 0 dB label at center line
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.3);
    cr->set_line_width(1.0);
    std::vector<double> dashes = {4.0, 4.0};
    cr->set_dash(dashes, 0.0);
    cr->move_to(0, std::round(mid_y) + 0.5);
    cr->line_to(width, std::round(mid_y) + 0.5);
    cr->stroke();
    cr->unset_dash();
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.5);
    cr->move_to(width - 18, std::round(mid_y) - 2);
    cr->show_text("0dB");
}
