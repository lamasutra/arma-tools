#include "app/tab_config_presenter.h"

#include <gtest/gtest.h>

TEST(TabConfigPresenterTest, EnsureInitializedCallsConfigExactlyOnce) {
    TabConfigPresenter presenter;
    Config cfg;

    int calls = 0;
    presenter.register_tab("p3d-info", [&calls](Config*) { ++calls; });

    EXPECT_TRUE(presenter.ensure_initialized("p3d-info", &cfg));
    EXPECT_FALSE(presenter.ensure_initialized("p3d-info", &cfg));
    EXPECT_EQ(calls, 1);
}

TEST(TabConfigPresenterTest, ApplyToInitializedSkipsUninitializedTabs) {
    TabConfigPresenter presenter;
    Config cfg;

    int configured_a = 0;
    int configured_b = 0;

    presenter.register_tab("a", [&configured_a](Config*) { ++configured_a; });
    presenter.register_tab("b", [&configured_b](Config*) { ++configured_b; });

    EXPECT_TRUE(presenter.ensure_initialized("a", &cfg));
    presenter.apply_to_initialized(&cfg);

    EXPECT_EQ(configured_a, 2);
    EXPECT_EQ(configured_b, 0);
}

TEST(TabConfigPresenterTest, ResetClearsInitializationState) {
    TabConfigPresenter presenter;
    Config cfg;

    int calls = 0;
    presenter.register_tab("audio", [&calls](Config*) { ++calls; });

    EXPECT_TRUE(presenter.ensure_initialized("audio", &cfg));
    presenter.reset();
    EXPECT_TRUE(presenter.ensure_initialized("audio", &cfg));
    EXPECT_EQ(calls, 2);
}

TEST(TabConfigPresenterTest, UnknownTabIsIgnored) {
    TabConfigPresenter presenter;
    Config cfg;

    EXPECT_FALSE(presenter.ensure_initialized("missing", &cfg));
    EXPECT_FALSE(presenter.is_initialized("missing"));
}
