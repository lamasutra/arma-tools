#include "app/log_panel_presenter.h"

#include <gtest/gtest.h>

TEST(LogPanelPresenterTest, AppendAndExportText) {
    LogPanelPresenter presenter;

    presenter.append(LogLevel::Info, "first\n");
    presenter.append(LogLevel::Error, "second\n");

    EXPECT_EQ(presenter.all_text(), "first\nsecond\n");
}

TEST(LogPanelPresenterTest, VisibleEntriesRespectLevelFilters) {
    LogPanelPresenter presenter;

    presenter.append(LogLevel::Debug, "dbg\n");
    presenter.append(LogLevel::Info, "info\n");
    presenter.append(LogLevel::Warning, "wrn\n");
    presenter.append(LogLevel::Error, "err\n");

    presenter.set_level_visible(LogLevel::Debug, false);
    presenter.set_level_visible(LogLevel::Warning, false);

    const auto visible = presenter.visible_entries();
    ASSERT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0]->line, "info\n");
    EXPECT_EQ(visible[1]->line, "err\n");
}

TEST(LogPanelPresenterTest, ClearDropsEntries) {
    LogPanelPresenter presenter;
    presenter.append(LogLevel::Info, "line\n");

    presenter.clear();

    EXPECT_TRUE(presenter.visible_entries().empty());
    EXPECT_TRUE(presenter.all_text().empty());
}

TEST(LogPanelPresenterTest, SearchAndMaximizeStateAreTracked) {
    LogPanelPresenter presenter;

    presenter.set_search_query("wrp");
    EXPECT_EQ(presenter.search_query(), "wrp");

    EXPECT_TRUE(presenter.set_maximized(true));
    EXPECT_FALSE(presenter.set_maximized(true));
    EXPECT_TRUE(presenter.maximized());
    EXPECT_TRUE(presenter.set_maximized(false));
    EXPECT_FALSE(presenter.maximized());
}
