#include "app_window.h"

#include <adwaita.h>
#include <libpanel.h>
#include <gtkmm.h>

#include <exception>
#include <iostream>
#ifdef _WIN32
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <windows.h>
#endif

#include <memory>
#include "cli_logger.h"

extern "C" GResource* arma_tools_get_resource(void);

#ifdef _WIN32
namespace {
std::filesystem::path get_executable_dir() {
    wchar_t module_path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(module_path).parent_path();
}

void setup_gtk_runtime_env() {
    const auto exe_dir = get_executable_dir();
    const auto schema_dir1 = exe_dir / ".." / "share" / "glib-2.0" / "schemas";
    const auto schema_dir2 = exe_dir / "share" / "glib-2.0" / "schemas";
    if (std::filesystem::exists(schema_dir1 / "gschemas.compiled")) {
        SetEnvironmentVariableW(L"GSETTINGS_SCHEMA_DIR",
                                schema_dir1.lexically_normal().c_str());
    } else if (std::filesystem::exists(schema_dir2 / "gschemas.compiled")) {
        SetEnvironmentVariableW(L"GSETTINGS_SCHEMA_DIR",
                                schema_dir2.lexically_normal().c_str());
    }
    SetEnvironmentVariableW(L"GIO_USE_VFS", L"local");
}

void setup_stderr_log() {
    const char* local_appdata = std::getenv("LOCALAPPDATA");
    std::filesystem::path log_dir =
        local_appdata ? std::filesystem::path(local_appdata) / "ArmaTools"
                      : std::filesystem::temp_directory_path() / "ArmaTools";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    const auto log_path = log_dir / "arma-tools-stderr.log";
    std::fflush(stderr);
    if (std::freopen(log_path.string().c_str(), "a", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
}
}  // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    setup_gtk_runtime_env();
    setup_stderr_log();
#else
    gtk_init();
#endif
    Glib::add_exception_handler([]() {
        try {
            throw;
        } catch (const std::exception& e) {
            armatools::cli::log_error("[gui] Unhandled exception in GTK callback: " + std::string(e.what()));
        } catch (...) {
            armatools::cli::log_error("[gui] Unhandled non-std exception in GTK callback");
        }
    });

    adw_init();
    panel_init();

    auto app = Gtk::Application::create("com.armatools.gui");

    std::unique_ptr<AppWindow> window;

    app->signal_activate().connect([&]() {
        if (!window) {
            // Global app stylesheet from GResource.
            try {
                g_resources_register(arma_tools_get_resource());
                auto css = Gtk::CssProvider::create();
                css->load_from_resource("/com/bigbangit/ArmaTools/css/style.css");
                auto display = Gdk::Display::get_default();
                if (display) {
                    Gtk::StyleContext::add_provider_for_display(
                        display, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                } else {
                    armatools::cli::log_error("[gui] Warning: No default display found, cannot apply CSS");
                }
            } catch (const std::exception& e) {
                armatools::cli::log_error("[gui] Failed to load style resource: " + std::string(e.what()));
            }
            try {
                armatools::cli::log_debug("[gui] Creating AppWindow...");
                window = std::make_unique<AppWindow>(app->gobj());
                armatools::cli::log_debug("[gui] AppWindow created successfully");
            } catch (const std::exception& e) {
                armatools::cli::log_error("[gui] Exception in AppWindow: " + std::string(e.what()));
            }
        }
        window->present();
    });

    try {
        return app->run(argc, argv);
    } catch (const std::exception& e) {
        armatools::cli::log_error("[gui] Fatal exception in main loop: " + std::string(e.what()));
    } catch (...) {
        armatools::cli::log_error("[gui] Fatal non-std exception in main loop");
    }
    return 1;
}
