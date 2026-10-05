#include <gtest/gtest.h>

import std;
import mcpp.build.plan;
import mcpp.build.ninja;
import mcpp.manifest;
import mcpp.modgraph.scanner;
import mcpp.toolchain.model;

namespace {

mcpp::modgraph::PackageRoot package(const std::filesystem::path& root,
                                   std::string name, std::string flag) {
    mcpp::modgraph::PackageRoot p;
    p.root = root / name;
    p.manifest.package.name = std::move(name);
    p.manifest.package.version = "0.1.0";
    p.linkUsage.ldflags = {std::move(flag)};
    return p;
}

} // namespace

// DLL 的链接配置属于它的包，不能从消费者或其它工作区成员借来。
TEST(SharedLinkUsage, OwnerAndDependencyFlagsReachEverySharedImage) {
    using mcpp::manifest::DependencySpec;
    using mcpp::manifest::Target;
    for (const bool workspace : {false, true}) {
        for (const auto triple : {"x86_64-linux-gnu", "x86_64-pc-windows-msvc"}) {
            const auto root = std::filesystem::temp_directory_path() / "mcpp-shared-link-usage";
            auto app = package(root, "app", "-lconsumer_only");
            auto owner = package(root, "owner", "-lowner_only");
            auto helper = package(root, "helper", "-lhelper_only");
            auto unrelated = package(root, "unrelated", "-lunrelated_only");
            owner.manifest.runtimeConfig.linkIntent.linkLibraryDirs = {"owner-lib"};
            owner.manifest.runtimeConfig.linkIntent.runtimeSearchDirs = {"owner-runtime"};
            owner.manifest.runtimeConfig.requirements.push_back({.kind = "soname", .value = "owner-plugin"});
            owner.manifest.runtimeConfig.artifacts.push_back({.role = "plugin", .path = "owner-plugin"});
            owner.manifest.runtimeConfig.provides = {"owner-capability"};
            helper.manifest.runtimeConfig.linkIntent.libraries = {"helper-runtime-lib"};
            unrelated.manifest.runtimeConfig.linkIntent.linkLibraryDirs = {"unrelated-lib"};
            app.manifest.package.virtualRoot = workspace;
            if (workspace) app.linkUsage.ldflags.clear();
            app.manifest.dependencies["owner"] = DependencySpec{.path = "../owner"};
            app.manifest.dependencies["unrelated"] = DependencySpec{.path = "../unrelated"};
            owner.manifest.dependencies["helper"] = DependencySpec{.path = "../helper"};
            owner.manifest.targets.push_back({.name = "owner_a", .kind = Target::SharedLibrary});
            owner.manifest.targets.push_back({.name = "owner_b", .kind = Target::SharedLibrary});
            owner.selectedMember = workspace;
            owner.memberProducts = "owner";
            std::vector<mcpp::modgraph::PackageRoot> packages = {app, owner, helper, unrelated};
            mcpp::toolchain::Toolchain tc;
            tc.compiler = mcpp::toolchain::CompilerId::Clang;
            tc.targetTriple = triple;
            const auto plan = mcpp::build::make_plan(app.manifest, tc, {}, {}, {}, packages,
                app.root, root / "target", {}, {});
            ASSERT_TRUE(plan.has_value()) << plan.error();
            std::size_t measured = 0;
            for (const auto& unit : plan->linkUnits) {
                if (unit.package != "owner") continue;
                ++measured;
                ASSERT_GE(unit.linkGroup, 0) << unit.targetName << " " << triple;
                const auto& group = plan->linkGroups.at(static_cast<std::size_t>(unit.linkGroup));
                EXPECT_TRUE(group.linkOnly);
                EXPECT_TRUE(group.placements.empty());
                EXPECT_NE(std::ranges::find(group.ldflags, "-lowner_only"), group.ldflags.end());
                EXPECT_NE(std::ranges::find(group.ldflags, "-lhelper_only"), group.ldflags.end());
                EXPECT_EQ(std::ranges::find(group.ldflags, "-lconsumer_only"), group.ldflags.end());
                EXPECT_EQ(std::ranges::find(group.ldflags, "-lunrelated_only"), group.ldflags.end());
                EXPECT_EQ(group.linkIntent.linkLibraryDirs,
                    std::vector<std::filesystem::path>{owner.root / "owner-lib"});
                EXPECT_EQ(group.runtimeLibraryDirs,
                    std::vector<std::filesystem::path>{owner.root / "owner-runtime"});
                EXPECT_EQ(group.linkIntent.libraries, std::vector<std::string>{"helper-runtime-lib"});
                EXPECT_EQ(group.runtimeDlopenLibs, std::vector<std::string>{"owner-plugin"});
                ASSERT_EQ(group.runtimeArtifacts.size(), 1u);
                EXPECT_EQ(group.runtimeArtifacts.front().path, owner.root / "owner-plugin");
                ASSERT_EQ(group.runtimeProviders.size(), 1u);
                EXPECT_EQ(group.runtimeProviders.front().capability, "owner-capability");
            }
            EXPECT_EQ(measured, 2u);
            const auto ninja = mcpp::build::emit_ninja_string(*plan);
            for (const auto& unit : plan->linkUnits) {
                if (unit.package != "owner") continue;
                const auto start = ninja.find("build " + unit.output.generic_string());
                ASSERT_NE(start, std::string::npos);
                const auto next = ninja.find("\nbuild ", start + 1);
                const auto edge = ninja.substr(start, next - start);
                EXPECT_NE(edge.find("-lowner_only"), std::string::npos) << edge;
                EXPECT_NE(edge.find("-lhelper_only"), std::string::npos) << edge;
                EXPECT_EQ(edge.find("-lconsumer_only"), std::string::npos) << edge;
                EXPECT_EQ(edge.find("-lunrelated_only"), std::string::npos) << edge;
            }
        }
    }
}
