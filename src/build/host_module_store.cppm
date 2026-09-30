// mcpp.build.host_module_store — where what a build program imports is kept once
// it is compiled (#748, B1).
//
// A build program that imports `mcpp`, or a host module of a rule package, needs
// that module's BMI and object before its own compile. Until this module, each
// program compiled them into its own directory, before its own compile and
// whether or not an identical copy sat in the directory next door: four members
// that import one host module compiled the `mcpp` module four times and the host
// module four times. #748 measured 7.8 s per program outside the program's own
// `ran`, repeated for every program and every invocation.
//
// AN ENTRY IS ADDRESSED BY ITS INPUTS, AND THE INPUTS ARE RECORDED. The key is a
// hash of everything that reaches the compile: the host compiler's identity, the
// standard flag, the flags the compile carries, the BMIs it imports, and the text
// it compiles. The same inputs are written to entry.json, and a hit compares
// them field by field (mcpp.bmi_cache::probe_cached), never the hash alone. The
// flags of one compile are the flags of every consumer of its BMI, so a BMI is
// shared only between compiles that agree with it (hostprogram.cppm, P6): the
// agreement is a consequence of the key and is not checked afterwards.
//
// WHERE AN ENTRY LIVES DEPENDS ON WHERE ITS TEXT CAME FROM, and on nothing else.
//
//   Global     the text comes from the engine (the bundled `mcpp` module) or from
//              an index package whose sources sit in the immutable store. The
//              same name and version are the same bytes, so one entry serves every
//              project of this machine. It lives in the global cache, in the
//              layout of a dependency's entry, so `mcpp cache gc` collects it.
//   Workspace  the text comes from a path or git dependency or from a workspace
//              member. Its sources can change without its name and version
//              changing, which is the rule the dependency cache applies
//              (plan.cpp, the admission of a package to the cache), so it is kept
//              under the workspace's `target/` and is never written to the global
//              cache.
//
// A host module is compiled ALONE, against `std` and `mcpp` only, so the local
// taint the dependency cache walks the closure for is, for a host module, its
// own package's alone.
//
// AN ENTRY IS PUBLISHED WHOLE. It is compiled into a staging directory and
// renamed into place with entry.json written last (mcpp.bmi_cache::
// publish_staged), so it is never seen half-written. Threads of one process that
// want one key take one lock, so the key is compiled once, and the others find
// it; two processes that race both compile, and the second to publish finds the
// first's entry in place and discards its own.

export module mcpp.build.host_module_store;

import std;
import mcpp.bmi_cache;
import mcpp.libs.json;
import mcpp.toolchain.fingerprint;   // hash_string — the one key hash of the build cache

export namespace mcpp::build::hostmods {

namespace fs = std::filesystem;

// ─── SHA-256 ─────────────────────────────────────────────────────────────
//
// The digest of the text an entry was compiled from. The build's other content
// identities are 64-bit FNV-1a, which answers "has this changed"; an entry that
// other projects and other users of the machine reuse is held to "is this the
// same text" and records a digest that answers that.
std::string sha256_hex(std::string_view bytes);
// Empty when the file cannot be read: an unreadable file has no identity, and a
// caller that keyed on the empty string would key every unreadable file alike.
std::string sha256_file(const fs::path& p);

// The digest of every regular file below `dir`: the relative name (UTF-8) and
// the content of each, in name order. A host module is compiled from one
// interface file, and the file may include what sits beside it; for a package
// whose sources can change in place, the interface's digest alone would not see
// an edit to an included file. `target`, `.git` and `.mcpp` are not entered.
//
// A tree of more than `kTreeFileLimit` files, or of more than `kTreeByteLimit`
// bytes, is not digested: `complete` is false, and the caller keys the entry on
// something that cannot match a later invocation, so the entry is reused only
// within this one.
inline constexpr std::size_t kTreeFileLimit = 4096;
inline constexpr std::uintmax_t kTreeByteLimit = 64ull * 1024 * 1024;
struct TreeDigest {
    std::string hex;
    bool        complete = true;
};
TreeDigest tree_digest(const fs::path& dir);

// A stand-in identity that matches within this process and nowhere else.
std::string process_nonce();

// ─── Where an entry lives ────────────────────────────────────────────────

struct Home {
    enum class Kind { Global, Workspace };
    Kind kind = Kind::Workspace;

    // Global: the cache root (mcpp::home::cache_root()) and the package address
    // below it, `pkg/<index>/<package>@<version>/<key>/`.
    fs::path    cacheRoot;
    std::string index;
    std::string package;
    std::string version;

    // Workspace: `<workspace>/target/.build-mcpp/host-modules`; an entry is
    // `<store>/<key>/`.
    fs::path workspaceStore;

    // What entry.json records about the BMI family, as for a dependency's entry.
    std::string bmiDirName = "gcm.cache";
    std::string manifestTag = "gcm";
};

// What an entry holds: file names below its `bmi/` and `obj/`.
struct Files {
    std::vector<std::string> bmi;
    std::vector<std::string> obj;
};

struct Entry {
    fs::path    dir;
    std::string key;            // 16 hex; what a dependent entry records as its import
    bool        reused = false; // found, and not compiled by this call
    bool        global = false;

    fs::path bmi(std::string_view name) const { return dir / "bmi" / std::string(name); }
    fs::path obj(std::string_view name) const { return dir / "obj" / std::string(name); }
};

// Writes `files` into `scratch/bmi` and `scratch/obj`. The scratch directory is
// the producer's own, and is gone when the call returns.
using Producer = std::function<std::expected<void, std::string>(const fs::path& scratch)>;

// The entry for `inputs` in `home`: found, or produced once and published.
//
// `inputs` is the complete description of what `produce` will compile. Two calls
// whose inputs are equal get one entry, and `produce` runs at most once for them
// in this process. `files` names what it writes.
std::expected<Entry, std::string>
obtain(const Home& home, const nlohmann::json& inputs, const Files& files,
       const Producer& produce);

// The key `obtain` derives for `inputs`.
std::string key_of(const nlohmann::json& inputs);

} // namespace mcpp::build::hostmods

namespace mcpp::build::hostmods {

// ─── SHA-256 (FIPS 180-4) ────────────────────────────────────────────────

namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
public:
    void update(const unsigned char* data, std::size_t len) {
        total_ += len;
        while (len > 0) {
            const std::size_t take = std::min<std::size_t>(len, 64 - fill_);
            std::memcpy(block_.data() + fill_, data, take);
            fill_ += take; data += take; len -= take;
            if (fill_ == 64) { compress(); fill_ = 0; }
        }
    }

    std::string finish() {
        const std::uint64_t bits = total_ * 8;
        const unsigned char one = 0x80;
        update(&one, 1);
        const unsigned char zero = 0;
        while (fill_ != 56) update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(len, 8);
        std::string out;
        out.reserve(64);
        for (auto w : h_)
            for (int i = 3; i >= 0; --i)
                out += std::format("{:02x}", static_cast<unsigned>((w >> (8 * i)) & 0xffu));
        return out;
    }

private:
    void compress() {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t{block_[4 * i]} << 24) | (std::uint32_t{block_[4 * i + 1]} << 16)
                 | (std::uint32_t{block_[4 * i + 2]} << 8) | std::uint32_t{block_[4 * i + 3]};
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = h_;
        for (int i = 0; i < 64; ++i) {
            const auto S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = h + S1 + ch + kSha256K[i] + w[i];
            const auto S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::array<std::uint32_t, 8> h_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<unsigned char, 64> block_{};
    std::size_t   fill_  = 0;
    std::uint64_t total_ = 0;
};

// The file name as UTF-8, never through the code page (the path narrowing rule:
// a name that is an identity is `u8string()`).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace

std::string sha256_hex(std::string_view bytes) {
    Sha256 h;
    h.update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    return h.finish();
}

std::string sha256_file(const fs::path& p) {
    std::ifstream is(p, std::ios::binary);
    if (!is) return {};
    Sha256 h;
    std::array<char, 65536> buf{};
    while (is.read(buf.data(), buf.size()) || is.gcount() > 0)
        h.update(reinterpret_cast<const unsigned char*>(buf.data()),
                 static_cast<std::size_t>(is.gcount()));
    return h.finish();
}

std::string process_nonce() {
    static const std::string nonce = [] {
        std::random_device rd;
        return std::format("{:08x}{:08x}", rd(), rd());
    }();
    return nonce;
}

TreeDigest tree_digest(const fs::path& dir) {
    TreeDigest out;
    std::vector<std::pair<std::string, std::string>> rows;   // (relative name, file digest)
    std::uintmax_t bytes = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) { out.complete = false; return out; }
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { out.complete = false; break; }
        const auto name = utf8(it->path().filename());
        std::error_code tec;
        if (it->is_directory(tec)) {
            if (name == "target" || name == ".git" || name == ".mcpp") it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(tec)) continue;
        if (rows.size() >= kTreeFileLimit) { out.complete = false; break; }
        if (const auto size = it->file_size(tec); !tec) bytes += size;
        if (bytes > kTreeByteLimit) { out.complete = false; break; }
        rows.emplace_back(utf8(it->path().lexically_relative(dir)), sha256_file(it->path()));
    }
    std::ranges::sort(rows);
    Sha256 h;
    for (auto const& [name, digest] : rows) {
        const auto line = std::format("{}\t{}\n", name, digest);
        h.update(reinterpret_cast<const unsigned char*>(line.data()), line.size());
    }
    out.hex = h.finish();
    return out;
}

// ─── The entries ─────────────────────────────────────────────────────────

std::string key_of(const nlohmann::json& inputs) {
    // `dump()` is canonical: an object's members are kept in key order.
    return mcpp::toolchain::hash_string("mcpp-host-module-store-v1\x1f" + inputs.dump());
}

namespace {

mcpp::bmi_cache::CacheKey cache_key_of(const Home& home, const nlohmann::json& inputs) {
    mcpp::bmi_cache::CacheKey ck;
    ck.keyHex      = key_of(inputs);
    ck.inputs      = inputs;
    ck.bmiDirName  = home.bmiDirName;
    ck.manifestTag = home.manifestTag;
    if (home.kind == Home::Kind::Global) {
        ck.cacheRoot   = home.cacheRoot;
        ck.indexName   = home.index;
        ck.packageName = home.package;
        ck.version     = home.version;
    } else {
        ck.cacheRoot   = home.workspaceStore;
        ck.directDir   = home.workspaceStore / ck.keyHex;
        ck.indexName   = "workspace";
        ck.packageName = "host-modules";
        ck.version     = "";
    }
    return ck;
}

mcpp::bmi_cache::DepArtifacts artifacts_of(const Files& files) {
    mcpp::bmi_cache::DepArtifacts a;
    a.bmiFiles = files.bmi;
    for (auto const& o : files.obj) a.objFiles.push_back({o, {}});
    return a;
}

// One lock per entry address, for the threads of this process. A lock is never
// removed: there are a handful of entries, and a thread still waiting on a
// removed lock would be waiting on a destroyed one.
std::mutex& lock_for(const fs::path& dir) {
    static std::mutex mapMutex;
    static std::map<std::string, std::unique_ptr<std::mutex>> locks;
    std::lock_guard guard(mapMutex);
    auto& slot = locks[utf8(dir)];
    if (!slot) slot = std::make_unique<std::mutex>();
    return *slot;
}

} // namespace

std::expected<Entry, std::string>
obtain(const Home& home, const nlohmann::json& inputs, const Files& files,
       const Producer& produce)
{
    const auto ck   = cache_key_of(home, inputs);
    const auto want = artifacts_of(files);
    Entry entry;
    entry.dir    = ck.dir();
    entry.key    = ck.keyHex;
    entry.global = home.kind == Home::Kind::Global;

    // One thread at a time per address: the thread that finds nothing compiles,
    // and the threads behind it find the entry.
    std::lock_guard guard(lock_for(entry.dir));

    if (mcpp::bmi_cache::probe_cached(ck, want).ok) {
        mcpp::bmi_cache::touch_accessed(ck);
        entry.reused = true;
        return entry;
    }

    auto staged = mcpp::bmi_cache::stage_entry(ck);
    if (!staged) return std::unexpected(staged.error());
    if (auto r = produce(*staged); !r) {
        std::error_code ec;
        fs::remove_all(*staged, ec);
        return std::unexpected(r.error());
    }
    auto placed = mcpp::bmi_cache::publish_staged(ck, *staged, want);
    if (!placed) return std::unexpected(placed.error());
    // Another process published first: its entry is the one in place.
    entry.reused = !*placed;
    return entry;
}

} // namespace mcpp::build::hostmods
