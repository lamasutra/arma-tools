#include "ui_domain/ui_backend_registry.h"
#include "ui_domain/ui_builtin_backends.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

struct TestUiState {
    bool overlay_enabled = false;
};

ui_backend_probe_result_v1 probe_available() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 1;
    r.score = 90;
    r.reason = "available";
    return r;
}

ui_backend_probe_result_v1 probe_available_plugin() {
    ui_backend_probe_result_v1 r{};
    r.struct_size = sizeof(ui_backend_probe_result_v1);
    r.available = 1;
    r.score = 95;
    r.reason = "plugin available";
    return r;
}

void destroy_backend(void* userdata) {
    delete static_cast<TestUiState*>(userdata);
}

int set_overlay(void* userdata, uint8_t enabled) {
    auto* state = static_cast<TestUiState*>(userdata);
    if (!state) return UI_STATUS_INVALID_ARGUMENT;
    state->overlay_enabled = enabled != 0;
    return UI_STATUS_OK;
}

uint8_t get_overlay(void* userdata) {
    const auto* state = static_cast<const TestUiState*>(userdata);
    return (state && state->overlay_enabled) ? 1 : 0;
}

int noop_resize(void*, uint32_t, uint32_t) {
    return UI_STATUS_OK;
}

int noop_event(void*, const ui_event_v1*) {
    return UI_STATUS_OK;
}

int noop_begin(void*, double) {
    return UI_STATUS_OK;
}

int noop_draw(void*) {
    return UI_STATUS_OK;
}

int noop_end(void*) {
    return UI_STATUS_OK;
}

int create_backend(const ui_backend_create_desc_v1* desc,
                   ui_backend_instance_v1* out_instance) {
    if (!desc || !out_instance) return UI_STATUS_INVALID_ARGUMENT;
    auto* state = new TestUiState{};
    state->overlay_enabled = desc->overlay_enabled != 0;

    out_instance->userdata = state;
    out_instance->destroy = destroy_backend;
    out_instance->resize = noop_resize;
    out_instance->handle_event = noop_event;
    out_instance->begin_frame = noop_begin;
    out_instance->draw = noop_draw;
    out_instance->end_frame = noop_end;
    out_instance->set_overlay_enabled = set_overlay;
    out_instance->get_overlay_enabled = get_overlay;
    return UI_STATUS_OK;
}

const ui_backend_factory_v1 k_factory = {
    UI_ABI_VERSION,
    "gtk",
    "GTK",
    probe_available,
    create_backend,
};

const ui_backend_factory_v1 k_factory_plugin = {
    UI_ABI_VERSION,
    "gtk",
    "GTK Plugin",
    probe_available_plugin,
    create_backend,
};

const ui_backend_factory_v1 k_factory_bad_abi = {
    UI_ABI_VERSION + 1u,
    "gtk",
    "GTK Bad ABI",
    probe_available,
    create_backend,
};

}  // namespace

TEST(UiDomainRegistryTests, CreateInstanceInitializesAndTogglesOverlayState) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory, "test:gtk", false);

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.overlay_enabled = 1;

    ui_domain::BackendInstance instance;
    std::string error;
    ASSERT_TRUE(registry.create_instance("gtk", desc, instance, error)) << error;
    EXPECT_TRUE(instance.valid());
    EXPECT_TRUE(instance.overlay_enabled());

    EXPECT_EQ(instance.set_overlay_enabled(false), UI_STATUS_OK);
    EXPECT_FALSE(instance.overlay_enabled());
}

TEST(UiDomainRegistryTests, PluginFactoryReplacesBuiltinBackendWithSameId) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory, "builtin:gtk", false);
    registry.register_factory(&k_factory_plugin, "plugin:gtk", true);

    const auto& backends = registry.backends();
    ASSERT_EQ(backends.size(), 1u);
    EXPECT_EQ(backends[0].id, "gtk");
    EXPECT_EQ(backends[0].name, "GTK Plugin");
    EXPECT_EQ(backends[0].source, "plugin:gtk");
    EXPECT_TRUE(backends[0].from_plugin);
    EXPECT_EQ(backends[0].probe.score, 95);
    EXPECT_EQ(backends[0].probe.reason, "plugin available");

    const auto& events = registry.load_events();
    ASSERT_GE(events.size(), 2u);
    EXPECT_TRUE(events[0].ok);
    EXPECT_EQ(events[0].message, "loaded");
    EXPECT_TRUE(events[1].ok);
    EXPECT_EQ(events[1].message, "loaded (plugin replaced builtin backend)");
}

TEST(UiDomainRegistryTests, DuplicateBuiltinBackendIdIsRejected) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory, "builtin:gtk:first", false);
    registry.register_factory(&k_factory, "builtin:gtk:duplicate", false);

    const auto& backends = registry.backends();
    ASSERT_EQ(backends.size(), 1u);
    EXPECT_EQ(backends[0].id, "gtk");
    EXPECT_EQ(backends[0].source, "builtin:gtk:first");
    EXPECT_FALSE(backends[0].from_plugin);

    const auto& events = registry.load_events();
    ASSERT_GE(events.size(), 2u);
    EXPECT_FALSE(events[1].ok);
    EXPECT_EQ(events[1].backend_id, "gtk");
    EXPECT_EQ(events[1].message, "duplicate backend id");
}

TEST(UiDomainRegistryTests, RejectsFactoryWithAbiMismatch) {
    ui_domain::BackendRegistry registry;
    registry.register_factory(&k_factory_bad_abi, "bad:abi", true);

    EXPECT_TRUE(registry.backends().empty());
    const auto& events = registry.load_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "ABI mismatch");
}

TEST(UiDomainRegistryTests, DiscoverPluginBackendsReportsMissingDirectory) {
    ui_domain::BackendRegistry registry;
    const auto missing_dir = std::filesystem::temp_directory_path() /
        "arma-tools-ui-registry-missing-dir-12345";
    std::error_code ec;
    std::filesystem::remove_all(missing_dir, ec);
    registry.discover_plugin_backends(missing_dir);

    const auto& events = registry.load_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].source_path, missing_dir.string());
    EXPECT_EQ(events[0].message, "plugin directory does not exist");
}

TEST(UiDomainRegistryTests, BuiltinImGuiBackendRequiresAvailableBridge) {
    ui_domain::BackendRegistry registry;
    ui_domain::register_builtin_backends(registry);

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.overlay_enabled = 1;

    ui_domain::BackendInstance instance_without_bridge;
    std::string error_without_bridge;
    EXPECT_FALSE(registry.create_instance(
        "imgui", desc, instance_without_bridge, error_without_bridge));

    ui_render_bridge_v1 bridge{};
    bridge.struct_size = sizeof(ui_render_bridge_v1);
    bridge.abi_version = UI_RENDER_BRIDGE_ABI_VERSION;
    bridge.userdata = nullptr;
    bridge.begin_frame = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.submit_draw_data = +[](void*, const ui_draw_data_v1*) -> int { return UI_STATUS_OK; };
    bridge.draw_overlay = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.end_frame = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.is_available = +[](void*) -> uint8_t { return 1; };
    desc.render_bridge = &bridge;

    ui_domain::BackendInstance instance_with_bridge;
    std::string error_with_bridge;
    EXPECT_TRUE(registry.create_instance(
        "imgui", desc, instance_with_bridge, error_with_bridge))
        << error_with_bridge;
}

TEST(UiDomainRegistryTests, BuiltinImGuiBackendConsumesOverlayInputAndAppliesScale) {
    ui_domain::BackendRegistry registry;
    ui_domain::register_builtin_backends(registry);

    struct BridgeCapture {
        int submit_calls = 0;
        uint32_t command_count = 0;
        ui_draw_cmd_v1 first_cmd{};
    } capture;

    ui_render_bridge_v1 bridge{};
    bridge.struct_size = sizeof(ui_render_bridge_v1);
    bridge.abi_version = UI_RENDER_BRIDGE_ABI_VERSION;
    bridge.userdata = &capture;
    bridge.begin_frame = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.submit_draw_data = +[](void* userdata, const ui_draw_data_v1* draw_data) -> int {
        auto* state = static_cast<BridgeCapture*>(userdata);
        if (!state || !draw_data) return UI_STATUS_INVALID_ARGUMENT;
        state->submit_calls++;
        state->command_count = draw_data->command_count;
        if (draw_data->command_count > 0 && draw_data->commands) {
            state->first_cmd = draw_data->commands[0];
        }
        return UI_STATUS_OK;
    };
    bridge.draw_overlay = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.end_frame = +[](void*) -> int { return UI_STATUS_OK; };
    bridge.is_available = +[](void*) -> uint8_t { return 1; };

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.overlay_enabled = 1;
    desc.render_bridge = &bridge;

    ui_domain::BackendInstance instance;
    std::string error;
    ASSERT_TRUE(registry.create_instance("imgui", desc, instance, error)) << error;

    ui_event_v1 scale_event{};
    scale_event.struct_size = sizeof(ui_event_v1);
    scale_event.type = UI_EVENT_DPI_SCALE;
    scale_event.f0 = 2.0f;
    EXPECT_EQ(instance.handle_event(&scale_event), UI_STATUS_OK);

    EXPECT_EQ(instance.begin_frame(1.0 / 60.0), UI_STATUS_OK);
    EXPECT_EQ(instance.draw(), UI_STATUS_OK);
    EXPECT_EQ(instance.end_frame(), UI_STATUS_OK);
    EXPECT_GT(capture.submit_calls, 0);
    ASSERT_GT(capture.command_count, 0u);
    const float drawn_h = capture.first_cmd.clip_rect_y2 - capture.first_cmd.clip_rect_y1;
    EXPECT_GT(drawn_h, 100.0f);

    ui_event_v1 inside_event{};
    inside_event.struct_size = sizeof(ui_event_v1);
    inside_event.type = UI_EVENT_MOUSE_MOVE;
    inside_event.f0 = 48.0f;
    inside_event.f1 = 48.0f;
    EXPECT_EQ(instance.handle_event(&inside_event), UI_STATUS_EVENT_CONSUMED);

    ui_event_v1 outside_event{};
    outside_event.struct_size = sizeof(ui_event_v1);
    outside_event.type = UI_EVENT_MOUSE_MOVE;
    outside_event.f0 = 4.0f;
    outside_event.f1 = 4.0f;
    EXPECT_EQ(instance.handle_event(&outside_event), UI_STATUS_OK);
}

TEST(UiDomainRegistryTests, BuiltinGtkBackendRequestsHostWindowThroughBridge) {
    ui_domain::BackendRegistry registry;
    ui_domain::register_builtin_backends(registry);

    struct HostCapture {
        int ensure_calls = 0;
        int present_calls = 0;
        int shutdown_calls = 0;
    } host_capture;

    ui_host_bridge_v1 host_bridge{};
    host_bridge.struct_size = sizeof(ui_host_bridge_v1);
    host_bridge.abi_version = UI_HOST_BRIDGE_ABI_VERSION;
    host_bridge.userdata = &host_capture;
    host_bridge.ensure_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->ensure_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.present_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->present_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.shutdown_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->shutdown_calls++;
        return UI_STATUS_OK;
    };

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.host_bridge = &host_bridge;

    {
        ui_domain::BackendInstance instance;
        std::string error;
        ASSERT_TRUE(registry.create_instance("gtk", desc, instance, error)) << error;
        EXPECT_EQ(host_capture.ensure_calls, 1);
        EXPECT_EQ(host_capture.present_calls, 1);
        EXPECT_EQ(host_capture.shutdown_calls, 0);
    }
    EXPECT_EQ(host_capture.shutdown_calls, 1);
}

TEST(UiDomainRegistryTests, BuiltinImGuiBackendRequestsHostWindowWithoutOwningShutdown) {
    ui_domain::BackendRegistry registry;
    ui_domain::register_builtin_backends(registry);

    struct HostCapture {
        int ensure_calls = 0;
        int present_calls = 0;
        int shutdown_calls = 0;
    } host_capture;

    ui_host_bridge_v1 host_bridge{};
    host_bridge.struct_size = sizeof(ui_host_bridge_v1);
    host_bridge.abi_version = UI_HOST_BRIDGE_ABI_VERSION;
    host_bridge.userdata = &host_capture;
    host_bridge.ensure_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->ensure_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.present_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->present_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.shutdown_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->shutdown_calls++;
        return UI_STATUS_OK;
    };

    ui_render_bridge_v1 render_bridge{};
    render_bridge.struct_size = sizeof(ui_render_bridge_v1);
    render_bridge.abi_version = UI_RENDER_BRIDGE_ABI_VERSION;
    render_bridge.begin_frame = +[](void*) -> int { return UI_STATUS_OK; };
    render_bridge.submit_draw_data = +[](void*, const ui_draw_data_v1*) -> int {
        return UI_STATUS_OK;
    };
    render_bridge.draw_overlay = +[](void*) -> int { return UI_STATUS_OK; };
    render_bridge.end_frame = +[](void*) -> int { return UI_STATUS_OK; };
    render_bridge.is_available = +[](void*) -> uint8_t { return 1; };

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.render_bridge = &render_bridge;
    desc.host_bridge = &host_bridge;

    {
        ui_domain::BackendInstance instance;
        std::string error;
        ASSERT_TRUE(registry.create_instance("imgui", desc, instance, error)) << error;
        EXPECT_EQ(host_capture.ensure_calls, 1);
        EXPECT_EQ(host_capture.present_calls, 1);
        EXPECT_EQ(host_capture.shutdown_calls, 0);
    }

    // ImGui backend requests startup but does not own host window teardown.
    EXPECT_EQ(host_capture.shutdown_calls, 0);
}

TEST(UiDomainRegistryTests, BuiltinNullBackendDoesNotTouchHostWindowBridge) {
    ui_domain::BackendRegistry registry;
    ui_domain::register_builtin_backends(registry);

    struct HostCapture {
        int ensure_calls = 0;
        int present_calls = 0;
        int shutdown_calls = 0;
    } host_capture;

    ui_host_bridge_v1 host_bridge{};
    host_bridge.struct_size = sizeof(ui_host_bridge_v1);
    host_bridge.abi_version = UI_HOST_BRIDGE_ABI_VERSION;
    host_bridge.userdata = &host_capture;
    host_bridge.ensure_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->ensure_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.present_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->present_calls++;
        return UI_STATUS_OK;
    };
    host_bridge.shutdown_main_window = +[](void* userdata) -> int {
        auto* state = static_cast<HostCapture*>(userdata);
        if (!state) return UI_STATUS_INVALID_ARGUMENT;
        state->shutdown_calls++;
        return UI_STATUS_OK;
    };

    ui_backend_create_desc_v1 desc{};
    desc.struct_size = sizeof(ui_backend_create_desc_v1);
    desc.host_bridge = &host_bridge;

    {
        ui_domain::BackendInstance instance;
        std::string error;
        ASSERT_TRUE(registry.create_instance("null", desc, instance, error)) << error;
        EXPECT_TRUE(instance.valid());
    }

    EXPECT_EQ(host_capture.ensure_calls, 0);
    EXPECT_EQ(host_capture.present_calls, 0);
    EXPECT_EQ(host_capture.shutdown_calls, 0);
}
