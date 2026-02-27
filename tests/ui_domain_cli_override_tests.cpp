#include "ui_domain/ui_cli_override.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

TEST(UiDomainCliOverrideTests, ParsesEqualsSyntaxAndStripsArgument) {
    std::array<char*, 5> argv = {
        const_cast<char*>("app"),
        const_cast<char*>("--ui=imgui"),
        const_cast<char*>("--renderer=gles"),
        nullptr,
        nullptr,
    };
    int argc = 3;

    const auto result = ui_domain::parse_ui_override_and_strip_args(&argc, argv.data());

    EXPECT_TRUE(result.has_ui_override);
    EXPECT_EQ(result.ui_backend, "imgui");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ(argv[1], "--renderer=gles");
}

TEST(UiDomainCliOverrideTests, ParsesSplitSyntaxAndStripsBothTokens) {
    std::array<char*, 6> argv = {
        const_cast<char*>("app"),
        const_cast<char*>("--ui"),
        const_cast<char*>("gtk"),
        const_cast<char*>("--renderer=gles"),
        nullptr,
        nullptr,
    };
    int argc = 4;

    const auto result = ui_domain::parse_ui_override_and_strip_args(&argc, argv.data());

    EXPECT_TRUE(result.has_ui_override);
    EXPECT_EQ(result.ui_backend, "gtk");
    EXPECT_EQ(argc, 2);
    EXPECT_STREQ(argv[1], "--renderer=gles");
}

TEST(UiDomainCliOverrideTests, ReportsMissingValueForSplitSyntax) {
    std::array<char*, 4> argv = {
        const_cast<char*>("app"),
        const_cast<char*>("--ui"),
        nullptr,
        nullptr,
    };
    int argc = 2;

    const auto result = ui_domain::parse_ui_override_and_strip_args(&argc, argv.data());

    EXPECT_FALSE(result.has_ui_override);
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings[0].find("Missing value"), std::string::npos);
    EXPECT_EQ(argc, 1);
}
