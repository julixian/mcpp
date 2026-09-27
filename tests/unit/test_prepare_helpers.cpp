// Unit tests for the pure helpers the phases of prepare_build share
// (src/build/prepare/config.cpp and fetch.cpp): the --features request
// grammar, the macro name a define entry names, the previous release's
// reading of a flag element, and whether a git remote is local. Before the
// decomposition of prepare.cppm these were internal to one 16,000-line unit
// and reached only through whole builds; they are exported for this file.

#include <gtest/gtest.h>

import std;
import mcpp.build.prepare;

using Words = std::vector<std::string>;

TEST(FeatureRequest, TokensSplitOnCommasAndSpaces) {
    EXPECT_EQ(mcpp::build::feature_request_tokens("a,b c,,  d"), (Words{"a", "b", "c", "d"}));
    EXPECT_TRUE(mcpp::build::feature_request_tokens("").empty());
    EXPECT_TRUE(mcpp::build::feature_request_tokens(" , ").empty());
}

// #649 E8: a token with `/` opens a dependency's feature, so it is a forward
// of the root and never one of the root's own features. A token with an
// empty half stays whole, for the caller to name.
TEST(FeatureRequest, ASlashTokenIsAForwardAndNotARootFeature) {
    constexpr std::string_view request = "simd, dep/fast,  gpu,/x,y/";
    EXPECT_EQ(mcpp::build::parse_feature_request(request), (Words{"simd", "gpu"}));
    EXPECT_EQ(mcpp::build::feature_forward_request_tokens(request),
              (Words{"dep/fast", "/x", "y/"}));
}

TEST(DefineName, IsTheTextBeforeTheFirstEquals) {
    EXPECT_EQ(mcpp::build::define_name("NDEBUG"), "NDEBUG");
    EXPECT_EQ(mcpp::build::define_name("VERSION=1"), "VERSION");
    EXPECT_EQ(mcpp::build::define_name("EXPR=a=b"), "EXPR");
    EXPECT_EQ(mcpp::build::define_name("=x"), "");
}

// The words an element reached the compiler as before flag_words: `$$` was a
// literal dollar, and `${name}` or `$name` a ninja variable, empty on a
// compile edge. A `defines` entry was read as `-D<entry>`.
TEST(PreviousReleaseWords, ReadNinjaEscapesAndVariablesAsTheOldEdgeDid) {
    EXPECT_EQ(mcpp::build::previous_release_words("-DA=x$$y", false), (Words{"-DA=x$y"}));
    EXPECT_EQ(mcpp::build::previous_release_words("-DA=${v}z", false), (Words{"-DA=z"}));
    EXPECT_EQ(mcpp::build::previous_release_words("-DA=$v", false), (Words{"-DA="}));
    EXPECT_EQ(mcpp::build::previous_release_words("B=1", true), (Words{"-DB=1"}));
}

// A remote served by plain filesystem reads is local, so `--offline` keeps
// building it; a scheme or scp-like syntax is remote. A Windows drive letter
// has a colon but no `@`, so an existing `C:\repo` stays local.
TEST(GitRemote, LocalMeansAFileUrlOrAnExistingPath) {
    EXPECT_TRUE(mcpp::build::is_local_git_remote("file:///srv/repo.git"));
    EXPECT_FALSE(mcpp::build::is_local_git_remote("https://github.com/x/y.git"));
    EXPECT_FALSE(mcpp::build::is_local_git_remote("ssh://git@host/x.git"));
    EXPECT_FALSE(mcpp::build::is_local_git_remote("git@github.com:x/y.git"));

    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path()
        / std::format("mcpp-prepare-helpers-{:x}", std::random_device{}());
    fs::create_directories(dir);
    EXPECT_TRUE(mcpp::build::is_local_git_remote(dir.string()));
    EXPECT_FALSE(mcpp::build::is_local_git_remote((dir / "missing").string()));
    std::error_code ec;
    fs::remove_all(dir, ec);
}
