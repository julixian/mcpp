#include <gtest/gtest.h>

import std;
import mcpp.ui;

// Closing notices are advisories about the run as a whole. They are printed
// once, after the command's own output, as `tip:` lines on stderr; a command
// that writes an envelope takes them first and reports them as notes.

TEST(ClosingNotices, AreDeduplicatedAndTakenOnce) {
    (void)mcpp::ui::take_closing_notices();
    mcpp::ui::add_closing_notice("CODE_A", "the index needs a newer mcpp");
    mcpp::ui::add_closing_notice("CODE_A", "the index needs a newer mcpp");
    mcpp::ui::add_closing_notice("CODE_B", "a second notice");
    auto taken = mcpp::ui::take_closing_notices();
    ASSERT_EQ(taken.size(), 2u);
    EXPECT_EQ(taken[0].code, "CODE_A");
    EXPECT_EQ(taken[1].message, "a second notice");
    EXPECT_TRUE(mcpp::ui::take_closing_notices().empty());
}

TEST(ClosingNotices, PrintAsTipLinesOnStderrAndThenAreGone) {
    (void)mcpp::ui::take_closing_notices();
    mcpp::ui::disable_color();
    mcpp::ui::add_closing_notice("CODE", "upgrade to see newer packages");
    testing::internal::CaptureStderr();
    mcpp::ui::print_closing_notices();
    mcpp::ui::print_closing_notices();
    auto err = testing::internal::GetCapturedStderr();
    EXPECT_EQ(err, "tip: upgrade to see newer packages\n");
}

TEST(ClosingNotices, QuietPrintsNothingButStillConsumes) {
    (void)mcpp::ui::take_closing_notices();
    mcpp::ui::add_closing_notice("CODE", "not shown under --quiet");
    mcpp::ui::set_quiet(true);
    testing::internal::CaptureStderr();
    mcpp::ui::print_closing_notices();
    auto err = testing::internal::GetCapturedStderr();
    mcpp::ui::set_quiet(false);
    EXPECT_TRUE(err.empty()) << err;
    EXPECT_TRUE(mcpp::ui::take_closing_notices().empty());
}
