#include "app_window.h"
#include "render_domain/rd_backend_selection.h"
#include "render_domain/rd_backend_registry.h"
#include "render_domain/rd_builtin_backends.h"
#include "render_domain/rd_cli_override.h"
#include "render_domain/rd_runtime_config.h"
#include "render_domain/rd_runtime_state.h"
#include "render_domain/rd_ui_render_bridge.h"
#include "ui_domain/ui_backend_selection.h"
#include "ui_domain/ui_backend_registry.h"
#include "ui_domain/ui_builtin_backends.h"
#include "ui_domain/ui_cli_override.h"
#include "ui_domain/ui_runtime_config.h"
#include "ui_domain/ui_runtime_state.h"

#include <adwaita.h>
#include <libpanel.h>
#include <gtkmm.h>

#include <exception>
#include <iostream>
#include <cctype>
#include <cstdlib>
#ifdef _WIN32
#include <cstdio>
#include <filesystem>
#include <windows.h>
#endif

#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include "cli_logger.h"
#include "armatools/version.h"

// Declared in a generated file (arma-tools-resources.c), this function returns
// the compiled GResource bundle that contains CSS, icons, and other embedded assets.
extern "C" GResource* arma_tools_get_resource(void);

#ifdef _WIN32
// Windows-only helpers: GTK on Windows needs environment variables set to find
// its compiled GSettings schemas. Without GSETTINGS_SCHEMA_DIR pointing to the
// right place, GTK widgets that use settings (e.g. Adwaita) will not work correctly.
namespace {
// Returns the directory that contains the currently running executable.
// Used to locate the GLib schema directory relative to the install prefix.
std::filesystem::path get_executable_dir() {
    wchar_t module_path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(module_path).parent_path();
}

// Sets up Windows-specific GTK environment variables before GTK initializes.
// Checks two common install layouts (prefix/share and exe-dir/share).
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
    // Force the local VFS implementation: avoids GVfs/D-Bus issues on Windows.
    SetEnvironmentVariableW(L"GIO_USE_VFS", L"local");
}

// Redirects stderr to a log file in %LOCALAPPDATA%\ArmaTools\arma-tools-stderr.log.
// On Windows, stderr output is invisible in GUI mode, so this file is the only
// way to capture error output from the application and linked libraries.
void setup_stderr_log() {
    const char* local_appdata = std::getenv("LOCALAPPDATA");
    std::filesystem::path log_dir =
        local_appdata ? std::filesystem::path(local_appdata) / "ArmaTools"
                      : std::filesystem::temp_directory_path() / "ArmaTools";
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    const auto log_path = log_dir / "arma-tools-stderr.log";
    std::fflush(stderr);
    // Reopen stderr in append mode and disable buffering so every line
    // appears in the file immediately (even if the app crashes).
    if (std::freopen(log_path.string().c_str(), "a", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
}
}  // namespace
#endif

namespace {

// Prints a usage summary and a list of all known renderer/UI backends to stdout.
// Called when the user passes -h / --help on the command line.
void print_help(const char* prog_name) {
    std::cout << "Arma Tools v" << armatools::version_string() << "\n\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help               Show this help message\n"
              << "  -v, --version            Show version information\n"
              << "  --renderer=BACKEND       Force a specific renderer backend\n"
              << "  --ui=BACKEND             Force a specific UI backend\n"
              << "\n"
              << "Available Renderer Backends:\n";

    render_domain::BackendRegistry renderer_registry;
    render_domain::register_builtin_backends(renderer_registry);
    renderer_registry.discover_plugin_backends(render_domain::default_plugin_dir());
    for (const auto& b : renderer_registry.backends()) {
        std::cout << "  - " << b.id << " (" << b.name << ")";
        if (!b.probe.available) std::cout << " [NOT AVAILABLE: " << b.probe.reason << "]";
        std::cout << "\n";
    }

    std::cout << "\nAvailable UI Backends:\n";
    ui_domain::BackendRegistry ui_registry;
    ui_domain::register_builtin_backends(ui_registry);
    ui_registry.discover_plugin_backends(ui_domain::default_plugin_dir());
    for (const auto& b : ui_registry.backends()) {
        std::cout << "  - " << b.id << " (" << b.name << ")";
        if (!b.probe.available) std::cout << " [NOT AVAILABLE: " << b.probe.reason << "]";
        std::cout << "\n";
    }
    std::cout << std::endl;
}

// Converts a backend name to lowercase and replaces an empty string with "auto".
// This ensures that backend IDs are always compared case-insensitively,
// and that an unset environment variable is treated the same as "auto".
std::string normalize_backend_name(std::string backend) {
    for (char& ch : backend) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (backend.empty()) return "auto";
    return backend;
}

// Shared state used by the UI host bridge callbacks below.
// The UI backend (e.g. ImGui) calls these to request window creation/destruction
// without needing direct access to AppWindow or GTK types.
struct UiHostWindowState {
    bool request_main_window = false;     // True if a main window should be created.
    bool request_present = false;         // True if the window should be raised/shown.
    std::unique_ptr<AppWindow>* window_slot = nullptr; // Pointer to the owning window slot.
};

// Called by the UI backend to signal that a main GTK window must exist.
// Sets a flag; the Gtk::Application activate handler reads it and creates AppWindow.
int host_ensure_main_window(void* userdata) {
    auto* state = static_cast<UiHostWindowState*>(userdata);
    if (!state) return UI_STATUS_INVALID_ARGUMENT;
    state->request_main_window = true;
    return UI_STATUS_OK;
}

// Like host_ensure_main_window, but also requests that the window is shown/raised.
int host_present_main_window(void* userdata) {
    auto* state = static_cast<UiHostWindowState*>(userdata);
    if (!state) return UI_STATUS_INVALID_ARGUMENT;
    state->request_main_window = true;
    state->request_present = true;
    return UI_STATUS_OK;
}

// Called by the UI backend to destroy the main window (e.g. when the backend exits).
// Destroys the underlying GtkWindow and resets the AppWindow unique_ptr.
int host_shutdown_main_window(void* userdata) {
    auto* state = static_cast<UiHostWindowState*>(userdata);
    if (!state) return UI_STATUS_INVALID_ARGUMENT;
    state->request_main_window = false;
    state->request_present = false;
    if (state->window_slot && *state->window_slot) {
        auto* gtk_window = (*state->window_slot)->gtk_window();
        if (gtk_window) gtk_window_destroy(gtk_window);
        state->window_slot->reset();
    }
    return UI_STATUS_OK;
}

void log_ui_events(const ui_domain::RuntimeState& state) {
    for (const auto& event : state.load_events) {
        const bool non_fatal_info = !event.ok &&
            event.message == "plugin directory does not exist";
        if (event.ok || non_fatal_info) {
            armatools::cli::log_plain(
                "[ui] source=", event.source_path,
                " backend=", event.backend_id.empty() ? "-" : event.backend_id,
                " status=", event.ok ? "ok" : "info",
                " message=", event.message);
        } else {
            armatools::cli::log_warning(
                "[ui] source=", event.source_path,
                " backend=", event.backend_id.empty() ? "-" : event.backend_id,
                " status=error message=", event.message);
        }
    }

    for (const auto& backend : state.backends) {
        armatools::cli::log_plain(
            "[ui] detected id=", backend.id,
            " name=", backend.name,
            " available=", backend.probe.available ? "yes" : "no",
            " score=", backend.probe.score,
            " source=", backend.source,
            " reason=", backend.probe.reason.empty() ? "-" : backend.probe.reason);
    }

    if (state.selection.success) {
        armatools::cli::log_plain(
            "[ui] selected=", state.selection.selected_backend,
            " detail=", state.selection.message);
        if (state.backend_instance && state.backend_instance->valid()) {
            armatools::cli::log_plain(
                "[ui] instance=", state.backend_instance->backend_id(),
                " overlay=", state.backend_instance->overlay_enabled() ? "on" : "off");
        }
        if (state.overlay_backend_instance && state.overlay_backend_instance->valid()) {
            armatools::cli::log_plain(
                "[ui] overlay-instance=", state.overlay_backend_instance->backend_id(),
                " overlay=", state.overlay_backend_instance->overlay_enabled() ? "on" : "off");
        }
    } else {
        armatools::cli::log_error("[ui] selection failed:", state.selection.message);
    }
}

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

// Initializes the rendering backend system.
//
// The renderer is responsible for drawing 3D content (models, terrain) inside
// GL widget areas. It is separate from the UI system (GTK or ImGui).
//
// This function:
//   1. Parses CLI flags like --renderer=opengl and strips them from argv.
//   2. Loads the saved renderer preference from the runtime config file.
//   3. Discovers built-in + plugin renderer backends.
//   4. Selects the best available backend (or falls back to "auto" if explicit request fails).
//   5. Creates a render bridge so the UI backend can integrate its rendering surface.
//
// Returns a RuntimeState struct that records what happened (selected backend,
// load events, etc.) so it can be logged and accessed later via runtime_state().
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
    const std::string bridge_backend = state.selection.success
        ? state.selection.selected_backend
        : state.requested_backend;
    state.ui_render_bridge = render_domain::make_ui_render_bridge_for_backend(bridge_backend);
    return state;
}

// Initializes the UI backend system.
//
// The UI backend is the widget toolkit used to draw the application's interface.
// Supported backends include:
//   - "gtk": The native GTK4 + Adwaita UI (default on Linux/Windows).
//   - "imgui": An ImGui overlay drawn inside the renderer (for 3D debug panels).
//   - "null": Headless mode — no UI at all (useful for automated testing).
//
// This function:
//   1. Parses --ui= CLI flag and strips it from argv.
//   2. Checks for a UI_BACKEND env variable override.
//   3. Discovers built-in + plugin UI backends.
//   4. Selects the best backend, handling the case where imgui is unavailable
//      because no renderer UI bridge exists.
//   5. Optionally creates a secondary "overlay" imgui backend alongside GTK.
//
// The render_bridge connects the renderer to the UI backend so ImGui can be
// drawn inside a GL surface managed by GTK.
// The host_bridge lets the UI backend ask GTK to create/destroy the main window.
ui_domain::RuntimeState initialize_ui_domain(
    int* argc,
    char** argv,
    const std::shared_ptr<render_domain::IUiRenderBridge>& render_bridge,
    const ui_host_bridge_v1* host_bridge) {
    ui_domain::RuntimeState state;
    state.config_path = ui_domain::runtime_config_path();
    state.plugin_dir = ui_domain::default_plugin_dir();

    const auto cli = ui_domain::parse_ui_override_and_strip_args(argc, argv);
    for (const auto& warning : cli.warnings) {
        armatools::cli::log_warning("[ui]", warning);
    }

    const auto cfg = ui_domain::load_runtime_config();
    auto registry = std::make_shared<ui_domain::BackendRegistry>();
    ui_domain::register_builtin_backends(*registry);
    registry->discover_plugin_backends(state.plugin_dir);

    ui_domain::SelectionRequest request;
    request.config_backend = cfg.preferred;
    request.has_cli_override = cli.has_ui_override;
    request.cli_backend = cli.ui_backend;

    const char* env_backend = std::getenv("UI_BACKEND");
    if (env_backend && env_backend[0] != '\0') {
        request.has_env_override = true;
        request.env_backend = normalize_backend_name(env_backend);
    }

    std::string requested_backend = request.config_backend;
    if (request.has_env_override) requested_backend = request.env_backend;
    if (request.has_cli_override) requested_backend = request.cli_backend;

    const bool bridge_available =
        render_bridge && render_bridge->info().available && render_bridge->bridge_abi();
    const std::string bridge_reason =
        render_bridge ? render_bridge->info().reason : std::string("renderer bridge missing");

    state.requested_backend = requested_backend;
    state.requested_from_cli = request.has_cli_override;
    state.requested_from_env = request.has_env_override && !request.has_cli_override;
    state.selection = ui_domain::select_backend(*registry, request);

    auto find_best_non_imgui = [&](const std::vector<ui_domain::BackendRecord>& backends)
        -> const ui_domain::BackendRecord* {
        const ui_domain::BackendRecord* best = nullptr;
        for (const auto& backend : backends) {
            if (backend.id == "imgui") continue;
            if (!backend.probe.available) continue;
            if (!best ||
                backend.probe.score > best->probe.score ||
                (backend.probe.score == best->probe.score && backend.id < best->id)) {
                best = &backend;
            }
        }
        return best;
    };

    if (state.selection.success &&
        state.selection.selected_backend == "imgui" &&
        !bridge_available) {
        if (state.selection.used_explicit_request) {
            state.selection.success = false;
            state.selection.message =
                "Requested UI backend 'imgui' is unavailable: renderer UI bridge unavailable (" +
                (bridge_reason.empty() ? std::string("-") : bridge_reason) + ")";
        } else {
            const auto* fallback = find_best_non_imgui(registry->backends());
            if (fallback) {
                state.selection.selected_backend = fallback->id;
                state.selection.message +=
                    " | fallback: imgui unavailable without renderer UI bridge (" +
                    (bridge_reason.empty() ? std::string("-") : bridge_reason) +
                    "); selected '" + fallback->id + "'";
            } else {
                state.selection.success = false;
                state.selection.message =
                    "UI auto-selection chose 'imgui', but renderer UI bridge is unavailable (" +
                    (bridge_reason.empty() ? std::string("-") : bridge_reason) +
                    ") and no fallback UI backend is available";
            }
        }
    }

    if (state.selection.success) {
        auto* bridge_abi = bridge_available
            ? const_cast<ui_render_bridge_v1*>(render_bridge->bridge_abi())
            : nullptr;

        ui_backend_create_desc_v1 create_desc{};
        create_desc.struct_size = sizeof(ui_backend_create_desc_v1);
        // `imgui_overlay` controls the GTK+ImGui companion overlay mode.
        // When imgui is the primary UI backend, rendering must remain enabled.
        create_desc.overlay_enabled =
            (state.selection.selected_backend == "imgui") ? 1 : 0;
        create_desc.render_bridge = bridge_abi;
        create_desc.host_bridge = const_cast<ui_host_bridge_v1*>(host_bridge);

        auto instance = std::make_shared<ui_domain::BackendInstance>();
        std::string create_error;
        if (!registry->create_instance(state.selection.selected_backend, create_desc,
                                       *instance, create_error)) {
            state.selection.success = false;
            state.selection.message = "Failed to create UI backend '" +
                state.selection.selected_backend + "': " + create_error;
        } else {
            state.backend_instance = std::move(instance);

            // Epic 3 overlay path: GTK primary + ImGui companion overlay.
            const bool wants_overlay_companion =
                cfg.imgui_overlay_enabled &&
                state.selection.selected_backend == "gtk";
            if (wants_overlay_companion) {
                if (!bridge_available) {
                    state.load_events.push_back({
                        "overlay:imgui",
                        "imgui",
                        false,
                        "overlay disabled: renderer UI bridge unavailable",
                    });
                } else {
                    const auto& backends = registry->backends();
                    const auto imgui_backend = std::find_if(
                        backends.begin(), backends.end(),
                        [](const ui_domain::BackendRecord& backend) {
                            return backend.id == "imgui";
                        });
                    if (imgui_backend == backends.end() || !imgui_backend->probe.available) {
                        state.load_events.push_back({
                            "overlay:imgui",
                            "imgui",
                            false,
                            "overlay disabled: imgui backend unavailable",
                        });
                    } else {
                        ui_backend_create_desc_v1 overlay_desc{};
                        overlay_desc.struct_size = sizeof(ui_backend_create_desc_v1);
                        overlay_desc.overlay_enabled = 1;
                        overlay_desc.render_bridge = bridge_abi;
                        overlay_desc.host_bridge = const_cast<ui_host_bridge_v1*>(host_bridge);

                        auto overlay_instance = std::make_shared<ui_domain::BackendInstance>();
                        std::string overlay_create_error;
                        if (registry->create_instance("imgui", overlay_desc,
                                                      *overlay_instance, overlay_create_error)) {
                            state.overlay_backend_instance = std::move(overlay_instance);
                            state.overlay_backend_id = "imgui";
                        } else {
                            state.load_events.push_back({
                                "overlay:imgui",
                                "imgui",
                                false,
                                "overlay disabled: " + overlay_create_error,
                            });
                        }
                    }
                }
            }
        }
    }

    state.backends = registry->backends();
    if (!bridge_available) {
        for (auto& backend : state.backends) {
            if (backend.id != "imgui") continue;
            backend.probe.available = false;
            const std::string bridge_unavailable =
                "renderer UI bridge unavailable" +
                std::string(bridge_reason.empty() ? "" : " (" + bridge_reason + ")");
            if (backend.probe.reason.empty()) {
                backend.probe.reason = bridge_unavailable;
            } else {
                backend.probe.reason += "; " + bridge_unavailable;
            }
        }
    }
    {
        const auto& registry_events = registry->load_events();
        state.load_events.insert(state.load_events.end(),
                                 registry_events.begin(), registry_events.end());
    }
    state.registry_owner = std::move(registry);
    return state;
}

}  // namespace

// ============================================================
// Application entry point
// ============================================================
//
// Startup sequence:
//   1. (Windows only) Set up GTK environment & redirect stderr to a log file.
//   2. Handle --help / --version early, before any GTK initialization.
//   3. Initialize the renderer backend (OpenGL / GLES / null).
//   4. Fill in the UI host bridge (callbacks that let the UI backend
//      request window creation/destruction from the GTK side).
//   5. Initialize the UI backend (GTK / ImGui / null).
//   6. Create a Gtk::Application and connect the activate signal.
//   7. In the activate callback: register CSS, create AppWindow, show it.
//   8. Run the GTK main loop (app->run blocks until the user closes the app).
//   9. Tear down runtime singletons and return the exit code.
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Must be called before any GTK function so that GTK can find its schemas.
    setup_gtk_runtime_env();
    // Redirect stderr to a file because Windows GUI apps have no console output.
    setup_stderr_log();
#endif

    // Check for --help / --version before initializing GTK so these flags
    // work even on systems without a display.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "Arma Tools v" << armatools::version_string() << std::endl;
            return 0;
        }
    }

    const auto renderer_state = initialize_render_domain(&argc, argv);
    render_domain::set_runtime_state(renderer_state);
    log_renderer_events(renderer_state);
    if (renderer_state.ui_render_bridge) {
        const auto info = renderer_state.ui_render_bridge->info();
        armatools::cli::log_plain(
            "[ui-bridge] renderer=", info.renderer_backend,
            " bridge=", info.bridge_name,
            " available=", info.available ? "yes" : "no",
            " reason=", info.reason.empty() ? "-" : info.reason);
    }

    UiHostWindowState host_window_state{};
    ui_host_bridge_v1 host_bridge{};
    host_bridge.struct_size = sizeof(ui_host_bridge_v1);
    host_bridge.abi_version = UI_HOST_BRIDGE_ABI_VERSION;
    host_bridge.userdata = &host_window_state;
    host_bridge.ensure_main_window = &host_ensure_main_window;
    host_bridge.present_main_window = &host_present_main_window;
    host_bridge.shutdown_main_window = &host_shutdown_main_window;

    const auto ui_state = initialize_ui_domain(&argc, argv, renderer_state.ui_render_bridge, &host_bridge);
    ui_domain::set_runtime_state(ui_state);
    log_ui_events(ui_state);

    auto cleanup_runtime_and_return = [&](int code) {
        ui_domain::set_runtime_state(ui_domain::RuntimeState{});
        render_domain::set_runtime_state(render_domain::RuntimeState{});
        return code;
    };

    if (!ui_state.selection.success) {
        return cleanup_runtime_and_return(1);
    }
    if (ui_state.selection.selected_backend == "null") {
        armatools::cli::log_plain("[ui] null backend selected; running headless mode");
        return cleanup_runtime_and_return(0);
    }
    if (ui_state.selection.selected_backend == "imgui" &&
        (!renderer_state.ui_render_bridge || !renderer_state.ui_render_bridge->info().available)) {
        armatools::cli::log_warning(
            "[ui] imgui backend selected, but active renderer bridge is unavailable");
    }

    if (!host_window_state.request_main_window &&
        ui_state.selection.selected_backend != "null" &&
        ui_state.selection.selected_backend != "gtk") {
        host_window_state.request_main_window = true;
        host_window_state.request_present = true;
    }
    if (!host_window_state.request_main_window &&
        ui_state.selection.selected_backend == "gtk") {
        armatools::cli::log_warning(
            "[ui] gtk backend did not request main window startup; using compatibility fallback");
        host_window_state.request_main_window = true;
        host_window_state.request_present = true;
    }

#ifndef _WIN32
    if (!gtk_init_check()) {
        const bool explicit_ui = ui_state.selection.used_explicit_request;
        if (explicit_ui) {
            armatools::cli::log_error(
                "[ui] Failed to initialize GTK display for explicit UI backend '" +
                ui_state.selection.selected_backend + "'");
            return cleanup_runtime_and_return(1);
        }
        armatools::cli::log_warning(
            "[ui] Failed to initialize GTK display; falling back to headless null behavior");
        return cleanup_runtime_and_return(0);
    }
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
    host_window_state.window_slot = &window;

    app->signal_activate().connect([&]() {
        if (!host_window_state.request_main_window) {
            return;
        }
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
        if (window && host_window_state.request_present) {
            window->present();
        }
    });

    int exit_code = 1;
    try {
        exit_code = app->run(argc, argv);
    } catch (const std::exception& e) {
        armatools::cli::log_error("[gui] Fatal exception in main loop: " + std::string(e.what()));
    } catch (...) {
        armatools::cli::log_error("[gui] Fatal non-std exception in main loop");
    }

    // Explicitly tear down runtime singletons while host callbacks/userdata are still alive.
    return cleanup_runtime_and_return(exit_code);
}
