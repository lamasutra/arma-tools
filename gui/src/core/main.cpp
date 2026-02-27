#include "app_window.h"
#include "render_domain/rd_backend_selection.h"
#include "render_domain/rd_backend_registry.h"
#include "render_domain/rd_builtin_backends.h"
#include "render_domain/rd_cli_override.h"
#include "render_domain/rd_runtime_config.h"
#include "render_domain/rd_runtime_state.h"

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
#include <string>
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

namespace {

void log_renderer_events(const render_domain::RuntimeState& state) {
    for (const auto& event : state.load_events) {
        const bool non_fatal_info = !event.ok &&
            event.message == "plugin directory does not exist";
        if (event.ok || non_fatal_info) {
            armatools::cli::log_plain(
                "[renderer] source=", event.source_path,
                " backend=", event.backend_id.empty() ? "-" : event.backend_id,
                " status=", event.ok ? "ok" : "info",
                " message=", event.message);
        } else {
            armatools::cli::log_warning(
                "[renderer] source=", event.source_path,
                " backend=", event.backend_id.empty() ? "-" : event.backend_id,
                " status=error message=", event.message);
        }
    }

    for (const auto& backend : state.backends) {
        armatools::cli::log_plain(
            "[renderer] detected id=", backend.id,
            " name=", backend.name,
            " available=", backend.probe.available ? "yes" : "no",
            " score=", backend.probe.score,
            " source=", backend.source,
            " reason=", backend.probe.reason.empty() ? "-" : backend.probe.reason);
    }

    if (state.selection.success) {
        armatools::cli::log_plain(
            "[renderer] selected=", state.selection.selected_backend,
            " detail=", state.selection.message);
    } else {
        armatools::cli::log_error("[renderer] selection failed:", state.selection.message);
    }
}

render_domain::RuntimeState initialize_render_domain(int* argc, char** argv) {
    render_domain::RuntimeState state;
    state.config_path = render_domain::runtime_config_path();
    state.plugin_dir = render_domain::default_plugin_dir();

    const auto cli = render_domain::parse_renderer_override_and_strip_args(argc, argv);
    for (const auto& warning : cli.warnings) {
        armatools::cli::log_warning("[renderer]", warning);
    }

    const auto cfg = render_domain::load_runtime_config();
    render_domain::BackendRegistry registry;
    render_domain::register_builtin_backends(registry);
    registry.discover_plugin_backends(state.plugin_dir);

    render_domain::SelectionRequest request;
    request.config_backend = cfg.backend;
    request.has_cli_override = cli.has_renderer_override;
    request.cli_backend = cli.renderer_backend;

    const std::string requested_backend = request.has_cli_override
        ? request.cli_backend
        : request.config_backend;

    state.requested_backend = requested_backend;
    state.requested_from_cli = request.has_cli_override;
    state.selection = render_domain::select_backend(registry, request);
    if (!state.selection.success && state.selection.used_explicit_request) {
        const std::string failure_message = state.selection.message;
        render_domain::SelectionRequest fallback_request;
        fallback_request.config_backend = "auto";
        state.selection = render_domain::select_backend(registry, fallback_request);
        if (state.selection.success) {
            state.selection.message = failure_message + " | fallback: " + state.selection.message;
        }
    }

    state.backends = registry.backends();
    state.load_events = registry.load_events();
    return state;
}

}  // namespace

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

    const auto renderer_state = initialize_render_domain(&argc, argv);
    render_domain::set_runtime_state(renderer_state);
    log_renderer_events(renderer_state);

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
