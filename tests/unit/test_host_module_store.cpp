#include <gtest/gtest.h>

import std;
import mcpp.bmi_cache;
import mcpp.build.host_module_store;
import mcpp.libs.json;

// The store a build program's compiled imports live in (#748, B1): what an entry
// is addressed by, where it lives by provenance, what a hit compares, and that a
// key is compiled once however many threads ask for it.

namespace hm = mcpp::build::hostmods;
namespace fs = std::filesystem;

namespace {

struct Tmp {
    fs::path path;
    Tmp() {
        path = fs::temp_directory_path()
             / std::format("mcpp_host_module_store_test_{}", std::random_device{}());
        fs::create_directories(path);
    }
    ~Tmp() { std::error_code ec; fs::remove_all(path, ec); }
};

nlohmann::json inputs(std::string_view stdFlag = "-std=c++23", std::string_view digest = "abc") {
    nlohmann::json j;
    j["epoch"]            = 1;
    j["role"]             = "host-module";
    j["module"]           = "acme.rules";
    j["std_flag"]         = std::string(stdFlag);
    j["interface_sha256"] = std::string(digest);
    j["toolchain"]        = {{"compiler", "gcc"}, {"compiler_version", "16.1.0"}};
    return j;
}

hm::Files files() { return {{"acme.rules.gcm"}, {"acme.rules.o"}}; }

// What a producer writes: the two files an entry is listed with.
hm::Producer writing(std::atomic<int>& calls, std::string_view body = "x") {
    return [&calls, body = std::string(body)](const fs::path& scratch) -> std::expected<void, std::string> {
        ++calls;
        std::ofstream(scratch / "bmi" / "acme.rules.gcm") << body;
        std::ofstream(scratch / "obj" / "acme.rules.o")   << body;
        return {};
    };
}

hm::Home workspaceHome(const fs::path& root) {
    hm::Home h;
    h.kind = hm::Home::Kind::Workspace;
    h.workspaceStore = root / "target" / ".build-mcpp" / "host-modules";
    return h;
}

hm::Home globalHome(const fs::path& root) {
    hm::Home h;
    h.kind = hm::Home::Kind::Global;
    h.cacheRoot = root / "cache";
    h.index = "mcpplibs";
    h.package = "acme.rules";
    h.version = "1.2.3";
    return h;
}

} // namespace

// Known answers for SHA-256 (FIPS 180-2, appendix B and the empty message).
TEST(HostModuleStoreSha256, KnownAnswers) {
    EXPECT_EQ(hm::sha256_hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hm::sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hm::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A message that spans several blocks.
    EXPECT_EQ(hm::sha256_hex(std::string(1000, 'a')),
              "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST(HostModuleStoreSha256, AFileHasTheDigestOfItsBytes) {
    Tmp t;
    const auto p = t.path / "f.cppm";
    std::ofstream(p, std::ios::binary) << "abc";
    EXPECT_EQ(hm::sha256_file(p), hm::sha256_hex("abc"));
    EXPECT_EQ(hm::sha256_file(t.path / "absent"), "");
}

TEST(HostModuleStoreTree, TheDigestFollowsNamesAndContentsAndSkipsTarget) {
    Tmp t;
    fs::create_directories(t.path / "src");
    std::ofstream(t.path / "src" / "a.cppm") << "a";
    std::ofstream(t.path / "src" / "a.h") << "h";
    const auto first = hm::tree_digest(t.path);
    EXPECT_TRUE(first.complete);
    EXPECT_EQ(first.hex, hm::tree_digest(t.path).hex);

    // An included header edited: the digest moves although the interface did not.
    std::ofstream(t.path / "src" / "a.h") << "changed";
    const auto edited = hm::tree_digest(t.path);
    EXPECT_NE(first.hex, edited.hex);

    // What the build wrote below `target` is not the package's text.
    fs::create_directories(t.path / "target" / "x");
    std::ofstream(t.path / "target" / "x" / "out.o") << "bytes";
    EXPECT_EQ(edited.hex, hm::tree_digest(t.path).hex);

    // A file renamed, its bytes kept: another tree.
    fs::rename(t.path / "src" / "a.h", t.path / "src" / "b.h");
    EXPECT_NE(edited.hex, hm::tree_digest(t.path).hex);
}

TEST(HostModuleStore, TheKeyIsAFunctionOfTheInputs) {
    EXPECT_EQ(hm::key_of(inputs()), hm::key_of(inputs()));
    EXPECT_NE(hm::key_of(inputs("-std=c++23")), hm::key_of(inputs("-std=c++20")));
    EXPECT_NE(hm::key_of(inputs("-std=c++23", "abc")), hm::key_of(inputs("-std=c++23", "abd")));
    EXPECT_EQ(hm::key_of(inputs()).size(), 16u);
}

TEST(HostModuleStore, AnEntryOfAWorkspaceLivesInTheWorkspacesStore) {
    Tmp t;
    std::atomic<int> calls{0};
    auto e = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(e) << e.error();
    EXPECT_FALSE(e->reused);
    EXPECT_FALSE(e->global);
    EXPECT_EQ(e->dir, t.path / "target" / ".build-mcpp" / "host-modules" / hm::key_of(inputs()));
    EXPECT_TRUE(fs::exists(e->bmi("acme.rules.gcm")));
    EXPECT_TRUE(fs::exists(e->obj("acme.rules.o")));
    EXPECT_TRUE(fs::exists(e->dir / "entry.json"));
    // Nothing is left beside it: the staging directory went with the rename.
    const auto staging = t.path / "target" / ".build-mcpp" / "host-modules" / ".tmp";
    EXPECT_TRUE(!fs::exists(staging) || fs::is_empty(staging));
}

TEST(HostModuleStore, AnEntryOfTheEngineOrTheIndexLivesBelowThePackageAddress) {
    Tmp t;
    std::atomic<int> calls{0};
    auto e = hm::obtain(globalHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(e) << e.error();
    EXPECT_TRUE(e->global);
    // The layout of a dependency's entry, which is what lets `mcpp cache gc`,
    // `list` and `verify` walk it.
    EXPECT_EQ(e->dir, t.path / "cache" / "pkg" / "mcpplibs" / "acme.rules@1.2.3" / hm::key_of(inputs()));
    // Nothing of a workspace entry is written to the cache root, and the other
    // way round.
    Tmp u;
    auto w = hm::obtain(workspaceHome(u.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(w) << w.error();
    EXPECT_FALSE(fs::exists(u.path / "cache"));
}

TEST(HostModuleStore, TheEntryRecordsItsInputsAndItsFiles) {
    Tmp t;
    std::atomic<int> calls{0};
    auto e = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(e) << e.error();
    std::ifstream is(e->dir / "entry.json");
    nlohmann::json j;
    is >> j;
    EXPECT_EQ(j["key"], hm::key_of(inputs()));
    EXPECT_EQ(j["inputs"], inputs());
    EXPECT_EQ(j["bmi"], nlohmann::json::array({"acme.rules.gcm"}));
    EXPECT_EQ(j["obj"], nlohmann::json::array({"acme.rules.o"}));
}

TEST(HostModuleStore, AnEntryFoundIsNotProducedAgain) {
    Tmp t;
    std::atomic<int> calls{0};
    auto first = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(first) << first.error();
    auto second = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(second) << second.error();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_FALSE(first->reused);
    EXPECT_TRUE(second->reused);
    EXPECT_EQ(first->dir, second->dir);
}

TEST(HostModuleStore, DifferentInputsAreDifferentEntries) {
    Tmp t;
    std::atomic<int> calls{0};
    auto a = hm::obtain(workspaceHome(t.path), inputs("-std=c++23"), files(), writing(calls));
    auto b = hm::obtain(workspaceHome(t.path), inputs("-std=c++20"), files(), writing(calls));
    ASSERT_TRUE(a) << a.error();
    ASSERT_TRUE(b) << b.error();
    EXPECT_EQ(calls.load(), 2);
    EXPECT_NE(a->dir, b->dir);
}

// A hit compares the recorded inputs, not the address: an entry whose recorded
// inputs disagree is a miss, and is replaced.
TEST(HostModuleStore, AnEntryWhoseRecordedInputsDisagreeIsReplaced) {
    Tmp t;
    std::atomic<int> calls{0};
    auto first = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls, "old"));
    ASSERT_TRUE(first) << first.error();

    nlohmann::json j;
    { std::ifstream is(first->dir / "entry.json"); is >> j; }
    j["inputs"]["std_flag"] = "-std=c++98";
    { std::ofstream os(first->dir / "entry.json"); os << j.dump(2); }

    auto again = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls, "new"));
    ASSERT_TRUE(again) << again.error();
    EXPECT_EQ(calls.load(), 2);
    EXPECT_FALSE(again->reused);
    std::ifstream is(again->bmi("acme.rules.gcm"));
    std::string body;
    std::getline(is, body);
    EXPECT_EQ(body, "new");
    std::ifstream es(again->dir / "entry.json");
    nlohmann::json after;
    es >> after;
    EXPECT_EQ(after["inputs"]["std_flag"], "-std=c++23");
}

// An entry missing a file it lists is a miss too: nothing half-there is served.
TEST(HostModuleStore, AnEntryMissingAFileIsAMiss) {
    Tmp t;
    std::atomic<int> calls{0};
    auto first = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(first) << first.error();
    fs::remove(first->obj("acme.rules.o"));
    auto again = hm::obtain(workspaceHome(t.path), inputs(), files(), writing(calls));
    ASSERT_TRUE(again) << again.error();
    EXPECT_EQ(calls.load(), 2);
    EXPECT_TRUE(fs::exists(again->obj("acme.rules.o")));
}

TEST(HostModuleStore, AProducerThatFailsLeavesNoEntryAndNoStaging) {
    Tmp t;
    auto e = hm::obtain(workspaceHome(t.path), inputs(), files(),
        [](const fs::path&) -> std::expected<void, std::string> {
            return std::unexpected(std::string("the compiler said no"));
        });
    ASSERT_FALSE(e);
    EXPECT_EQ(e.error(), "the compiler said no");
    const auto store = t.path / "target" / ".build-mcpp" / "host-modules";
    EXPECT_FALSE(fs::exists(store / hm::key_of(inputs())));
    const auto staging = store / ".tmp";
    EXPECT_TRUE(!fs::exists(staging) || fs::is_empty(staging));
}

TEST(HostModuleStore, AProducerThatWritesLessThanItListedIsRefused) {
    Tmp t;
    auto e = hm::obtain(workspaceHome(t.path), inputs(), files(),
        [](const fs::path& scratch) -> std::expected<void, std::string> {
            std::ofstream(scratch / "bmi" / "acme.rules.gcm") << "x";   // no object
            return {};
        });
    ASSERT_FALSE(e);
    EXPECT_NE(e.error().find("acme.rules.o"), std::string::npos) << e.error();
    EXPECT_FALSE(fs::exists(t.path / "target" / ".build-mcpp" / "host-modules" / hm::key_of(inputs())));
}

// B2: the programs of a workspace are compiled at the same time, and a host
// module they share is compiled once. Many threads ask for one key; one of them
// produces it, and every thread is handed the same entry.
TEST(HostModuleStore, ManyThreadsAskingForOneKeyProduceItOnce) {
    Tmp t;
    std::atomic<int> calls{0};
    constexpr int kThreads = 16;
    std::vector<std::expected<hm::Entry, std::string>> results(kThreads, std::unexpected(std::string("unset")));
    std::atomic<bool> gate{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i)
        threads.emplace_back([&, i] {
            while (!gate.load()) std::this_thread::yield();
            results[i] = hm::obtain(workspaceHome(t.path), inputs(), files(),
                [&](const fs::path& scratch) -> std::expected<void, std::string> {
                    ++calls;
                    // Long enough for the other threads to arrive at the lock.
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    std::ofstream(scratch / "bmi" / "acme.rules.gcm") << "x";
                    std::ofstream(scratch / "obj" / "acme.rules.o")   << "x";
                    return {};
                });
        });
    gate.store(true);
    for (auto& th : threads) th.join();

    EXPECT_EQ(calls.load(), 1);
    int produced = 0;
    for (auto& r : results) {
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->dir, results[0]->dir);
        if (!r->reused) ++produced;
    }
    EXPECT_EQ(produced, 1);
}

TEST(HostModuleStore, ThreadsAskingForDifferentKeysProduceTheirOwn) {
    Tmp t;
    std::atomic<int> calls{0};
    constexpr int kThreads = 6;
    std::vector<std::thread> threads;
    std::vector<std::expected<hm::Entry, std::string>> results(kThreads, std::unexpected(std::string("unset")));
    for (int i = 0; i < kThreads; ++i)
        threads.emplace_back([&, i] {
            results[i] = hm::obtain(workspaceHome(t.path), inputs("-std=c++23", std::format("d{}", i)),
                                    files(), writing(calls));
        });
    for (auto& th : threads) th.join();
    EXPECT_EQ(calls.load(), kThreads);
    std::set<fs::path> dirs;
    for (auto& r : results) { ASSERT_TRUE(r) << r.error(); dirs.insert(r->dir); }
    EXPECT_EQ(dirs.size(), static_cast<std::size_t>(kThreads));
}

// A second producer that finishes after the first one published finds the entry
// in place and discards its own: the staged directory is removed.
TEST(HostModuleStore, APublishThatFindsTheEntryInPlaceKeepsTheFirst) {
    Tmp t;
    mcpp::bmi_cache::CacheKey key;
    key.cacheRoot   = t.path / "store";
    key.directDir   = t.path / "store" / "abcd";
    key.keyHex      = "abcd";
    key.indexName   = "workspace";
    key.packageName = "host-modules";
    key.inputs      = inputs();
    mcpp::bmi_cache::DepArtifacts arts;
    arts.bmiFiles = {"m.gcm"};
    arts.objFiles = {{"m.o", {}}};

    auto stage = [&](std::string_view body) {
        auto s = mcpp::bmi_cache::stage_entry(key);
        EXPECT_TRUE(s);
        std::ofstream(*s / "bmi" / "m.gcm") << body;
        std::ofstream(*s / "obj" / "m.o") << body;
        return *s;
    };
    const auto a = stage("first");
    const auto b = stage("second");
    EXPECT_NE(a, b);
    auto pa = mcpp::bmi_cache::publish_staged(key, a, arts);
    ASSERT_TRUE(pa);
    EXPECT_TRUE(*pa);
    auto pb = mcpp::bmi_cache::publish_staged(key, b, arts);
    ASSERT_TRUE(pb);
    EXPECT_FALSE(*pb);
    EXPECT_FALSE(fs::exists(b));
    std::ifstream is(key.directDir / "bmi" / "m.gcm");
    std::string body;
    std::getline(is, body);
    EXPECT_EQ(body, "first");
}
