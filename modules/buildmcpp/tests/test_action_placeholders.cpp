#include <gtest/gtest.h>

import std;
import mcpp.build.directives;
import mcpp.manifest;
import mcpp.source_kind;

namespace dirs = mcpp::build::directives;

namespace {

struct ActionPlaceholders : testing::Test {
    std::filesystem::path root;

    void SetUp() override {
        root = std::filesystem::temp_directory_path() / "mcpp_action_placeholders";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    auto actions(std::vector<std::string> outputs) {
        mcpp::manifest::BuildAction a;
        a.id = "generate";
        a.role = mcpp::manifest::BuildAction::Role::Source;
        a.outputs = std::move(outputs);
        return std::vector{std::move(a)};
    }

    void prepare(std::vector<mcpp::manifest::BuildAction>& a,
                 dirs::ActionPlaceholders& owner) {
        dirs::prepare_actions(a, root, mcpp::extension_table_for({}, {}), owner);
    }
};

} // namespace

TEST_F(ActionPlaceholders, MissingSourcesExistOnlyDuringScanning) {
    auto a = actions({"gen/answer.cpp", "gen/answer.h"});
    {
        dirs::ActionPlaceholders owner;
        prepare(a, owner);
        EXPECT_TRUE(std::filesystem::exists(root / "gen/answer.cpp"));
        EXPECT_FALSE(std::filesystem::exists(root / "gen/answer.h"));
        // Re-adopting an output must not acquire a second owner or erase it.
        prepare(a, owner);
        EXPECT_TRUE(std::filesystem::exists(root / "gen/answer.cpp"));
        owner.clear();
        EXPECT_FALSE(std::filesystem::exists(root / "gen/answer.cpp"));
    }
    EXPECT_FALSE(std::filesystem::exists(root / "gen/answer.cpp"));
}

TEST_F(ActionPlaceholders, ExistingOutputsIncludingEmptySourcesArePreserved) {
    {
        std::ofstream(root / "empty.cpp");
        std::ofstream(root / "answer.cpp") << "int answer() { return 42; }\n";
    }
    const auto emptyTime = std::filesystem::last_write_time(root / "empty.cpp");
    const auto answerTime = std::filesystem::last_write_time(root / "answer.cpp");
    auto a = actions({"empty.cpp", "answer.cpp", "missing.cpp"});
    {
        dirs::ActionPlaceholders owner;
        prepare(a, owner);
    }
    EXPECT_TRUE(std::filesystem::exists(root / "empty.cpp"));
    EXPECT_EQ(std::filesystem::file_size(root / "empty.cpp"), 0u);
    EXPECT_EQ(std::filesystem::last_write_time(root / "empty.cpp"), emptyTime);
    EXPECT_EQ(std::filesystem::last_write_time(root / "answer.cpp"), answerTime);
    EXPECT_GT(std::filesystem::file_size(root / "answer.cpp"), 0u);
    EXPECT_FALSE(std::filesystem::exists(root / "missing.cpp"));
}

TEST_F(ActionPlaceholders, ModuleDeclarationsAreAvailableUntilScopeExit) {
    auto a = actions({"generated.cppm"});
    a[0].provides = {"generated"};
    a[0].imports = {"std"};
    EXPECT_THROW({
        dirs::ActionPlaceholders owner;
        prepare(a, owner);
        std::ifstream file(root / "generated.cppm");
        const std::string content(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>{});
        EXPECT_NE(content.find("import std;"), std::string::npos);
        EXPECT_NE(content.find("export module generated;"), std::string::npos);
        throw std::runtime_error("prepare failed after adopting the output");
    }, std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(root / "generated.cppm"));
}
