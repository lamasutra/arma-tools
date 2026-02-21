#include "tab_about.h"
#include <armatools/version.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
fs::path executable_dir() {
#ifdef _WIN32
    wchar_t module_path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) return fs::path(module_path).parent_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path();
}

std::optional<fs::path> find_about_icon_path() {
    const auto exe = executable_dir();
    const std::vector<fs::path> candidates = {
        exe / ".." / "share" / "icons" / "hicolor" / "256x256" / "apps" / "arma-tools.png",
        exe / "share" / "icons" / "hicolor" / "256x256" / "apps" / "arma-tools.png",
        exe / ".." / ".." / "assets" / "arma-tools.png",
        exe / "assets" / "arma-tools.png",
    };

    std::error_code ec;
    for (const auto& p : candidates) {
        if (fs::exists(p, ec)) return fs::weakly_canonical(p, ec);
    }
    return std::nullopt;
}
}  // namespace

TabAbout::TabAbout() : Gtk::Box(Gtk::Orientation::VERTICAL, 16) {
    set_margin(32);
    set_valign(Gtk::Align::CENTER);
    set_halign(Gtk::Align::CENTER);

    bool icon_loaded = false;
    if (auto icon_path = find_about_icon_path()) {
        try {
            auto texture = Gdk::Texture::create_from_filename(icon_path->string());
            if (texture) {
                icon_.set(texture);
                icon_loaded = true;
            }
        } catch (...) {
        }
    }
    if (!icon_loaded) {
        auto icon_name = Glib::ustring{"arma-tools"};
        if (auto display = Gdk::Display::get_default()) {
            auto icon_theme = Gtk::IconTheme::get_for_display(display);
            if (!icon_theme || !icon_theme->has_icon(icon_name)) icon_name = "help-about-symbolic";
        } else {
            icon_name = "help-about-symbolic";
        }
        icon_.set_from_icon_name(icon_name);
    }
    icon_.set_pixel_size(256);
    icon_.set_halign(Gtk::Align::CENTER);

    title_.set_markup("<span size='xx-large' weight='bold'>ArmA Tools</span>");
    version_.set_text(std::string("Version ") + armatools::version_string());
    description_.set_markup(
        "Community toolkit for ArmA 3 modding.\n\n"
        "PAA texture viewer, WRP terrain inspector,\n"
        "PBO archiver, P3D model tools, and more.\n\n"
        "<span size='small'>Built with gtkmm-4.0\n"
        "vibecoded by lamasutra</span>");
    description_.set_justify(Gtk::Justification::CENTER);

    append(icon_);
    append(title_);
    append(version_);
    append(description_);
}
