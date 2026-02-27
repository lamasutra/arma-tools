#include "ui_domain/ui_event_adapter.h"

#include <gtest/gtest.h>

TEST(UiEventAdapterTests, BuildsMouseMoveEvent) {
    const auto event = ui_domain::event_adapter::make_mouse_move_event(
        1234u, 7u, 11.5f, 22.5f);
    EXPECT_EQ(event.struct_size, sizeof(ui_event_v1));
    EXPECT_EQ(event.type, UI_EVENT_MOUSE_MOVE);
    EXPECT_EQ(event.timestamp_ns, 1234u);
    EXPECT_EQ(event.modifiers, 7u);
    EXPECT_FLOAT_EQ(event.f0, 11.5f);
    EXPECT_FLOAT_EQ(event.f1, 22.5f);
}

TEST(UiEventAdapterTests, BuildsMouseButtonEvent) {
    const auto event = ui_domain::event_adapter::make_mouse_button_event(
        55u, 3u, 2, true, 1.0f, 2.0f);
    EXPECT_EQ(event.type, UI_EVENT_MOUSE_BUTTON);
    EXPECT_EQ(event.modifiers, 3u);
    EXPECT_EQ(event.i0, 2);
    EXPECT_EQ(event.i1, 1);
    EXPECT_FLOAT_EQ(event.f0, 1.0f);
    EXPECT_FLOAT_EQ(event.f1, 2.0f);
}

TEST(UiEventAdapterTests, BuildsKeyAndTextEvents) {
    const auto key = ui_domain::event_adapter::make_key_event(
        999u, 4u, 65, false);
    EXPECT_EQ(key.type, UI_EVENT_KEY);
    EXPECT_EQ(key.timestamp_ns, 999u);
    EXPECT_EQ(key.modifiers, 4u);
    EXPECT_EQ(key.i0, 65);
    EXPECT_EQ(key.i1, 0);

    const char* text = "A";
    const auto text_event = ui_domain::event_adapter::make_text_input_event(
        1000u, 1u, text);
    EXPECT_EQ(text_event.type, UI_EVENT_TEXT_INPUT);
    EXPECT_EQ(text_event.timestamp_ns, 1000u);
    EXPECT_EQ(text_event.modifiers, 1u);
    EXPECT_STREQ(text_event.text, text);
}

TEST(UiEventAdapterTests, BuildsWheelAndScaleEvents) {
    const auto wheel = ui_domain::event_adapter::make_mouse_wheel_event(
        88u, 2u, 0.25f, -1.0f);
    EXPECT_EQ(wheel.type, UI_EVENT_MOUSE_WHEEL);
    EXPECT_EQ(wheel.modifiers, 2u);
    EXPECT_FLOAT_EQ(wheel.f0, 0.25f);
    EXPECT_FLOAT_EQ(wheel.f1, -1.0f);

    const auto scale = ui_domain::event_adapter::make_dpi_scale_event(77u, 1.5f);
    EXPECT_EQ(scale.type, UI_EVENT_DPI_SCALE);
    EXPECT_EQ(scale.timestamp_ns, 77u);
    EXPECT_FLOAT_EQ(scale.f0, 1.5f);
}
