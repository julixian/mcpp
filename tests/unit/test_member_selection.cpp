#include <gtest/gtest.h>

import std;
import mcpp.manifest;
import mcpp.cli.selection;

// Member selection design 2026-09-30, S1 to S3. `select_members` is the one
// function every command that acts on workspace members reads its `-p`,
// `--workspace` and `--exclude` through, so the set a command plans does not
// depend on which command it is, or on the order `-p` names members in. The
// e2e halves (what the selected members build and test) are
// tests/e2e/852_… to 856_….

namespace {

namespace fs = std::filesystem;

struct Workspace {
    fs::path root;

    explicit Workspace(std::string_view tag) {
        root = fs::temp_directory_path()
             / std::format("mcpp-select-{}-{:x}", tag, std::random_device{}());
        fs::create_directories(root);
    }
    ~Workspace() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Workspace(const Workspace&) = delete;

    void write(const fs::path& rel, std::string_view text) {
        auto p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << text;
    }
    void member(std::string_view dir, std::string_view name, std::string_view ns = "") {
        std::string toml = "[package]\n";
        if (!ns.empty()) toml += std::format("namespace = \"{}\"\n", ns);
        toml += std::format("name = \"{}\"\nversion = \"0.1.0\"\n", name);
        write(fs::path(dir) / "mcpp.toml", toml);
    }
    // A virtual workspace root (`[workspace]` alone) or a rooted one.
    mcpp::manifest::Manifest virtual_root(std::string_view members) {
        write("mcpp.toml", std::format("[workspace]\nmembers = [{}]\n", members));
        return load();
    }
    mcpp::manifest::Manifest rooted_root(std::string_view members) {
        write("mcpp.toml", std::format(
            "[package]\nname = \"rootpkg\"\nversion = \"0.1.0\"\n\n[workspace]\nmembers = [{}]\n",
            members));
        return load();
    }
    mcpp::manifest::Manifest load() {
        auto m = mcpp::manifest::load(root / "mcpp.toml");
        EXPECT_TRUE(m.has_value()) << (m ? "" : m.error().format());
        return m ? std::move(*m) : mcpp::manifest::Manifest{};
    }
};

using Members = std::vector<std::string>;

mcpp::cli::MemberRequest request(bool all, Members packages = {}, Members excludes = {}) {
    return {all, std::move(packages), std::move(excludes)};
}

// Three members in `[workspace] members` order: a, b, c.
struct Three : Workspace {
    Three() : Workspace("three") {
        member("a", "a");
        member("b", "b");
        member("c", "c");
    }
    mcpp::manifest::Manifest ws() { return virtual_root("\"a\", \"b\", \"c\""); }
};

} // namespace

// S1: a virtual root without `-p` is the whole workspace, `--workspace` is
// the whole workspace anywhere, and both are the same set.
TEST(MemberSelection, AVirtualRootWithoutDashPAndWorkspaceAreEveryMember) {
    Three f;
    auto ws = f.ws();
    auto implicit = mcpp::cli::select_members(ws, f.root, "", request(false));
    ASSERT_TRUE(implicit.has_value()) << implicit.error();
    EXPECT_EQ(implicit->members, (Members{"a", "b", "c"}));
    EXPECT_TRUE(implicit->whole);

    auto all = mcpp::cli::select_members(ws, f.root, "b", request(true));
    ASSERT_TRUE(all.has_value()) << all.error();
    EXPECT_EQ(all->members, (Members{"a", "b", "c"}));
    EXPECT_TRUE(all->whole);
}

// A rooted workspace's own package is "." and comes first. Without a selector
// it is the only member; `--workspace` adds the others.
TEST(MemberSelection, ARootedRootSelectsItsOwnPackageFirst) {
    Workspace f("rooted");
    f.member("a", "a");
    f.member("b", "b");
    auto ws = f.rooted_root("\"a\", \"b\"");

    auto bare = mcpp::cli::select_members(ws, f.root, "", request(false));
    ASSERT_TRUE(bare.has_value()) << bare.error();
    EXPECT_EQ(bare->members, (Members{"."}));
    EXPECT_FALSE(bare->whole);

    auto all = mcpp::cli::select_members(ws, f.root, "", request(true));
    ASSERT_TRUE(all.has_value()) << all.error();
    EXPECT_EQ(all->members, (Members{".", "a", "b"}));

    auto named = mcpp::cli::select_members(ws, f.root, "", request(false, {"b", "rootpkg"}));
    ASSERT_TRUE(named.has_value()) << named.error();
    EXPECT_EQ(named->members, (Members{".", "b"}));
}

// A command run inside a member's directory selects that member.
TEST(MemberSelection, InsideAMemberSelectsThatMember) {
    Three f;
    auto ws = f.ws();
    auto in = mcpp::cli::select_members(ws, f.root, "b", request(false));
    ASSERT_TRUE(in.has_value()) << in.error();
    EXPECT_EQ(in->members, (Members{"b"}));
    EXPECT_FALSE(in->whole);
}

// S1: `-p` names members, each resolved in the order docs/07 §5.3 states. The
// selection is a set kept in manifest order: the order `-p` was written in
// does not change it, and a member named twice, by two spellings, is one.
TEST(MemberSelection, SeveralDashPSelectTheirMembersOnceInManifestOrder) {
    Workspace f("set");
    f.member("libs/a", "alpha");
    f.member("libs/b", "beta");
    f.member("libs/c", "gamma");
    auto ws = f.virtual_root("\"libs/a\", \"libs/b\", \"libs/c\"");

    auto ab = mcpp::cli::select_members(ws, f.root, "", request(false, {"alpha", "beta"}));
    auto ba = mcpp::cli::select_members(ws, f.root, "", request(false, {"beta", "alpha"}));
    ASSERT_TRUE(ab.has_value()) << ab.error();
    ASSERT_TRUE(ba.has_value()) << ba.error();
    EXPECT_EQ(ab->members, (Members{"libs/a", "libs/b"}));
    EXPECT_EQ(ab->members, ba->members);
    EXPECT_FALSE(ab->whole);

    // The package name, the member path and the directory's last segment are
    // three spellings of one member.
    auto same = mcpp::cli::select_members(ws, f.root, "",
                                          request(false, {"alpha", "libs/a", "a", "./libs/a"}));
    ASSERT_TRUE(same.has_value()) << same.error();
    EXPECT_EQ(same->members, (Members{"libs/a"}));
}

// A `-p` that names no member is refused, and the refusal names it and lists
// the members; one that several members match is refused naming every match.
TEST(MemberSelection, AnUnknownOrAmbiguousDashPIsRefusedByName) {
    Workspace f("refuse");
    f.member("a", "same", "ns1");
    f.member("b", "same", "ns2");
    f.member("c", "other");
    auto ws = f.virtual_root("\"a\", \"b\", \"c\"");

    auto unknown = mcpp::cli::select_members(ws, f.root, "", request(false, {"c", "nosuch"}));
    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().find("nosuch"), std::string::npos) << unknown.error();
    EXPECT_NE(unknown.error().find("'c'"), std::string::npos) << unknown.error();

    auto ambiguous = mcpp::cli::select_members(ws, f.root, "", request(false, {"same"}));
    ASSERT_FALSE(ambiguous.has_value());
    EXPECT_NE(ambiguous.error().find("ns1.same"), std::string::npos) << ambiguous.error();
    EXPECT_NE(ambiguous.error().find("ns2.same"), std::string::npos) << ambiguous.error();

    auto qualified = mcpp::cli::select_members(ws, f.root, "",
                                               request(false, {"ns2.same", "ns1.same"}));
    ASSERT_TRUE(qualified.has_value()) << qualified.error();
    EXPECT_EQ(qualified->members, (Members{"a", "b"}));
}

// S3: `--exclude` removes members from either "all" form, by the same
// resolution as `-p`, and keeps manifest order.
TEST(MemberSelection, ExcludeRemovesMembersFromAnAllForm) {
    Three f;
    auto ws = f.ws();
    auto flag = mcpp::cli::select_members(ws, f.root, "", request(true, {}, {"c"}));
    ASSERT_TRUE(flag.has_value()) << flag.error();
    EXPECT_EQ(flag->members, (Members{"a", "b"}));
    EXPECT_TRUE(flag->whole);

    // The implicit whole selection of a virtual root, and two spellings.
    auto implicit = mcpp::cli::select_members(ws, f.root, "", request(false, {}, {"a", "./c"}));
    ASSERT_TRUE(implicit.has_value()) << implicit.error();
    EXPECT_EQ(implicit->members, (Members{"b"}));

    Workspace r("rooted-exclude");
    r.member("a", "a");
    auto rws = r.rooted_root("\"a\"");
    auto root = mcpp::cli::select_members(rws, r.root, "", request(true, {}, {"rootpkg"}));
    ASSERT_TRUE(root.has_value()) << root.error();
    EXPECT_EQ(root->members, (Members{"a"}));
}

// S1, refusals: before anything is planned, `--exclude` with `-p`, `--exclude`
// that names no member, `--exclude` that leaves nothing, and `--exclude`
// where no "all" form applies.
TEST(MemberSelection, ExcludeIsRefusedWhereItHasNoMeaning) {
    Three f;
    auto ws = f.ws();

    auto withP = mcpp::cli::select_members(ws, f.root, "", request(false, {"a"}, {"b"}));
    ASSERT_FALSE(withP.has_value());
    EXPECT_NE(withP.error().find("--exclude"), std::string::npos) << withP.error();
    EXPECT_NE(withP.error().find("-p"), std::string::npos) << withP.error();

    auto withPAndAll = mcpp::cli::select_members(ws, f.root, "", request(true, {"a"}, {"b"}));
    EXPECT_FALSE(withPAndAll.has_value());

    auto unknown = mcpp::cli::select_members(ws, f.root, "", request(true, {}, {"nosuch"}));
    ASSERT_FALSE(unknown.has_value());
    EXPECT_NE(unknown.error().find("nosuch"), std::string::npos) << unknown.error();

    auto everything = mcpp::cli::select_members(ws, f.root, "", request(true, {}, {"a", "b", "c"}));
    ASSERT_FALSE(everything.has_value());
    EXPECT_NE(everything.error().find("every member"), std::string::npos) << everything.error();

    // Inside a member, and at a rooted root, without `--workspace`: nothing is
    // a whole-workspace selection to remove from.
    auto inside = mcpp::cli::select_members(ws, f.root, "a", request(false, {}, {"b"}));
    ASSERT_FALSE(inside.has_value());
    EXPECT_NE(inside.error().find("--workspace"), std::string::npos) << inside.error();

    Workspace r("rooted-refuse");
    r.member("a", "a");
    auto rws = r.rooted_root("\"a\"");
    EXPECT_FALSE(mcpp::cli::select_members(rws, r.root, "", request(false, {}, {"a"})).has_value());
}

// `--workspace` and `-p` state two selections; neither is taken over the other.
TEST(MemberSelection, WorkspaceTogetherWithDashPIsRefused) {
    Three f;
    auto ws = f.ws();
    auto both = mcpp::cli::select_members(ws, f.root, "", request(true, {"a"}, {}));
    ASSERT_FALSE(both.has_value());
    EXPECT_NE(both.error().find("--workspace"), std::string::npos) << both.error();
    EXPECT_NE(both.error().find("-p"), std::string::npos) << both.error();
}

// The overload that takes a directory finds the workspace the directory
// belongs to: from a member's directory, `-p` and `--workspace` still select
// among the members of the workspace that lists it. Outside a workspace there
// is no selection, and `--exclude` has nothing to name.
TEST(MemberSelection, TheDirectoryDecidesTheWorkspaceAndTheMemberItIsIn) {
    Three f;
    f.ws();

    auto at_root = mcpp::cli::select_members(request(false), f.root);
    ASSERT_TRUE(at_root.has_value()) << at_root.error();
    ASSERT_TRUE(at_root->has_value());
    EXPECT_EQ((*at_root)->members, (Members{"a", "b", "c"}));

    auto in_member = mcpp::cli::select_members(request(false), f.root / "b");
    ASSERT_TRUE(in_member.has_value()) << in_member.error();
    ASSERT_TRUE(in_member->has_value());
    EXPECT_EQ((*in_member)->members, (Members{"b"}));
    EXPECT_EQ(std::filesystem::weakly_canonical((*in_member)->root),
              std::filesystem::weakly_canonical(f.root));

    auto several_from_member = mcpp::cli::select_members(request(false, {"a", "c"}), f.root / "b");
    ASSERT_TRUE(several_from_member.has_value()) << several_from_member.error();
    ASSERT_TRUE(several_from_member->has_value());
    EXPECT_EQ((*several_from_member)->members, (Members{"a", "c"}));

    auto all_from_member = mcpp::cli::select_members(request(true, {}, {"a"}), f.root / "b");
    ASSERT_TRUE(all_from_member.has_value()) << all_from_member.error();
    ASSERT_TRUE(all_from_member->has_value());
    EXPECT_EQ((*all_from_member)->members, (Members{"b", "c"}));

    Workspace alone("alone");
    alone.member(".", "solo");
    auto none = mcpp::cli::select_members(request(false), alone.root);
    ASSERT_TRUE(none.has_value()) << none.error();
    EXPECT_FALSE(none->has_value());
    auto excluded = mcpp::cli::select_members(request(false, {}, {"x"}), alone.root);
    ASSERT_FALSE(excluded.has_value());
    EXPECT_NE(excluded.error().find("not in one"), std::string::npos) << excluded.error();
}
