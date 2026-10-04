#include <gtest/gtest.h>

import std;
import mcpp.build.directives;
import mcpp.toolchain.fingerprint;

namespace dirs = mcpp::build::directives;

namespace {

struct GlobInputs : testing::Test {
    std::filesystem::path tree;
    std::filesystem::path root;

    void SetUp() override {
        tree = std::filesystem::temp_directory_path() / "mcpp_parent_glob_inputs";
        std::filesystem::remove_all(tree);
        root = tree / "workspace" / "app";
        std::filesystem::create_directories(root);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tree, ec);
    }

    void write(const std::filesystem::path& relative, std::string_view content = "x") {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    std::string fingerprint(std::string_view pattern) {
        return dirs::glob_fingerprint(root, pattern, "target");
    }
};

} // namespace

TEST_F(GlobInputs, ParentDirectoryMembershipInvalidatesTheFingerprint) {
    write("../inputs/a.in");
    const auto one = fingerprint("../inputs/**/*.in");
    EXPECT_NE(one, mcpp::toolchain::hash_string(""));

    write("../inputs/nested/b.in");
    EXPECT_NE(fingerprint("../inputs/**/*.in"), one);
    std::filesystem::remove(root / "../inputs/nested/b.in");
    EXPECT_EQ(fingerprint("../inputs/**/*.in"), one);

    write("../inputs/a.in", "different contents");
    write("../inputs/ignored.txt");
    write("../unrelated/c.in");
    EXPECT_EQ(fingerprint("../inputs/**/*.in"), one);
}

TEST_F(GlobInputs, MultipleParentLevelsAndExactPathsAreWatched) {
    const auto before = fingerprint("../../inputs/*.in");
    write("../../inputs/a.in");
    EXPECT_NE(fingerprint("../../inputs/*.in"), before);
    EXPECT_NE(fingerprint("../../inputs/a.in"), before);
    std::filesystem::remove(root / "../../inputs/a.in");
    EXPECT_EQ(fingerprint("../../inputs/*.in"), before);
}

TEST_F(GlobInputs, MissingLiteralDirectoryDoesNotHideLaterFiles) {
    const auto before = fingerprint("../missing/**/*.in");
    write("../missing/ignored.txt");
    EXPECT_EQ(fingerprint("../missing/**/*.in"), before);
    write("../missing/nested/a.in");
    EXPECT_NE(fingerprint("../missing/**/*.in"), before);
}

TEST_F(GlobInputs, LiteralPrefixDoesNotBypassExcludedDirectories) {
    const auto before = fingerprint("**");
    write("target/generated.in");
    write(".git/index.in");
    write("../inputs/target/generated.in");
    write("../inputs/.git/index.in");
    EXPECT_EQ(fingerprint("**"), before);
    for (const auto pattern : {"target/**", ".git/**", "../inputs/target/**",
                               "../inputs/.git/**", "../inputs/**"})
        EXPECT_EQ(fingerprint(pattern), mcpp::toolchain::hash_string("")) << pattern;
}

TEST_F(GlobInputs, ParentPatternsRetainPathSetSemantics) {
    write("../inputs/a.in");
    const auto before = fingerprint("../inputs/**");
    write("../inputs/a.in", "longer content");
    std::filesystem::last_write_time(root / "../inputs/a.in",
        std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    EXPECT_EQ(fingerprint("../inputs/**"), before);
    write("../inputs/nested/b.in");
    EXPECT_NE(fingerprint("../inputs/**"), before);
}

TEST_F(GlobInputs, DirectorySymlinksAreNotFollowedThroughTheLiteralPrefix) {
    write("../real/nested/a.in");
    std::error_code ec;
    std::filesystem::create_directory_symlink(tree / "workspace" / "real",
        root / "../link", ec);
    if (ec) GTEST_SKIP() << "Directory symlinks are unavailable: " << ec.message();
    EXPECT_EQ(fingerprint("../link/**/*.in"), mcpp::toolchain::hash_string(""));
    EXPECT_EQ(fingerprint("../link/nested/*.in"), mcpp::toolchain::hash_string(""));
    const auto before = fingerprint("../**/*.in");
    write("../real/nested/b.in");
    EXPECT_NE(fingerprint("../**/*.in"), before);
}
