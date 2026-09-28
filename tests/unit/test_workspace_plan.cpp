#include <gtest/gtest.h>

import std;
import mcpp.manifest;
import mcpp.project;

// The unit-level statements of a workspace plan (workspace design 2026-09-29
// §15): which member values separate two plans, what the virtual root holds,
// and where a member's products are placed. The end-to-end halves are
// tests/e2e/834_a_workspace_is_one_graph_per_configuration.sh and its
// neighbours.

namespace {

mcpp::manifest::Manifest member(std::string name) {
    mcpp::manifest::Manifest m;
    m.package.name = std::move(name);
    m.package.version = "0.1.0";
    return m;
}

mcpp::project::WorkspaceMember listed(std::string path, std::string ns, std::string name) {
    return {.memberPath = path, .dir = path, .namespace_ = std::move(ns),
            .name = std::move(name)};
}

} // namespace

// A member's own flags, sources and dependencies are attributes of its node:
// they never separate it from another member.
TEST(WorkspacePlan, PackageAttributesDoNotSeparateMembers) {
    auto a = member("a");
    auto b = member("b");
    b.buildConfig.cxxflags = {"-DONLY_B=1"};
    b.buildConfig.ldflags = {"-lm"};
    b.buildConfig.sources = {"src/**/*.cpp"};
    b.buildConfig.cStandard = "c17";
    b.dependencies["fmt"] = mcpp::manifest::DependencySpec{.version = "11.0.0"};
    EXPECT_EQ(mcpp::project::root_position_key(a), mcpp::project::root_position_key(b));
}

// What every node of one graph shares does separate them: the toolchain, the
// standard, the dialect flags, the target and the profile.
TEST(WorkspacePlan, RootPositionValuesSeparateMembers) {
    const auto ref = mcpp::project::root_position_key(member("a"));
    { auto m = member("a"); m.package.standard = "c++26";
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
    { auto m = member("a"); m.toolchain.byPlatform["default"] = "gcc@16.1.0";
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
    { auto m = member("a"); m.buildConfig.dialectCxxflags = {"-freflection"};
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
    { auto m = member("a"); m.buildConfig.target = "x86_64-linux-musl";
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
    { auto m = member("a"); m.buildConfig.defaultProfile = "release";
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
    { auto m = member("a"); m.buildConfig.cxxRuntime = "static";
      EXPECT_NE(mcpp::project::root_position_key(m), ref); }
}

// The virtual root carries the plan's values and nothing a package owns.
TEST(WorkspacePlan, TheVirtualRootHoldsNoPackageContent) {
    mcpp::manifest::Manifest ws;
    ws.workspace.present = true;
    ws.workspace.members = {"a"};
    auto first = member("a");
    first.package.standard = "c++26";
    first.toolchain.byPlatform["default"] = "gcc@16.1.0";
    first.buildConfig.dialectCxxflags = {"-freflection"};
    first.buildConfig.cxxflags = {"-DMEMBER=1"};
    first.buildConfig.ldflags = {"-lmember"};
    first.buildConfig.sources = {"src/**/*.cpp"};
    first.targets.push_back(mcpp::manifest::Target{.name = "a"});
    first.dependencies["fmt"] = mcpp::manifest::DependencySpec{.version = "11.0.0"};

    const auto v = mcpp::project::virtual_workspace_root(ws, first, "/ws");
    EXPECT_TRUE(v.package.virtualRoot);
    EXPECT_EQ(v.package.standard, "c++26");
    EXPECT_EQ(v.toolchain.byPlatform.at("default"), "gcc@16.1.0");
    EXPECT_EQ(v.buildConfig.dialectCxxflags, first.buildConfig.dialectCxxflags);
    EXPECT_TRUE(v.buildConfig.cxxflags.empty());
    EXPECT_TRUE(v.buildConfig.ldflags.empty());
    EXPECT_TRUE(v.buildConfig.sources.empty());
    EXPECT_TRUE(v.buildConfig.sourcesDeclared);
    EXPECT_TRUE(v.targets.empty());
    EXPECT_TRUE(v.dependencies.empty());
    // The root's values and the group's are one statement.
    EXPECT_EQ(mcpp::project::root_position_key(v), mcpp::project::root_position_key(first));
}

// A member's products are in `bin/<package name>/`, qualified when another
// member of the workspace has the same name; the workspace's own package
// keeps `bin/`.
TEST(WorkspacePlan, ProductDirectoriesAreNamedByPackage) {
    const std::vector<mcpp::project::WorkspaceMember> all = {
        listed("apps/cli", "", "cli"),
        listed("ns1/common", "ns1", "common"),
        listed("ns2/common", "ns2", "common"),
    };
    EXPECT_EQ(mcpp::project::product_directory_name(all, "apps/cli"), "cli");
    EXPECT_EQ(mcpp::project::product_directory_name(all, "ns1/common"), "ns1.common");
    EXPECT_EQ(mcpp::project::product_directory_name(all, "ns2/common"), "ns2.common");
    EXPECT_EQ(mcpp::project::product_directory_name(all, "."), "");
}
