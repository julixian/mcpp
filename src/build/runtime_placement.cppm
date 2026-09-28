// mcpp.build.runtime_placement — for each name beside a Windows program, where
// its bytes come from (SPEC-006 §3.7, SPEC-007 R4.3).
//
// ONE AUTHORITY PER FACT. Until 2026.9.28.2 four places decided which file sat
// beside a PE program: the plan-time scan of the runtime search directories,
// the toolchain-coupled staging in `flags.cppm`, the post-link `place-dlls`
// edge and `mcpp pack`. The scan wrote every DLL of a search directory into the
// deploy list; the staging then found the MSVC runtime's names already there,
// took them for the project's declarations, kept them and warned. On a program
// that links Qt that placed the CRT a Qt recipe had copied into its `bin/` --
// older than the toolset that compiled the program -- ten times per link, with
// a message that attributed the files to "this project" (review 2026-09-28
// §2.1). This module is the one answer the four read.
//
// THE RULE.
//
//   * Candidates come in three kinds. `Declared`: a `[runtime] deploy_files`
//     entry or an R4.2 `deploy`. `Toolchain`: the selected toolset's
//     `Microsoft.VC*.CRT` files. `Derived`: a DLL found in a runtime search
//     directory. A declaration outranks the toolchain, which outranks a
//     derived observation (principle P2 of the design).
//   * The MSVC C++ runtime is ONE VERSIONED SET. Its files are never mixed
//     across versions: the set is chosen whole, as the toolset's unless a
//     derived directory holds a complete set that is strictly newer (D1), in
//     which case that set is placed and the choice is stated once.
//   * The contract governs the kind, not only the order. Under host-coupled no
//     CRT name is placed from any source, and a declared one is refused; under
//     self-contained the program imports no CRT, and a CRT name a dependency
//     brings is placed from the chosen set.
//   * A declared CRT file wins over the choice and is compared with the floor
//     (the toolset's own runtime version): older is a warning naming both
//     versions (D2, a warning until the floor's reading is shown reliable).
//   * A version that cannot be read decides nothing: the kind order applies,
//     and the fact is stated once.
//   * A dependency directory that ships the CRT is a packaging fault, stated
//     once and never a silent source (§2.9).
//
// MinGW's runtime (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
// `libwinpthread-1.dll`) takes the kind order and no version rule: those DLLs
// carry no reliable VERSIONINFO, and no toolchain candidate is staged for
// them, so on that row the rule reduces to "a declaration outranks a derived
// file", as before.
//
// PURE: `resolve` reads no file except through `Input::versionOf`, which a
// test replaces, so every combination of kinds, versions and contracts is a
// unit test (tests/unit/test_runtime_placement.cpp).

export module mcpp.build.runtime_placement;

import std;
import mcpp.pack.binfmt;

export namespace mcpp::build::runtime_placement {

using Version = mcpp::pack::binfmt::PeVersion;

// The files of a VC redistributable's `Microsoft.VC14x.CRT` directory (x64,
// 14.3x and 14.4x). A name here is a CRT name wherever it is found; the
// toolset's own listing may add to it.
inline constexpr std::string_view kMsvcCrtNames[] = {
    "concrt140.dll",       "msvcp140.dll",           "msvcp140_1.dll",
    "msvcp140_2.dll",      "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",     "vcruntime140.dll",       "vcruntime140_1.dll",
    "vcruntime140_threads.dll",
};

inline std::string fold(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline bool is_msvc_crt_name(std::string_view name) {
    const auto f = fold(name);
    return std::ranges::any_of(kMsvcCrtNames, [&](std::string_view n) { return n == f; });
}

enum class Kind { Declared, Toolchain, Derived };

inline std::string_view to_string(Kind k) {
    switch (k) {
        case Kind::Declared:  return "declared";
        case Kind::Toolchain: return "toolchain";
        case Kind::Derived:   return "derived";
    }
    return "derived";
}

// How the C++ runtime contract governs a CRT name (SPEC-006 §3.7).
enum class CrtPolicy {
    NotApplicable,  // not the MSVC ABI: no CRT set; every name takes the kind order
    Carry,          // toolchain-coupled: the chosen set is placed
    System,         // host-coupled: no CRT name is placed from any source
    Static,         // self-contained: placed from the chosen set only when a dependency brings a CRT name
};

struct Candidate {
    // Usually one; a declared destination may have several (SPEC-007 R4.2),
    // whose bytes `mcpp stage` compares at build time.
    std::vector<std::filesystem::path> sources;
    // Relative to the output directory: `bin/<file>`, or `bin/<sub>/<file>`
    // for a declared entry with a `to`.
    std::filesystem::path dest;
    Kind kind = Kind::Derived;
};

struct Input {
    // In precedence order within a kind; derived candidates in search order.
    std::vector<Candidate> candidates;
    CrtPolicy crt = CrtPolicy::NotApplicable;
    // The PE file version of a candidate's source; nullopt when it cannot be
    // read. Injected, so the rule is tested without files.
    std::function<std::optional<Version>(const std::filesystem::path&)> versionOf;
};

struct Placement {
    std::vector<std::filesystem::path> sources;
    std::filesystem::path dest;
    Kind kind = Kind::Derived;
};

struct Decision {
    std::vector<Placement> placed;       // declared first, then the CRT set, then derived
    std::vector<std::string> notes;      // stated once: a packaging fault, a newer set, an unreadable version
    std::vector<std::string> warnings;   // a declared CRT file older than the toolset's
    std::vector<std::string> errors;     // a declared CRT file under host-coupled
    // The CRT set placed, and its source kind, for `resolution.json`.
    std::optional<Version> crtVersion;
    std::optional<Kind> crtKind;
};

Decision resolve(const Input& in);

} // namespace mcpp::build::runtime_placement

// ── implementation ──────────────────────────────────────────────────────────

namespace mcpp::build::runtime_placement {

namespace {

// Directly beside the program: a CRT name elsewhere (a declared `to` into a
// subdirectory) is not the program's runtime and takes the ordinary kind order.
bool beside_program(const std::filesystem::path& dest) {
    return fold(dest.parent_path().generic_string()) == "bin";
}

struct CrtSet {
    std::filesystem::path dir;                          // for a derived set; empty for the toolset's
    std::vector<const Candidate*> members;
    std::optional<Version> version;                     // the oldest member's; nullopt when one is unreadable
    bool readable = true;
};

std::optional<Version> set_version(const CrtSet& s, const Input& in, bool& readable) {
    std::optional<Version> oldest;
    readable = true;
    for (auto const* c : s.members) {
        auto v = in.versionOf ? in.versionOf(c->sources.front()) : std::nullopt;
        if (!v) { readable = false; return std::nullopt; }
        if (!oldest || *v < *oldest) oldest = v;
    }
    return oldest;
}

std::string names_of(const CrtSet& s) {
    std::vector<std::string> names;
    for (auto const* c : s.members) names.push_back(c->dest.filename().string());
    std::ranges::sort(names);
    std::string out;
    for (auto const& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

} // namespace

Decision resolve(const Input& in) {
    Decision out;
    const bool crtRule = in.crt != CrtPolicy::NotApplicable;
    auto is_crt = [&](const Candidate& c) {
        return crtRule && beside_program(c.dest) && is_msvc_crt_name(c.dest.filename().string());
    };

    // ── the ordinary names: the highest kind wins ──────────────────────
    //
    // Within the derived kind two search directories offering one name stay
    // two sources of one destination, as before: `mcpp stage` compares their
    // bytes and refuses a difference (SPEC-007 R4.2). Only a higher kind
    // replaces a lower one outright.
    std::map<std::string, std::size_t> taken;   // folded destination -> index in placed
    for (auto kind : {Kind::Declared, Kind::Toolchain, Kind::Derived}) {
        for (auto const& c : in.candidates) {
            if (c.kind != kind || is_crt(c)) continue;
            auto key = fold(c.dest.generic_string());
            if (auto it = taken.find(key); it != taken.end()) {
                auto& p = out.placed[it->second];
                if (p.kind == Kind::Derived && c.kind == Kind::Derived)
                    for (auto const& s : c.sources)
                        if (std::ranges::find(p.sources, s) == p.sources.end())
                            p.sources.push_back(s);
                continue;
            }
            taken.emplace(std::move(key), out.placed.size());
            out.placed.push_back({c.sources, c.dest, c.kind});
        }
    }
    if (!crtRule) return out;

    // ── the CRT: one versioned set ─────────────────────────────────────
    std::vector<const Candidate*> declaredCrt;
    CrtSet toolset;
    std::vector<CrtSet> derivedSets;
    for (auto const& c : in.candidates) {
        if (!is_crt(c)) continue;
        if (c.kind == Kind::Declared) { declaredCrt.push_back(&c); continue; }
        if (c.kind == Kind::Toolchain) { toolset.members.push_back(&c); continue; }
        const auto dir = c.sources.front().parent_path();
        auto it = std::ranges::find_if(derivedSets, [&](auto const& s) { return s.dir == dir; });
        if (it == derivedSets.end()) {
            derivedSets.push_back(CrtSet{.dir = dir});
            it = std::prev(derivedSets.end());
        }
        // One file per name in a set: a second directory offering the name is
        // another set.
        auto name = fold(c.dest.filename().string());
        if (std::ranges::none_of(it->members, [&](auto const* m) {
                return fold(m->dest.filename().string()) == name; }))
            it->members.push_back(&c);
    }

    if (in.crt == CrtPolicy::System) {
        for (auto const* d : declaredCrt)
            out.errors.push_back(std::format(
                "'{}' is declared beside the program, and the C++ runtime contract is "
                "host-coupled: the system's C++ runtime serves this program, so no "
                "copy of it is placed. Remove the declaration, or state "
                "cxx_runtime = \"toolchain-coupled\".",
                d->dest.filename().string()));
        for (auto const& s : derivedSets)
            out.notes.push_back(std::format(
                "'{}' ships the MSVC C++ runtime ({}); under host-coupled the system's "
                "runtime serves the program, and these files are not placed. A library "
                "package does not carry the compiler's runtime.",
                s.dir.string(), names_of(s)));
        return out;
    }

    const bool needed = in.crt == CrtPolicy::Carry
        || !declaredCrt.empty() || !derivedSets.empty();
    if (!needed) return out;

    // Versions: read once, only where a comparison needs them.
    bool toolsetReadable = true;
    if (!toolset.members.empty()) toolset.version = set_version(toolset, in, toolsetReadable);
    toolset.readable = toolsetReadable;
    for (auto& s : derivedSets) {
        bool readable = true;
        s.version = set_version(s, in, readable);
        s.readable = readable;
    }

    // The choice: the toolset's set, unless a derived set is complete and
    // strictly newer. Without a toolset set (self-contained on a row with no
    // redistributable) the first derived set is the only candidate.
    const CrtSet* chosen = toolset.members.empty() ? nullptr : &toolset;
    auto complete = [&](const CrtSet& s) {
        return std::ranges::all_of(toolset.members, [&](auto const* t) {
            auto name = fold(t->dest.filename().string());
            return std::ranges::any_of(s.members, [&](auto const* m) {
                return fold(m->dest.filename().string()) == name; });
        });
    };
    bool unreadableStated = false;
    for (auto const& s : derivedSets) {
        if (!chosen) { chosen = &s; continue; }
        if (chosen != &toolset) {
            // Two derived sets and no toolset: the newer readable one.
            if (s.version && chosen->version && *s.version > *chosen->version) chosen = &s;
            continue;
        }
        if (!complete(s)) continue;
        if (!s.readable || !toolset.readable) {
            if (!unreadableStated) {
                out.notes.push_back(std::format(
                    "the version of the MSVC C++ runtime in '{}' or of the toolset's "
                    "could not be read; the toolset's runtime is placed",
                    s.dir.string()));
                unreadableStated = true;
            }
            continue;
        }
        if (s.version && toolset.version && *s.version > *toolset.version) {
            out.notes.push_back(std::format(
                "the program's C++ runtime comes from '{}' ({}), newer than the "
                "toolset's ({})",
                s.dir.string(), s.version->str(), toolset.version->str()));
            chosen = &s;
        }
    }
    // A derived set that is not placed is a packaging fault, said once.
    for (auto const& s : derivedSets) {
        if (&s == chosen) continue;
        out.notes.push_back(std::format(
            "'{}' ships the MSVC C++ runtime ({}{}); the {} runtime{} is placed "
            "instead. A library package does not carry the compiler's runtime.",
            s.dir.string(), names_of(s),
            s.version ? ", " + s.version->str() : std::string{},
            chosen == &toolset ? "toolset's" : "newer",
            chosen && chosen->version ? " (" + chosen->version->str() + ")" : std::string{}));
    }

    // Declared CRT files win over the choice, compared with the floor: the
    // toolset's runtime version, which the program's STL headers require.
    const auto floor = toolset.version;
    std::set<std::string> declaredNames;
    for (auto const* d : declaredCrt) {
        declaredNames.insert(fold(d->dest.filename().string()));
        out.placed.push_back({d->sources, d->dest, Kind::Declared});
        auto v = in.versionOf ? in.versionOf(d->sources.front()) : std::nullopt;
        if (!v) {
            out.notes.push_back(std::format(
                "the version of the declared '{}' could not be read; it is placed as "
                "declared", d->sources.front().string()));
        } else if (floor && *v < *floor) {
            out.warnings.push_back(std::format(
                "the declared '{}' is version {}, older than the toolset's C++ runtime "
                "({}) that this program is compiled against; a runtime older than the "
                "newest toolset that built one of the program's images can fail to "
                "load it",
                d->sources.front().string(), v->str(), floor->str()));
        }
    }
    if (chosen) {
        for (auto const* m : chosen->members) {
            if (declaredNames.contains(fold(m->dest.filename().string()))) continue;
            out.placed.push_back({m->sources, m->dest, m->kind});
        }
        out.crtVersion = chosen->version;
        out.crtKind = chosen->members.empty() ? std::nullopt
                                              : std::optional<Kind>(chosen->members.front()->kind);
    }
    return out;
}

} // namespace mcpp::build::runtime_placement
