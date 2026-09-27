// mcpp.build.prepare — BuildContext + prepare_build: the build-orchestration
// core (workspace -> toolchain -> dependency resolution -> features ->
// modgraph -> fingerprint -> plan -> lockfile).
//
// LAYOUT. This file is the primary interface: exported types, and
// prepare_build's own exported declaration (default arguments included —
// they belong on the declaration, not the definition). Nothing else. The
// implementation lives under src/build/prepare/:
//   state.cppm       implementation partition `:state` — PrepareState (the
//                     working state every phase reads and writes, by
//                     reference, in place of prepare_build's former ~180
//                     locals) and the phase functions' declarations.
//   driver.cpp        prepare_build's definition: construct PrepareState,
//                     call each phase in order, return the last one's result.
//   manifest.cpp      P0 — manifest and workspace resolution.
//   toolchain.cpp     P1, P2, P5 — toolchain spec, the toolchain resolver
//                     closure (P2 defines it, P5 calls it), the toolchain
//                     decision once the graph exists.
//   xlings.cpp        P3 — xlings payload before the graph.
//   graph_load.cpp    P4a — the git/path/version dependency loader
//                     (loadVersionDep), called from graph.cpp.
//   graph.cpp         P4b — the worklist engine and cycle detection that
//                     call it.
//   features.cpp      P6-P8 — feature activation, capability/ABI
//                     accumulation, target side, host-tool provisioning.
//   target_side.cpp   P9 — the target side, resolved once against the graph.
//   scan.cpp          P11 — the modgraph scan, validation, fingerprint.
//   plan.cpp          P13 — BuildContext: plan, assembly, Windows resources,
//                     the global cache, mcpp.lock, resolution.json.
//
// A GCC 16.1 CONSTRAINT SHAPES ALL OF THIS. Verified locally (archived on
// branch wip/prepare-split; see the mcpp repository's `upstream`-labelled GCC
// issue for the minimal reproduction): inserting a new INTERFACE unit into
// mcpp.build.prepare's import chain — a separately named module, or an
// interface partition (`export module mcpp.build.prepare:x;`) — makes GCC
// 16.1 segfault in add_imported_namespace while reading `import mcpp.cli;`
// in src/main.cpp, regardless of that unit's content. Implementation units,
// and an implementation partition imported only by implementation units, do
// not trigger it. Therefore: this file imports no partition and defines
// nothing beyond the declarations above; `:state` is an implementation
// partition, never an interface partition, and only driver.cpp and the
// phaseN files import it. Anyone adding a new interface partition or a new
// named module to this chain should build with GCC 16.1 first — the failure
// is immediate and unambiguous.
//
// Bodies moved verbatim from the CLI layer, then from one file into many.
// Zero behavior change either time.

module;
#include <cstdio>
#include <cstdlib>

export module mcpp.build.prepare;

export import mcpp.build.prepare_inputs;


import std;
import mcpp.targetside;
import mcpp.diag;
import mcpp.build.refusal;
import mcpp.xlings.address_set;
import mcpp.build.version_floor;
import mcpp.home;
import mcpp.platform.axis;
import mcpp.libs.json;
import mcpp.log;
import mcpp.manifest;
import mcpp.source_kind;
import mcpp.modgraph.glob;
import mcpp.modgraph.graph;
import mcpp.modgraph.scanner;
import mcpp.modgraph.validate;
import mcpp.toolchain.clang;
import mcpp.toolchain.hostflags;   // the compile-token producer the package std module reuses
import mcpp.toolchain.cenv;        // [c-abi] declaration → compiler configuration (design 2026-09-18)
import mcpp.toolchain.cenv_probe;  // [c-abi] declaration is checked, not trusted (design §3.2)
import mcpp.toolchain.predefines;  // the macros this engine defines: contract and emission in one module
import mcpp.toolchain.cppfly;
import mcpp.toolchain.detect;
import mcpp.toolchain.dialect;
import mcpp.toolchain.fingerprint;
import mcpp.toolchain.msvc;
import mcpp.toolchain.registry;
import mcpp.toolchain.linkmodel;
import mcpp.toolchain.gcc;
// For `resolve_version_match` / `list_installed_versions`: a bare compiler
// family named by the dependency graph resolves to a concrete version through
// exactly the path `mcpp toolchain default <family>` uses.
import mcpp.toolchain.lifecycle;
import mcpp.toolchain.stdmod;
import mcpp.freestanding.target;   // the target sysroot layout (libdir)
import mcpp.freestanding.linkline; // the ISA profile, for the std module command
import mcpp.toolchain.post_install;
import mcpp.toolchain.abi;
import mcpp.toolchain.triple;
import mcpp.build.linkage_form;   // #519 — which form each dependency takes
import mcpp.build.plan;
import mcpp.build.schedule.policy;
import mcpp.build.flags;          // compute_flags — the per-role contracts (#418)
import mcpp.build.distribution;   // dist::Role / dist::Contract to_string
import mcpp.platform.capacity;   // the host fallback handed to schedule::decide
import mcpp.build.graph_shape;  // #407: the graph says which mode wrote it
import mcpp.build.runtime_validation;  // declared artifact -> identity verdict
import mcpp.build.cache_key;
import mcpp.pack.abi_tag;      // the tag a prebuilt dependency is checked against
import mcpp.pack.prebuilt;     // …and the check itself
import mcpp.pack.stage_tree;   // where `${mcpp.stage_dir}` points, and its manifest
import mcpp.build.build_program;
import mcpp.build.directives;   // directive table: mark / fold_private_tail
import mcpp.build.tool_store;   // #355 host tools: store layout + key + overrides
import mcpp.build.dep_graph;    // queries over the resolved edge graph
import mcpp.build.provisions;   // #359 build-time provisions: table + propagation
import mcpp.build.resources;    // #365 Windows resources: synthesise / scan / find rc
import mcpp.build.backend;      // BuildOptions for the tool sub-build
import mcpp.build.ninja;        // make_ninja_backend — driving that sub-build
import mcpp.lockfile;
import mcpp.config;
import mcpp.xlings;
import mcpp.xlings.subos_info;
import mcpp.xlings.runtime_selection;
import mcpp.runtime.binding;
import mcpp.platform.runtime_search;
import mcpp.toolchain.post_install;
import mcpp.platform;
import mcpp.platform.macos;
import mcpp.build.runner_lookup;
import mcpp.fetcher;
import mcpp.fetcher.progress;
import mcpp.pm.resolver;
import mcpp.pm.index_spec;
import mcpp.pm.index_contract;
import mcpp.pm.index_route;
import mcpp.pm.index_refresh;
import mcpp.pm.mangle;
import mcpp.pm.compat;
import mcpp.pm.dep_spec;
import mcpp.pm.dependency_selector;
import mcpp.pm.lock_io;
import mcpp.version_req;
import mcpp.ui;
import mcpp.log;
import mcpp.wire;               // Severity, for PlanNote (#699 item 2, E3)
import mcpp.fallback.install_integrity;
import mcpp.bmi_cache;
import mcpp.project;

namespace mcpp::build {

// mcpp#237: surface xpkg-descriptor mcpp-segment keys this mcpp did not
// recognise. The parser collects them into `xpkgUnknownKeys` and skips the
// value; without this a typo like `dependencies = {...}` (correct key: `deps`)
// dropped the dependency with no diagnostic. Called at the descriptor-adoption
// sites (a fetched dep with no mcpp.toml, synthesized from the index `mcpp={}`
// block) — the single place the descriptor becomes a build input. Warning (not
// hard error) keeps forward-compat: an older mcpp building a newer descriptor
// should not fail outright, only tell the user what it ignored.
inline void warn_unknown_xpkg_keys(const mcpp::manifest::Manifest& dm,
                                   std::string_view depLabel) {
    // A LAYER NAME THIS ENGINE DOES NOT KNOW IS A VERSION GAP, NOT A TYPO,
    // WHEN IT ARRIVES FROM A DEPENDENCY.
    //
    // The reserved `mcpp:` prefix is a closed set so a misspelling cannot
    // silently disable a behaviour. Refusing a DEPENDENCY's manifest for it made
    // the set closed in a second sense nobody intended: a published package
    // could never declare a layer named after the reader was released.
    // Ignoring the layer and saying so is what this engine already does for
    // every other unknown key, and it is the only response that lets the
    // vocabulary grow.
    for (auto const& cap : dm.unknownCapabilities) {
        auto why = mcpp::targetside::parse_capability(cap);
        mcpp::ui::warning(std::format(
            "dependency '{}': {}\n"
            "       Ignored, and this build proceeds without that layer. "
            "A newer mcpp may resolve it.",
            depLabel,
            why ? std::format("`{}` names no capability mcpp knows.", cap)
                : why.error()));
    }
    for (auto const& key : dm.xpkgUnknownKeys) {
        auto suggestion = mcpp::manifest::closest_known_xpkg_key(key);
        if (suggestion.empty())
            mcpp::ui::warning(std::format(
                "dependency '{}': unknown mcpp-segment key '{}' in its xpkg "
                "descriptor — ignored (schema mismatch or typo)", depLabel, key));
        else
            mcpp::ui::warning(std::format(
                "dependency '{}': unknown mcpp-segment key '{}' in its xpkg "
                "descriptor — ignored; did you mean '{}'?", depLabel, key, suggestion));
    }
}

// `stale`, when given, turns the function into a comparison: nothing is
// created or written, and every declared file that is missing or differs from
// its declared content is appended. A build that describes itself rather than
// running (BuildOverrides::plan_only) reads the root package's generated files
// this way, because they live in the source tree it promises not to write.
std::expected<void, std::string>
materialize_generated_files(const std::filesystem::path& root,
                            const mcpp::manifest::Manifest& manifest,
                            std::vector<std::filesystem::path>* stale = nullptr)
{
    for (auto const& [relPath, content] : manifest.buildConfig.generatedFiles) {
        if (relPath.empty()) {
            return std::unexpected("generated_files contains an empty path");
        }
        if (relPath.is_absolute()) {
            return std::unexpected(std::format(
                "generated_files path '{}' must be relative", relPath.generic_string()));
        }
        auto const genericPath = relPath.generic_string();
        for (std::size_t begin = 0; begin <= genericPath.size();) {
            auto const end = genericPath.find('/', begin);
            auto const part = genericPath.substr(begin, end == std::string::npos
                                                           ? std::string::npos
                                                           : end - begin);
            if (part == "..") {
                return std::unexpected(std::format(
                    "generated_files path '{}' must not escape the package root",
                    relPath.generic_string()));
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }

        auto out = root / relPath.lexically_normal();

        // Skip the write when the on-disk content is already identical: ninja
        // is mtime-driven, and an unconditional rewrite bumps the mtime every
        // build, recompiling every TU that #includes the materialized file
        // (via depfiles) — e.g. a frozen-snapshot config.h included by
        // thousands of TUs. Change detection is already owned by the
        // fingerprint (content is folded in above), so skipping only
        // preserves the mtime — mirroring the build.mcpp cache design,
        // which likewise avoids mtime churn on unchanged outputs.
        {
            std::ifstream is(out, std::ios::binary);
            if (is) {
                std::string existing((std::istreambuf_iterator<char>(is)),
                                     std::istreambuf_iterator<char>());
                if (is && existing == content) {
                    continue;
                }
            }
        }
        if (stale) {
            stale->push_back(out);
            continue;
        }

        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (ec) {
            return std::unexpected(std::format(
                "cannot create directory for generated file '{}': {}",
                out.string(), ec.message()));
        }
        std::ofstream os(out, std::ios::binary);
        if (!os) {
            return std::unexpected(std::format(
                "cannot write generated file '{}'", out.string()));
        }
        os << content;
        if (!os) {
            return std::unexpected(std::format(
                "failed while writing generated file '{}'", out.string()));
        }
    }
    return {};
}

// L1 cfg merge for ONE package's manifest (root or ANY dependency — path,
// git, or version/registry): append the matching conditional
// cflags/cxxflags/ldflags and sources (G1b) to its buildConfig. Sources also
// update the legacy modules.sources mirror — the scanner walks that.
//
// #229: this is the SINGLE funnel for cfg-conditional sources/flags — every
// package's manifest passes through exactly one call to this function,
// always immediately BEFORE that manifest is captured into `packages[]` via
// makePackageRoot()/propagateLinkFlags() (which snapshot buildConfig into
// privateBuild/linkUsage and into the root's propagated ldflags — merging
// any later than that point is silently lost for flags, though not for
// sources, which the modgraph scan re-reads live). Three call sites, one per
// loading branch, together cover every package exactly once: the root
// (before its own makePackageRoot), the path/git-dep branch, and
// loadVersionDep() (shared by the main per-dependency loop, the
// multi-version mangling secondary, and the SemVer-merge re-fetch — all three
// of ITS callers get the merge for free from the one call inside it).
// The dependency MAPS ride the same funnel (#359). They used to be merged by
// a hand-written loop at the root call site only, with a comment declaring a
// dependency's own conditional deps "out of scope". That was the #229 shape
// one level up: three call sites merged build inputs, ONE of them also merged
// deps, and nothing said why. A package's `[target.windows.dependencies]` is
// its own statement about itself and means the same thing whether the package
// is the root or someone's dependency.
// The resolved triple travels INSIDE `ctx` (cfgpred::Ctx::triple). It used to
// be a third parameter here too, which is how a bare-triple predicate came to
// disagree with a cfg() one about the same native build — see the note on Ctx.
// `[target.<sel>.xlings…]` — the TARGET axis of the tool plane (SPEC-004 §4).
//
// Folded into `m.xlings` exactly as the conditional build inputs fold into
// `m.buildConfig`, so every downstream reader — the two provisioning passes,
// `fillXpkgDirs`, the materialised `.xlings.json` — stays on one flat list and
// none of them has to learn that a second axis exists.
//
// THE HOST AXIS IS NOT TOUCHED. Top-level `[xlings]` has already resolved its
// platform-keyed values against the host by the time this runs; folding here
// puts the target's entries beside them.
//
// DEDUP IS BY PACKAGE, NOT BY ADDRESS. `xim:glibc` and `xim:glibc@2.40` are two
// addresses for ONE install, and keeping both asks xlings for the same package
// twice at two versions — which is not a build that fails, it is a build whose
// environment depends on iteration order. Where both axes name a package, the
// conditional entry wins, because it is the more specific statement; that is
// the rule the flag half of this merge follows by appending after the base
// entries. A disagreement is reported, because it is the one case where the
// author wrote two things and only one of them can happen.
export void merge_conditional_xlings(mcpp::manifest::Manifest& m,
                                     const mcpp::manifest::ConditionalConfig& cc)
{
    // ONE DEFINITION OF IDENTITY, and it is not local to this merge. It used
    // to be `parse_address(a).target` — the bare name, so `xim:cuda` and a
    // hypothetical `scode:cuda` collided, and the graph split a few thousand
    // lines below compared whole address strings instead. See
    // mcpp.xlings.address_set for what the two definitions cost.
    auto package_of = [](std::string_view address) {
        return mcpp::xlings::addrset::package_key(address);
    };
    for (auto const& a : cc.xlings.deps) {
        const auto pkg = package_of(a);
        auto it = std::ranges::find_if(m.xlings.deps, [&](const std::string& e) {
            return package_of(e) == pkg;
        });
        if (it == m.xlings.deps.end()) { m.xlings.deps.push_back(a); continue; }
        if (*it != a)
            mcpp::diag::warning("xlings/axis-override", std::format(
                "'{}' is declared on both tool axes, as '{}' and as '{}'. The "
                "[target.<selector>] entry is the more specific statement and "
                "is the one used. Declare a tool that runs on the build machine "
                "in the top-level [xlings.workspace], and what the produced "
                "code is compiled against under [target.<selector>.xlings."
                "workspace] — see docs/05 section 2.13.", pkg, *it, a));
        *it = a;
    }
    // Keyed by PACKAGE, so the same override applies without a second search.
    for (auto const& [pkg, pin] : cc.xlings.workspace)
        m.xlings.workspace.insert_or_assign(pkg, pin);
    // Keyed by ADDRESS. `insert_or_assign` rather than `try_emplace` for the
    // same reason: the address that survived above is the conditional one.
    for (auto const& [addr, w] : cc.xlings.depWhen)
        m.xlings.depWhen.insert_or_assign(addr, w);
    for (auto const& [f, addrs] : cc.xlings.featureDeps) {
        auto& dst = m.xlings.featureDeps[f];
        for (auto const& a : addrs) {
            const auto pkg = package_of(a);
            auto it = std::ranges::find_if(dst, [&](const std::string& e) {
                return package_of(e) == pkg;
            });
            if (it == dst.end()) dst.push_back(a); else *it = a;
        }
    }
    for (auto const& [addr, pin] : cc.xlings.featurePins)
        m.xlings.featurePins.insert_or_assign(addr, pin);
}

// A `[target.<selector>.xlings…]` selector MUST NOT name a RESOLVED layer.
//
// Not a style rule, a schedule one. The five resolved layer keys (`c-abi`,
// `compiler`, …) are answered by dependency RESOLUTION, so a predicate naming
// one is held back to the second merge pass further down — which runs after
// every package's build.mcpp has already run and after the root's tool
// provisioning. An entry admitted there would be declared and never installed,
// and the failure it produces is the worst-shaped one there is: the build
// succeeds and the tool is simply absent.
//
// `accelerator` IS ADMITTED, and used to be refused here with the rest. It is
// not resolved from anything: it is `--accel`, or `[build] accel`, read before
// the first package is looked up, so a payload predicated on it is merged in
// the FIRST pass and installed like any other. Refusing it had a cost paid on
// every build of every project with a device island — the vendor toolkit is
// declared unconditionally or not at all, so a CPU-only build downloaded
// gigabytes for a device it was not compiling for.
//
// Refused rather than deferred, and refused at the earliest point that can
// see the predicate. The gate plane answers the case this shape is reached
// for: `[feature-xlings.<f>]` selects a tool by what the project asked for,
// and a feature is known before anything is provisioned.
export std::optional<std::string>
layer_predicated_xlings_refusal(const mcpp::manifest::Manifest& m)
{
    for (auto const& cc : m.conditionalConfigs) {
        if (cc.xlings.empty()) continue;
        if (!cfgpred::uses_layer(cc.predicate)) continue;
        std::string named;
        for (auto const& a : cc.xlings.deps) {
            if (!named.empty()) named += ", ";
            named += a;
        }
        for (auto const& [f, addrs] : cc.xlings.featureDeps)
            for (auto const& a : addrs) {
                if (!named.empty()) named += ", ";
                named += std::format("{} (feature '{}')", a, f);
            }
        return std::format(
            "[target.'{}'] declares tools ({}), but its predicate names a "
            "target-side layer. A layer is answered by dependency resolution, "
            "which happens after tools are installed and after build programs "
            "run, so a tool conditioned on one would be declared and never "
            "installed. Condition it on the target instead "
            "(`[target.'cfg(os = \"linux\")'.xlings.workspace]`), on the "
            "accelerator (`[target.'cfg(accelerator = \"cuda\")'.xlings"
            ".workspace]`, which IS answered before provisioning), or on a "
            "feature (`[feature-xlings.<feature>]`). See docs/05 section 2.13.",
            cc.predicate, named);
    }
    return std::nullopt;
}

// Two declarations of one dependency, compared by the identity their keys
// normalise to rather than by the keys themselves: `fw` and `mcpplibs.fw` are
// one package under two map keys (`selector.stableMapKey`), and a comparison
// of keys would leave both entries in the map for the resolver to see.
bool same_dependency_identity(const mcpp::manifest::DependencySpec& a,
                              const mcpp::manifest::DependencySpec& b) {
    if (a.shortName.empty() || b.shortName.empty()) return false;
    return a.namespace_ == b.namespace_ && a.shortName == b.shortName;
}

void replace_dependencies(
    std::map<std::string, mcpp::manifest::DependencySpec>& into,
    const std::map<std::string, mcpp::manifest::DependencySpec>& from)
{
    for (auto const& [key, spec] : from) {
        std::erase_if(into, [&](auto const& entry) {
            return entry.first == key || same_dependency_identity(entry.second, spec);
        });
        into[key] = spec;
    }
}

export void merge_conditional_config(mcpp::manifest::Manifest& m,
                                    const cfgpred::Ctx& ctx)
{
    // Recorded before the first merge; see Manifest::beforeConditionalMerge.
    if (!m.beforeConditionalMerge)
        m.beforeConditionalMerge = std::make_shared<const mcpp::manifest::Manifest>(m);
    // A DISTRIBUTION package may carry a leg's link line twice: as `ldflags`
    // (GNU spelling, which is all an older mcpp reads) and as the neutral
    // `[target.<pred>.runtime]` pair, which mcpp renders per dialect. Applying
    // both would put `-L` on a native `cl.exe` command line, which is exactly
    // what the neutral form exists to avoid — so where the neutral form is
    // present it REPLACES the ldflags rather than adding to them.
    //
    // Scoped to distribution packages on purpose: a hand-written manifest that
    // states both may well mean both (`ldflags` also carries things like
    // `-Wl,--as-needed`), and silently dropping half of it would be its own
    // silent failure.
    const bool generatedPackage = mcpp::pack::is_distribution_package(m);

    for (auto const& cc : m.conditionalConfigs) {
        // THE TWO PASSES MUST BE DISJOINT, AND `matches()` ALONE DOES NOT
        // MAKE THEM SO. A layer key answers false here because `layersKnown` is
        // false — but `cfg(any(linux, c-abi = "musl"))` still matches on its
        // triple leg, and the second pass would match it again and `append()`
        // the same inputs twice. Membership, not the answer, decides ownership:
        // a predicate that NAMES a layer belongs to the second pass entirely.
        if (cfgpred::uses_layer(cc.predicate)) continue;
        if (!cfgpred::matches(cc.predicate, ctx)) continue;
        const bool neutralWins = generatedPackage
                              && (!cc.linkLibraryDirs.empty() || !cc.libraries.empty()
                                  || !cc.frameworks.empty());
        // One append() for every field the axis may carry (#258). Matching
        // sections land AFTER the base entries, so a conditional rule beats
        // a broader unconditional one under GNU last-wins — which is what
        // makes an off-OS REMOVAL expressible (`-U` after the base `-D`).
        if (neutralWins) {
            // Drop the LIBRARY REFERENCES, not the whole ldflags list.
            //
            // Clearing it outright was a measured regression: a PE/MinGW shared
            // leg's ldflags also carry `-Wl,-Bdynamic`, without which `-static`
            // leaves ld in static-only mode and it refuses the import library
            // with `have you installed the static version of the mathkit
            // library?`. e2e 257 caught it.
            //
            // The neutral form replaces exactly what it can express — a library
            // and where to find it. Anything else in that block says something
            // it cannot say, and must survive.
            auto inputs = cc.inputs;
            std::erase_if(inputs.ldflags, [](std::string_view f) {
                return f.starts_with("-L") || f.starts_with("-l")
                    || f.starts_with("/LIBPATH:");
            });
            mcpp::manifest::append(m.buildConfig, inputs);
        } else {
            mcpp::manifest::append(m.buildConfig, cc.inputs);
        }
        // The neutral half goes where `render_link_intent_flags` will find it.
        for (auto const& d : cc.linkLibraryDirs)
            m.runtimeConfig.linkIntent.linkLibraryDirs.push_back(d);
        for (auto const& l : cc.libraries)
            m.runtimeConfig.linkIntent.libraries.push_back(l);
        for (auto const& f : cc.frameworks)
            m.runtimeConfig.linkIntent.frameworks.push_back(f);
        merge_conditional_xlings(m, cc);
        // `[target.<sel>.abi]`: recorded for every package; rendered only for
        // the root, where prepare_build reads it. Last matching section wins,
        // the rule every other conditional scalar follows.
        if (cc.abiThreadsDeclared) {
            m.buildConfig.abiThreads = cc.abiThreads;
            m.buildConfig.abiThreadsDeclared = true;
        }
        if (cc.abiExceptionsDeclared) {
            m.buildConfig.abiExceptions = cc.abiExceptions;
            m.buildConfig.abiExceptionsDeclared = true;
        }
        // `[target.<sel>] requires_abi` / `.feature-requires-abi` (A6): a
        // requirement on the TARGET axis, unioned in -- not overwritten --
        // because more than one matching selector may ask for the same
        // member, and every one of them is a true statement. The selector
        // text rides along so the unmet-requirement check can name what
        // asked, the same courtesy `[package] requires_abi` gets by naming
        // "the package" and a feature's entry by naming the feature.
        if (cc.requiresAbiThreads)
            m.targetRequiresAbiThreads.push_back(cc.predicate);
        if (cc.requiresAbiExceptions)
            m.targetRequiresAbiExceptions.push_back(cc.predicate);
        for (auto const& [f, val] : cc.featureRequiresAbiThreads)
            if (val) m.targetFeatureRequiresAbiThreads[f].push_back(cc.predicate);
        for (auto const& [f, val] : cc.featureRequiresAbiExceptions)
            if (val) m.targetFeatureRequiresAbiExceptions[f].push_back(cc.predicate);
        // `modules.sources` is the scanner's own view and is not part of
        // BuildInputs, so conditional sources are mirrored into it here.
        for (auto const& s : cc.inputs.sources)
            m.modules.sources.push_back(s);
        // A matching conditional declaration of a dependency REPLACES the
        // declaration of the same identity, and a later matching section
        // replaces an earlier one: the rule every conditional scalar above
        // follows (#634, A1). This used to be `insert()`, which kept the
        // unconditional entry, so `linkage = "shared"` written for one row was
        // dropped on that row without a word. No manifest among 509 scanned
        // declared one dependency in both tables, so no build that worked
        // changes; the declaring table rides on the spec (`declaredIn`) into
        // the resolution record.
        replace_dependencies(m.dependencies, cc.dependencies);
        replace_dependencies(m.devDependencies, cc.devDependencies);
        replace_dependencies(m.buildDependencies, cc.buildDependencies);
        // #359: `[target.<sel>.feature-deps.<feature>]`. The feature is
        // registered by the parser regardless of the predicate; only what it
        // pulls in is conditional.
        for (auto const& [fname, deps] : cc.featureDeps)
            replace_dependencies(m.featureDeps[fname], deps);
        // `[target.<sel>.targets.<name>] kind`: the row's form of a library
        // target, applied before resolution, so the link-form resolution
        // reads it exactly as it reads `[targets.<name>] kind`. `load` has
        // already refused a name that is not a library target.
        //
        // `linkage` (#642 E1) is the row's default form, and a row's statement
        // REPLACES the statement it follows, whichever of the two each one is:
        // a default after `kind = "shared"` returns the target to the library
        // form a consumer may choose from, and a `kind` after a default clears
        // the default. Last matching section wins, as for every conditional
        // scalar.
        for (auto const& [name, row] : cc.targetKinds) {
            for (auto& t : m.targets) {
                if (t.name != name) continue;
                t.kindDeclaredBy = row.statement;
                t.kindFromRow = true;
                if (!row.linkage.empty()) {
                    t.kind = mcpp::manifest::Target::Library;
                    t.linkageDefault = row.linkage;
                    t.linkageDeclaredBy = row.statement;
                } else {
                    t.kind = row.kind;
                    t.linkageDefault.clear();
                    t.linkageDeclaredBy.clear();
                }
            }
        }
    }
}

// ── An element whose words changed in 2026.9.17.1 (#655) ─────────────────────
//
// A compile-flag element used to reach the compiler as its host's command-line
// reader made it (POSIX `sh`, or the MSVCRT rules), after ninja had replaced
// `$` sequences, with a `-D` element containing a space quoted whole (#234).
// It now reaches the compiler as `flag_words` reads it, and a `defines` value
// is one word. Most spellings mean the same under both readings; the ones that
// do not are told what the compiler receives now and what it received before.
// The previous reading is modelled on quote removal only: `sh` expansions
// (`$VAR`, globs) are not reproduced.
//
// WHEN IT IS SAID. The notes are collected while manifests load and released
// where the output directory is decided, only if that directory has no
// build.ninja yet. The fingerprint names the mcpp version and every flag, so
// that is the first plan after an upgrade, after a flag was edited, or in a
// fresh checkout. A build that repeats a plan says nothing, so a manifest that
// is already spelled for the new reading is not warned about on every run.
std::vector<std::pair<std::string, std::string>>& pending_flag_words_notes() {
    static std::vector<std::pair<std::string, std::string>> notes;
    return notes;
}
std::vector<std::string> previous_release_words(std::string element, bool define) {
    if (define) element = "-D" + element;
    if ((element.starts_with("-D") || element.starts_with("/D"))
        && element.find(' ') != std::string::npos)
        element = mcpp::build::shell_quote_arg(element);
    std::string line;
    for (std::size_t i = 0; i < element.size(); ++i) {
        if (element[i] != '$' || i + 1 == element.size()) { line.push_back(element[i]); continue; }
        const char n = element[i + 1];
        if (n == '$' || n == ' ' || n == ':') { line.push_back(n); ++i; continue; }
        // A ninja variable reference: `${name}` or `$name`, empty on a compile edge.
        std::size_t j = i + 1;
        if (n == '{') {
            while (j < element.size() && element[j] != '}') ++j;
        } else {
            while (j + 1 < element.size()
                   && (std::isalnum(static_cast<unsigned char>(element[j + 1]))
                       || element[j + 1] == '_' || element[j + 1] == '-'))
                ++j;
        }
        i = j;
    }
    return mcpp::manifest::host_command_words(line, mcpp::platform::is_windows);
}

void report_flag_words_changes(const mcpp::manifest::Manifest& m) {
    auto show = [](const std::vector<std::string>& words) {
        std::string out = "[";
        for (auto const& w : words)
            out += std::format("{}'{}'", out.size() > 1 ? ", " : "", w);
        return out + "]";
    };
    auto note_change = [&](std::string what, std::string hint) {
        auto note = std::pair{std::move(what), std::move(hint)};
        auto& notes = pending_flag_words_notes();
        if (std::ranges::find(notes, note) == notes.end()) notes.push_back(std::move(note));
    };
    auto const who = m.package.name.empty() ? std::string("(root)") : m.package.name;
    auto check = [&](std::string_view where, const std::vector<std::string>& list,
                     bool define) {
        for (auto const& e : list) {
            auto now = define ? std::vector<std::string>{"-D" + e}
                              : mcpp::manifest::flag_words(e);
            auto before = previous_release_words(e, define);
            if (now == before) continue;
            note_change(std::format(
                "{}: {} element '{}' reaches the compiler as {}; mcpp before "
                "2026.9.17.1 passed {} on this host",
                who, where, e, show(now), show(before)),
                std::string(
                "a compile-flag element is read by one syntax on every host, and a "
                "`defines` entry is one value (docs/04-mcpp-toml.md, "
                "\"Compile-flag syntax\"); spell the element so that it reads as the "
                "words meant"));
        }
    };
    // THE SAME QUESTION FOR THE LINK FLAGS, which take the reading from
    // 2026.9.26.2 (#703). Before, a `-L` or `-Wl,-rpath,` element was escaped
    // for ninja, so its text reached the host's reader as written, and any
    // other element was pasted into the ninja rule, so ninja replaced its `$`
    // sequences first. `$ORIGIN` written plainly reads the same under both
    // models, because neither reproduces the shell's expansion that lost it;
    // an element escaped for ninja or for the shell by hand is what differs.
    auto check_link = [&](std::string_view where, const std::vector<std::string>& list) {
        for (auto const& e : list) {
            auto now = mcpp::manifest::flag_words(e);
            auto before = e.starts_with("-L") || e.starts_with("-Wl,-rpath,")
                ? mcpp::manifest::host_command_words(e, mcpp::platform::is_windows)
                : previous_release_words(e, false);
            if (now == before) continue;
            note_change(std::format(
                "{}: {} element '{}' reaches the linker as {}; mcpp before "
                "2026.9.26.2 passed {} on this host",
                who, where, e, show(now), show(before)),
                std::string(
                "a link-flag element is read by the compile-flag syntax, so `$ORIGIN` "
                "reaches the linker as written and an element escaped for ninja or the "
                "shell by hand is no longer unescaped (docs/04-mcpp-toml.md, "
                "\"Compile-flag syntax\"); spell the element so that it reads as the "
                "words meant"));
        }
    };
    auto const& bc = m.buildConfig;
    check_link("[build] ldflags", bc.ldflags);
    check("[build] cflags", bc.cflags, false);
    check("[build] cxxflags", bc.cxxflags, false);
    check("[build] defines", bc.defines, true);
    for (auto const& gf : bc.globFlags) {
        check("flags cflags", gf.cflags, false);
        check("flags cxxflags", gf.cxxflags, false);
        check("flags asmflags", gf.asmflags, false);
        check("flags defines", gf.defines, true);
    }
    for (auto const& [feature, defines] : bc.featureDefines)
        check(std::format("features.{} defines", feature), defines, true);
    for (auto const& [feature, globs] : bc.featureFlags) {
        for (auto const& gf : globs) {
            check(std::format("features.{} cflags", feature), gf.cflags, false);
            check(std::format("features.{} cxxflags", feature), gf.cxxflags, false);
            check(std::format("features.{} asmflags", feature), gf.asmflags, false);
            check(std::format("features.{} defines", feature), gf.defines, true);
        }
    }
    for (auto const& t : m.targets) {
        check(std::format("targets.{} cflags", t.name), t.cflags, false);
        check(std::format("targets.{} cxxflags", t.name), t.cxxflags, false);
        check(std::format("targets.{} defines", t.name), t.defines, true);
    }
}

// The macro name a `defines` entry or a `-D` word defines: the text before
// the first `=`, or the whole text when there is no value.
std::string_view define_name(std::string_view entry) {
    return entry.substr(0, entry.find('='));
}

// Desugar `[build].defines` into `-D<x>` on both C and C++ flag channels.
//
// ORDER (both halves are load-bearing): this must run AFTER every merge that
// contributes `defines` (workspace inheritance, then the package's own table,
// then a matching `[target.'cfg(...)'.build]`), and BEFORE the manifest is
// snapshotted into packages[] / fingerprinted, because that snapshot (not the
// manifest) is what the P1689 scan, the compile edges and compute_fingerprint
// actually read. `makePackageRoot` refuses a manifest whose `defines` are
// still unfolded.
//
// `defines` IS A SET KEYED BY MACRO NAME (SPEC-004 §8). A later entry for a
// name replaces the earlier one in place, so a member that restates an
// inherited `NAME=value` produces one `-DNAME=value` word instead of two
// words and a redefinition diagnostic; an entry `!NAME` removes the name.
// A list is not enough for this, because the compiler resolves a repeated
// `-D` by warning (an error under `-Werror`) and a `-U` written in `cxxflags`
// precedes every folded `-D` and so cannot remove one.
//
// The key covers every `-D<NAME>` word already in the flag lists as well:
// those written in `cflags`/`cxxflags` of the same tables, and those folded
// by an earlier call (the layer-conditional pass calls this again with only
// its own entries). A name this call defines or removes supersedes them, so
// the package's compile lines carry at most one definition per name.
//
// Idempotent: clearing the vector after folding makes repeated calls harmless.
// Both `cflags` and `cxxflags` get the macro; assembly units pick it up for
// free via the -D/-U/-I subset the ninja backend filters out of packageCflags.
// A define is a value, so it enters the flag list as one word
// (`flag_element`): `N="x"` reaches the compiler as `-DN="x"` on every host.
export void fold_build_defines_into_flags(mcpp::manifest::BuildConfig& bc) {
    if (bc.defines.empty()) return;

    std::vector<std::string> resolved;          // entries, first-seen order
    std::vector<std::string> named;             // every name this call touches
    auto touch = [&](std::string_view name) {
        if (std::ranges::find(named, name) == named.end())
            named.emplace_back(name);
    };
    for (auto const& d : bc.defines) {
        if (d.starts_with('!')) {
            const auto name = std::string_view(d).substr(1);
            std::erase_if(resolved, [&](const std::string& e) {
                return define_name(e) == name;
            });
            touch(name);
            continue;
        }
        const auto name = define_name(d);
        touch(name);
        auto it = std::ranges::find_if(resolved, [&](const std::string& e) {
            return define_name(e) == name;
        });
        if (it != resolved.end()) *it = d;
        else resolved.push_back(d);
    }

    auto superseded = [&](const std::string& element) {
        auto words = mcpp::manifest::flag_words(element);
        if (words.size() != 1 || !words.front().starts_with("-D")) return false;
        const auto name = define_name(std::string_view(words.front()).substr(2));
        return std::ranges::find(named, name) != named.end();
    };
    std::erase_if(bc.cflags, superseded);
    std::erase_if(bc.cxxflags, superseded);

    for (auto const& d : resolved) {
        const auto element = mcpp::manifest::flag_element("-D" + d);
        bc.cflags.push_back(element);
        bc.cxxflags.push_back(element);
    }
    bc.defines.clear();
}

// The post-condition of the normalisation pipeline, as the snapshot checks it:
// every `defines` entry has been folded into the flag lists. A non-empty list
// here means a merge ran after the fold, and the entries would otherwise be
// dropped without a diagnostic (#690). Returns the internal-error text, or
// nothing when the manifest may be captured.
export std::optional<std::string>
unfolded_defines_error(const mcpp::manifest::Manifest& m) {
    auto const& d = m.buildConfig.defines;
    if (d.empty()) return std::nullopt;
    return std::format(
        "internal error: [build].defines of package '{}' reached the build "
        "graph unfolded ({} entr{}, first '{}'); a merge ran after "
        "fold_build_defines_into_flags (please report)",
        m.package.name.empty() ? std::string("(root)") : m.package.name,
        d.size(), d.size() == 1 ? "y" : "ies", d.front());
}

// WHAT A MEMBER RECEIVES FROM ITS WORKSPACE WHEN IT IS REACHED AS A DEPENDENCY.
//
// Three parts of the inheritance matter to a dependency: `[workspace.package]`
// (a member may omit `version`), `x.workspace = true` dependency entries
// (without the merge the entry reaches resolution with neither version nor
// path), and `[workspace.build]`. They are applied at the dependency's LOAD
// site, before the conditional merge and the `defines` fold, which is the
// order the root follows; `makePackageRoot` only captures the result (#690).
// `[toolchain]`, `[target.<triple>]` and `[indices]` are decided by the root
// for the whole graph and are not applied to a dependency.
//
// One function for every way a member is reached: a sibling `path`
// dependency, a member of a git-hosted workspace, and a member inside an
// index package's archive. The same commit then compiles the same way in its
// own checkout and in every consumer's graph.
std::optional<std::string>
inherit_as_workspace_member(mcpp::manifest::Manifest& member,
                            const mcpp::manifest::Manifest& workspace,
                            const std::filesystem::path& workspaceRoot,
                            const std::filesystem::path& memberDir) {
    mcpp::project::inherit_workspace_package(member, workspace);
    mcpp::project::merge_workspace_deps(member, workspace, workspaceRoot);
    mcpp::project::inherit_workspace_build(member, workspace, workspaceRoot);
    mcpp::project::inherit_workspace_xlings(member, workspace);
    return mcpp::project::workspace_inheritance_error(member, memberDir);
}

// The workspace whose `members` list `memberDir`, searched upward from its
// parent and never above `bound` (an index package's install root: the
// archive is the only tree the package's author wrote).
std::optional<std::pair<mcpp::manifest::Manifest, std::filesystem::path>>
workspace_listing(const std::filesystem::path& memberDir,
                  const std::filesystem::path& bound) {
    auto inside = [&](const std::filesystem::path& p) {
        auto rel = p.lexically_normal().lexically_relative(bound.lexically_normal());
        return !rel.empty() && *rel.begin() != "..";
    };
    for (auto p = memberDir.parent_path(); inside(p); p = p.parent_path()) {
        if (std::filesystem::exists(p / "mcpp.toml")) {
            if (auto ws = mcpp::manifest::load(p / "mcpp.toml");
                ws && ws->workspace.present
                && mcpp::project::is_workspace_member(*ws, p, memberDir))
                return std::pair{std::move(*ws), p};
        }
        if (p == p.parent_path()) break;
    }
    return std::nullopt;
}

// ── The SECOND conditional pass: predicates that name a target-side layer ────
//
// #540/#494. `docs/14` documents a package adapting to the C library it was
// built over — `[target.'cfg(c-abi = "musl")'.build] std-module-flags =
// ["-D_GNU_SOURCE"]`, wrong for picolibc — and `stdModuleFlags` was moved onto
// BuildInputs FOR this, its member comment saying membership "is what makes the
// cfg axis carry it". Nothing evaluated the predicate: `cfgpred::Ctx` was built
// from the triple alone, so every such section was dropped in silence and the
// package built with the wrong C-library configuration, successfully.
//
// WHY A SECOND PASS AND NOT AN EARLIER CONTEXT. A layer is answerable only
// after dependency resolution — a package in the graph may supply the C library
// (openkal-musl under a `-gnu` triple), which is exactly why the triple's `env`
// segment is a REQUEST and not the answer (docs/specs/target-side.md §3.4). The
// first merge runs before resolution because conditional DEPENDENCIES have to.
//
// WHERE IT RUNS. Between `tsd::resolve` and the P1689 scan — the same window in
// which build.mcpp already contributes build inputs by mirroring into
// `packages[0]`. Everything downstream reads the snapshot from there on: the
// scan, `stdModuleFlags` collection, the fingerprint, and `compute_flags`.
//
// SCOPE. Build INPUTS only, which is what docs/14 promises ("available in
// [build] sections only"). Dependencies are excluded by construction — they are
// already resolved by now — and a section that tries is reported rather than
// silently ignored; see `warn_layer_predicate_dependencies`.
bool merge_layer_conditional_config(mcpp::manifest::Manifest& m,
                                    const cfgpred::Ctx& ctx)
{
    bool any = false;
    for (auto const& cc : m.conditionalConfigs) {
        if (!cfgpred::uses_layer(cc.predicate)) continue;
        if (!cfgpred::matches(cc.predicate, ctx)) continue;
        any = true;
        mcpp::manifest::append(m.buildConfig, cc.inputs);
        // Same mirror the first pass does: `modules.sources` is the scanner's
        // own view and is not a BuildInputs member.
        for (auto const& s : cc.inputs.sources)
            m.modules.sources.push_back(s);
        for (auto const& d : cc.linkLibraryDirs)
            m.runtimeConfig.linkIntent.linkLibraryDirs.push_back(d);
        for (auto const& l : cc.libraries)
            m.runtimeConfig.linkIntent.libraries.push_back(l);
        for (auto const& f : cc.frameworks)
            m.runtimeConfig.linkIntent.frameworks.push_back(f);
        // NO `merge_conditional_xlings` HERE, DELIBERATELY. A cc that reaches
        // this pass has a layer in its predicate, and one carrying tools was
        // refused long before — see `layer_predicated_xlings_refusal`. Folding
        // it here would be folding after the tools were provisioned and after
        // every build.mcpp ran, which is the silent-absence failure the
        // refusal exists to prevent.
    }
    // Re-fold only when something was added. The call is safe either way — the
    // function clears `defines` after folding and says so — but skipping it
    // keeps this pass a no-op for the overwhelming majority of manifests, which
    // name no layer at all.
    if (any) fold_build_defines_into_flags(m.buildConfig);
    return any;
}

// Feature-activation closure — THE single implementation (build.mcpp env
// contract, Stage 2a feature-deps, and the main feature pass all call this):
// seed = [features].default ∪ requested, expanded transitively over implies;
// the literal name "default" is never itself a feature.
//
// `seedDefault` is the funnel for consumer-side `default-features = false`
// (#242, Cargo parity): when false the dependency's own `[features].default`
// is NOT seeded, so only the explicitly `requested` features (and their
// transitive `implies`) activate. The root package always seeds its own
// default (seedDefault=true); a dependency passes its dep spec's
// `defaultFeatures` flag. `requested` is applied identically either way.
std::vector<std::string> feature_closure(const mcpp::manifest::Manifest& pm,
                                         const std::vector<std::string>& requested,
                                         bool seedDefault = true)
{
    std::vector<std::string> act, q;
    if (seedDefault)
        if (auto it = pm.featuresMap.find("default"); it != pm.featuresMap.end())
            q.insert(q.end(), it->second.begin(), it->second.end());
    q.insert(q.end(), requested.begin(), requested.end());
    std::set<std::string> seen;
    while (!q.empty()) {
        auto f = q.back(); q.pop_back();
        if (f == "default" || !seen.insert(f).second) continue;
        act.push_back(f);
        if (auto it = pm.featuresMap.find(f); it != pm.featuresMap.end())
            q.insert(q.end(), it->second.begin(), it->second.end());
    }
    return act;
}

// --features value → tokens (comma/space separated).
std::vector<std::string> feature_request_tokens(std::string_view s) {
    std::vector<std::string> out;
    for (std::size_t p = 0; p < s.size();) {
        auto c = s.find_first_of(", ", p);
        auto tok = s.substr(p, c == std::string_view::npos ? std::string_view::npos : c - p);
        if (!tok.empty()) out.emplace_back(tok);
        if (c == std::string_view::npos) break;
        p = c + 1;
    }
    return out;
}

// The root's own features among the --features tokens.
//
// A TOKEN CONTAINING `/` IS NOT A FEATURE OF THE ROOT (#649 E8). It can only
// mean "open this feature of that dependency", which is what the same token
// means inside `[features]`, so it is taken out here and applied as a forward
// of the root (`feature_forward_request`). It used to stay in this list, where
// a root without `[features]` turned it into `-DMCPP_FEATURE_SPIKE_FW_INSTALLER`
// and a root with the table reported it as an undeclared feature; neither
// opened the dependency's feature.
std::vector<std::string> parse_feature_request(std::string_view s) {
    std::vector<std::string> out;
    for (auto& tok : feature_request_tokens(s))
        if (tok.find('/') == std::string::npos) out.push_back(std::move(tok));
    return out;
}

// The `<dependency key>/<feature>` tokens of --features, in the keyspace of a
// `[features]` forward. A token with an empty half is kept whole and named by
// the caller, rather than being dropped as the manifest parser drops it: on a
// command line the user typed it just now.
std::vector<std::string> feature_forward_request_tokens(std::string_view s) {
    std::vector<std::string> out;
    for (auto& tok : feature_request_tokens(s))
        if (tok.find('/') != std::string::npos) out.push_back(std::move(tok));
    return out;
}

bool is_std_module(std::string_view name) {
    return name == "std" || name == "std.compat";
}

bool graph_or_targets_import_std(const mcpp::modgraph::Graph& graph,
                                 const mcpp::manifest::Manifest& manifest,
                                 const std::filesystem::path& projectRoot) {
    for (auto& u : graph.units) {
        for (auto& req : u.requires_) {
            if (is_std_module(req.logicalName))
                return true;
        }
    }

    // Some target entry files can be added to the plan after the package scan.
    // Check them here so std BMI setup matches what make_plan will compile: they
    // are read by the same scan_entry_file make_plan reads them with.
    const auto extTable = mcpp::extension_table_for(manifest.buildConfig.moduleExtensions,
                                                    manifest.buildConfig.deviceExtensions);
    for (auto& t : manifest.targets) {
        if (t.main.empty()) continue;
        const auto entry = mcpp::modgraph::scan_entry_file(projectRoot / t.main,
                                                           manifest.package.name, extTable);
        for (auto const& req : entry.requires_)
            if (is_std_module(req.logicalName)) return true;
    }
    return false;
}

// How this invocation may use the global dependency cache.
//
//   Global  read + write  (default)
//   Local   neither — every dependency is compiled inside this project's
//           target/, which is what every build did before the cache worked
//   Off     neither, and this build's target/<triple>/<fp>/ directory is
//           cleared first (a full cold rebuild). Sibling build dirs — other
//           profiles, other targets — are left alone.
//
// `--no-cache` used to be the only switch and it meant "clear the build dir",
// which says nothing about a cache (and its help text claimed all of target/);
// it stays as a deprecated alias for Off.
// Where the resolved toolchain spec came from.
//
// This exists so mcpp can tell its own guesses apart from the user's
// instructions. When a resolved toolchain turns out to be unusable on this
// machine (the motivating case: a Windows default that targets the MSVC ABI
// on a box with no Visual Studio), mcpp may quietly revise a default it
// picked itself — but a spec the user wrote into mcpp.toml must produce an
// error instead. A project that needs the MSVC ABI to link vcpkg-built .lib
// files is worse off with a silent ABI swap than with a failed build.
//
// Deliberately derived from the two config layers that already exist rather
// than persisted: no new field, nothing to keep in sync on disk.
export enum class TcOrigin {
    None,               // nothing resolved yet
    ManifestToolchain,  // mcpp.toml [toolchain]           — user explicit
    TargetSection,      // mcpp.toml [target.X].toolchain  — user explicit
    GlobalDefault,      // `mcpp toolchain default`        — user explicit
    TargetPin,          // triple.cppm vocabulary convention
    GraphRequirement,   // `requires = ["mcpp:compiler=…"]` in the graph
    FirstRun,           // chosen and persisted by this very invocation
};

// `GlobalDefault` IS DELIBERATELY NOT LISTED, AND THE REASON IS A MEASURED
// REGRESSION RATHER THAN A JUDGEMENT ABOUT WHOSE OPINION COUNTS.
//
// A target row's pin does not name a preferred compiler. It names the payload
// that supplies THAT TARGET'S C library — the mingw payload for
// `x86_64-windows-gnu`, the musl-gcc payload for `*-linux-musl`. Whether the
// user's own default can serve the target instead depends on whether something
// ELSE supplies the target side, and that is knowable only after the dependency
// graph is resolved, which is after this line.
//
// Making the global default outrank the pin was tried and measured: a project
// with no dependencies, a global default of `llvm@22.1.8` and
// `--target x86_64-windows-gnu` stopped building, because clang alone carries no
// C runtime for that target while the payload the row names does. That is a
// working build turned into a failing one by an upgrade.
//
// What the user actually loses is ergonomics, and that is addressed where it is
// visible: when the pin replaces a default the user wrote down, the status line
// SAYS SO and names the one-line override. The structural fix is to defer the
// pin the way the target side itself was deferred — resolve it after the graph,
// where the question it answers has an answer.
export inline bool tc_origin_is_user_explicit(TcOrigin o) {
    return o == TcOrigin::ManifestToolchain || o == TcOrigin::TargetSection;
}

// MAY A BUILD THAT RESOLVED THIS WAY WRITE THE MACHINE'S DEFAULT?
//
// `GraphRequirement` is the one origin that must not: it is a property of a
// package this project depends on, not of this machine. Two branches persist a
// default — the Windows first-run diversion, whose condition is
// `tcSpec.has_value()`, and the MSVC repair, whose gate is "mcpp chose this
// itself" — and a compiler chosen by `requires = ["mcpp:compiler=…"]` satisfies
// both. Measured against the design rather than a run, because it needs a
// Windows box with no toolchain: a bare machine building ONE llvm-requiring
// project would have handed llvm to every later project that asked for nothing.
//
// NAMED RATHER THAN SPELLED INLINE AT EACH SITE. There are two today; the
// third would be written by someone who never read this note, and a predicate
// with a name is something they can find.
export inline bool tc_origin_may_persist(TcOrigin o) {
    return o != TcOrigin::GraphRequirement;
}

// How a resolution came about, for the status line. A convention that replaced
// nothing needs no explanation; one that replaced a user's stated preference is
// a decision the user did not make and must be told about.
export constexpr std::string_view tc_origin_name(TcOrigin o) {
    switch (o) {
        case TcOrigin::ManifestToolchain: return "[toolchain] in mcpp.toml";
        case TcOrigin::TargetSection:     return "[target.<triple>] in mcpp.toml";
        case TcOrigin::GlobalDefault:     return "your default";
        case TcOrigin::TargetPin:         return "target default";
        case TcOrigin::GraphRequirement:  return "required by the dependency graph";
        case TcOrigin::FirstRun:          return "first-run default";
        case TcOrigin::None:              break;
    }
    return {};
}

// What to tell a user whose build targets the MSVC ABI on a machine that
// cannot serve it. Two shapes, because the two states need different fixes:
//
//   • cl.exe was found but the Windows SDK was not — a half-installed VS.
//     Point at the missing SDK component; switching toolchains would be an
//     over-correction for someone who clearly wants MSVC.
//   • nothing usable at all — the bare-Windows case. Lead with the MinGW-w64
//     route, which needs no Visual Studio and is already a verified target,
//     and keep the "install the C++ workload" option second.
export std::string msvc_unavailable_guidance(const mcpp::toolchain::Toolchain& tc) {
    namespace pins = mcpp::toolchain::triple::pins;
    const bool haveVcTools = tc.compiler == mcpp::toolchain::CompilerId::MSVC;
    if (haveVcTools && mcpp::toolchain::msvc::find_msvc_tools_dir()) {
        return std::format(
            "msvc {} was detected at {}, but no Windows SDK was found —\n"
            "       cl.exe cannot compile without the UCRT/SDK headers.\n"
            "       Install the 'Windows 11 SDK' component via the Visual Studio\n"
            "       Installer (it is part of the Desktop development with C++\n"
            "       workload), then retry.",
            tc.version, tc.binaryPath.string());
    }
    return std::format(
        "this build targets the MSVC ABI, which needs Visual Studio /\n"
        "       Build Tools (MSVC STL + Windows SDK) — neither was found.\n"
        "\n"
        "       No Visual Studio? Use the self-contained MinGW-w64 toolchain\n"
        "       (no Visual Studio required, `import std` works):\n"
        "         mcpp toolchain default {} --target {}\n"
        "\n"
        "       Have Visual Studio? Install the 'Desktop development with C++'\n"
        "       workload — it provides the MSVC STL and the Windows SDK.",
        pins::kSuggestGccMingw, pins::kFirstRunWinGnuTarget);
}

export enum class CacheMode { Global, Local, Off };

export std::optional<CacheMode> parse_cache_mode(std::string_view v) {
    if (v == "global") return CacheMode::Global;
    if (v == "local")  return CacheMode::Local;
    if (v == "off" || v == "none") return CacheMode::Off;
    return std::nullopt;
}

export std::string_view cache_mode_name(CacheMode m) {
    switch (m) {
        case CacheMode::Local: return "local";
        case CacheMode::Off:   return "off";
        default:               return "global";
    }
}

// A condition a planning pass reports instead of acting on (plan_only): the
// code is stable and the message is for people. Emitted as diagnostics by the
// command that asked for the plan, at the severity carried here — most notes
// are warnings the document is still complete despite (a lock that would
// change, a generated file left unmaterialized); a build program whose run
// failed under `plan_only` (#699 item 2, E3) is an error, because the sets it
// would have shaped are described without its directives.
export struct PlanNote {
    std::string code;
    std::string message;
    mcpp::wire::Severity severity = mcpp::wire::Severity::Warning;
    // The absolute, native path of the file the condition is about (a
    // package's `build.mcpp`), empty when the note names no file.
    // `mcpp.build.build_database::render` rewrites it to the workspace-
    // relative form every other `path` in the document uses.
    std::string path;
};

export struct BuildContext {
    // THE PER-MACHINE JOB DEFAULT, carried so it is read once.
    //
    // `[build] default_jobs` in `$MCPP_HOME/config.toml` is the machine's
    // answer to "how many at once". `prepare_build` resolves it into the build
    // schedule itself; this field exists for the SECOND reader --
    // `mcpp test`'s runner concurrency (execute.cppm) -- which calls
    // `resolve_jobs` again after this function has returned. Recorded rather
    // than re-read, because a second `load_or_init` there would be a second
    // parser of one file, and because the two readers must not be able to
    // disagree about the machine.
    int                             globalDefaultJobs = 0;
    // --strict: degradations reported through mcpp::diag become errors.
    // Carried on the context because the build's degradations are discovered
    // during backend emission, i.e. after prepare_build has returned — the
    // single place that settles the policy is run_build_plan (execute.cppm).
    bool                            strict = false;
    mcpp::manifest::Manifest        manifest;
    mcpp::toolchain::Toolchain      tc;
    mcpp::toolchain::Fingerprint    fp;
    mcpp::xlings::runtime::RuntimeSelection runtimeSelection;
    mcpp::platform::runtime::RuntimeBinding runtimeBinding;
    std::filesystem::path           projectRoot;
    // THE SOURCE TREES THIS BUILD READ THAT ARE NOT UNDER `projectRoot`.
    //
    // A `path` dependency — which is what every workspace member is to its
    // siblings — contributes translation units from a directory the fast path
    // has no way to name. `sources_newer_than` sweeps the project being built,
    // so a NEW FILE appearing in such a tree is invisible to it: ninja cannot
    // report an edge that was never emitted, and the fast path replays a
    // build.ninja that predates the file. Measured before this field existed:
    // `mcpp build` printed `Finished dev in 0.00s` and the module was never
    // compiled.
    //
    // Recorded rather than re-derived, because the authoritative answer is
    // which packages this build ACTUALLY read from source — the fast path
    // cannot resolve dependencies without becoming prepare_build, and a second
    // derivation would drift from the first exactly when a resolution rule
    // changes. Written into `.build_cache`; see BuildCacheEntry::depSourceRoots.
    std::vector<std::filesystem::path> depSourceRoots;
    // `<payload>/bin` and then `<payload>` of every installed `[xlings] deps`
    // payload of the runtime-owner manifest, in declaration order (#544); the
    // pair comes from runner_lookup::payload_search_dirs. Read by
    // choose_runner's lookup (mcpp.build.runner_lookup) so a runner may name
    // a program the project declared without writing the payload's
    // home-and-version path into the manifest. Computed by the same
    // resolution `fillXpkgDirs` uses for build programs; a payload that is
    // declared but not installed contributes nothing, and the lookup then
    // continues to PATH.
    std::vector<std::filesystem::path> xlingsDepBinDirs;
    // The payload directory of every `[xlings]` address resolved above, for
    // the fast path's presence check (#716). See BuildCacheEntry::xlingsPayloads.
    std::vector<std::filesystem::path> xlingsPayloads;
    // True when the graph declared a `when = "run"` tool that THIS invocation
    // did not provision, because it was not going to execute anything. The
    // build cache records it so `mcpp run`'s fast path declines an entry a
    // plain `mcpp build` wrote — see BuildCacheEntry::runTierPending.
    bool runTierPending = false;
    // What `--features` asked for, verbatim. Carried so the build cache entry
    // can record the set its artefacts were built with — the output directory
    // is keyed on a fingerprint that includes the features and the entry was
    // not, which let a plain build serve a featured artefact.
    std::string activeFeatureRequest;
    std::filesystem::path           outputDir;
    std::filesystem::path           stdBmi;
    std::filesystem::path           stdObject;
    // plan_only: what the std module build WOULD be (sources are on the
    // toolchain), set when the graph imports std. A build compiles it instead
    // and leaves this empty.
    std::optional<mcpp::toolchain::StdModuleDescription> stdModule;
    // The packages this build read from an editable source tree: the root, and
    // every package whose root is neither in a store nor in a hash-addressed git
    // checkout (the same test depSourceRoots applies). `sources` are the
    // package's source globs, relative to `root`. Read by the build database for
    // the inputs it lists.
    struct SourcePackage {
        std::string                 name;
        std::filesystem::path       root;
        std::vector<std::string>    sources;
    };
    std::vector<SourcePackage>      sourcePackages;
    std::vector<PlanNote>           planNotes;
    mcpp::build::BuildPlan          plan;
    // The scanned module graph. Only `mcpp pack` reads it — see the note at
    // the assignment for why the plan cannot answer its question.
    mcpp::modgraph::Graph           graph;
    // Resolved profile name (resolve_profile_name). Carried so run_build_plan
    // can record it in .build_cache — without it the fast path cannot tell
    // whether a cached build.ninja was generated for the profile being asked
    // for — and so `Finished <profile>` stops being a hardcoded "release".
    std::string                     profile;
    // WHY THIS COMPILER — carried so the QUERY can answer it too.
    //
    // A build says so on its status line. `mcpp why toolchain --format json`
    // exists precisely to answer "what would this resolve to, and why", and a
    // consumer that had to parse the prose to learn that a dependency chose the
    // compiler would be doing the substring matching the machine interface was
    // introduced to remove.
    struct CompilerChoice {
        std::string origin;      // tc_origin_name(): who decided
        std::string requiredBy;  // the package, when the graph decided
        std::string replaced;    // the spec displaced, when one was
    };
    CompilerChoice                  compilerChoice;
    // Resolved global-cache mode. Read side is honored in prepare_build; write
    // side in run_build_plan.
    CacheMode                       cacheMode = CacheMode::Global;

    // M3.2 BMI cache: deps that did NOT hit cache and therefore need
    // populate_from(...) AFTER backend.build succeeds.
    struct CacheTask {
        mcpp::bmi_cache::CacheKey       key;
        mcpp::bmi_cache::DepArtifacts   artifacts;
    };
    std::vector<CacheTask>          depsToPopulate;

    // Deps that DID hit the global cache, and how many compile units each one
    // spared. run_build_plan reports the count so the "Cached" line cannot be
    // true-looking and empty at the same time.
    struct CachedDep {
        std::string name;
        std::string version;
        std::size_t units = 0;
    };
    std::vector<CachedDep>          cachedDeps;

    // What the dependency walk actually RESOLVED, keyed by the root manifest's
    // dependency map key. The "Compiling <dep> v<version>" banner used to read
    // `manifest.dependencies[...].version` — the constraint as authored — so a
    // caret dep announced itself as `v^1.92.8` (mcpp#363). The resolution result
    // already existed inside prepare_build; the banner and mcpp.lock were simply
    // reading the input instead of the output. Both now read this.
    std::map<std::string, std::string> resolvedVersions;
};

// The ONE cache-mode resolver, for the same reason resolve_profile_name exists:
// execute.cppm's fast paths deliberately skip prepare_build, so they need to
// settle the mode from the same rule. Pure in (manifest, override, environment).
//
// `--cache` on the command line already bypasses the fast path, so the override
// argument is empty there; it is threaded anyway so there is exactly one place
// where precedence is written down.
//
// Precedence: --cache > MCPP_BUILD_CACHE > [build] cache > global. An
// unparseable value falls through to the next source rather than silently
// meaning "global" — see prepare_build, which also reports it.
export CacheMode resolve_cache_mode(const mcpp::manifest::Manifest& m,
                                    std::string_view override_mode) {
    if (auto v = parse_cache_mode(override_mode)) return *v;
    if (const char* e = std::getenv("MCPP_BUILD_CACHE"); e && *e)
        if (auto v = parse_cache_mode(e)) return *v;
    if (auto v = parse_cache_mode(m.buildConfig.cacheMode)) return *v;
    return CacheMode::Global;
}

// The ONE profile-name resolver. Shared with execute.cppm's fast paths:
// they deliberately skip prepare_build, so before this existed they had no
// idea which profile the request meant — and `.build_cache` keyed entries by
// target triple alone. Net effect: `mcpp build --release` followed by a bare
// `mcpp build` reported success in 0.00s and left the RELEASE artifacts in
// place. The rule is pure (manifest + one override string), so both sides can
// evaluate it without resolving a toolchain or scanning the module graph.
//
// Precedence: --profile/--release/--dev > [build].default-profile > `fallback`.
// The global default is "dev" (-O0 -g) per the dominant convention
// (Cargo/Meson/CMake/Zig/Bazel/MSBuild all default to debug).
//
// `fallback` exists for ONE caller: `mcpp pack`, where the artifact leaves this
// machine and an unoptimized build with the publisher's absolute source paths
// in it is never what was meant. It changes the LAST step only, so a manifest
// that states `[build] default-profile` still decides — packaging an artifact
// with different flags than `mcpp build` produces would be its own surprise.
// Adding a parameter here rather than a second resolver keeps the precedence
// rule in one function, which is why this function exists at all.
export std::string resolve_profile_name(const mcpp::manifest::Manifest& m,
                                        std::string_view override_name,
                                        std::string_view fallback = "dev") {
    if (!override_name.empty())                 return std::string(override_name);
    if (!m.buildConfig.defaultProfile.empty())  return m.buildConfig.defaultProfile;
    return fallback.empty() ? std::string("dev") : std::string(fallback);
}

// THE OVERRIDE NAME THE COMMAND LINE STATES, OR "" FOR NONE (#649 E9).
//
// `--profile NAME` > `--release` > `--dev`, and one function for every verb
// that takes the spellings (`build`, `run`, `test`, `emit build-database`,
// `pack`). The rule used to be written twice and the copies disagreed:
// `mcpp build --profile dev --release` built `dev` while `mcpp run` given the
// same line built `release`. Its result is `resolve_profile_name`'s
// `override_name`.
export std::string profile_override_from_flags(std::string_view profileOption,
                                               bool release, bool dev) {
    if (!profileOption.empty()) return std::string(profileOption);
    if (release)                return "release";
    if (dev)                    return "dev";
    return {};
}

// Command-level overrides (--target / --static).
// Empty defaults preserve pre-existing behaviour exactly.
export struct BuildOverrides {
    // Where the package being built LIVES (its mcpp.toml). Empty = walk up from
    // the process cwd, which is what every user-facing invocation does. Set by
    // the tool-provisioning pass, which builds a package that lives in the
    // registry rather than under the cwd.
    std::filesystem::path project_root;
    // Where mcpp WRITES. Empty = the project root, which is the historical
    // (and for a normal build, correct) behaviour.
    //
    // The two are separate because a registry package root is shared across
    // projects and may be read-only — build_program.cppm has said so in a
    // comment since G2, and until now nothing could honour it for anything
    // bigger than build.mcpp's own scratch dir. Splitting "source" from "work"
    // is what lets mcpp build such a package at all.
    //
    // EVERYTHING derived from it moves together: target/, mcpp.lock,
    // compile_commands.json, .mcpp/, and build.mcpp's artifact dir. Moving
    // only some would be worse than moving none — a half-redirected build
    // writes into the shared root anyway, just less visibly.
    std::filesystem::path work_dir;
    // PLANNING TO DESCRIBE, NOT TO BUILD (`mcpp emit build-database`).
    //
    // With `work_dir` pointed outside the project, three things still reached
    // it or ran a compiler, and this switch settles each: the std module is
    // described (mcpp::toolchain::describe_std_module) instead of compiled; the
    // root package's `[build] generated_files`, which are sources and live in
    // the source tree, are compared instead of written, and a missing or stale
    // one is recorded in BuildContext::planNotes; and mcpp.lock is READ from the
    // project root, as the resolution input it is, while the lock this planning
    // produces is written under `work_dir`. Build programs still run.
    bool        plan_only = false;
    // #355 tool provisioning re-enters prepare_build for the tool package. A
    // tool package's own build.mcpp may legitimately want another tool (gRPC's
    // wants protoc), so the depth cannot be 1 — but an unbounded chain is a
    // bug, and hanging is a worse diagnostic than a named cycle.
    int         tool_depth = 0;
    // The request chain, for that diagnostic. "root → grpc:grpc_cpp_plugin → …"
    std::string tool_chain;
    // The (package source, tool) pairs being built by the enclosing sub-builds,
    // outermost first. A request for one of them is the tool's own build asking
    // for itself, refused at its first repetition (#649 E6).
    std::vector<std::string> tool_chain_sources;
    // The toolchain spec a host-tool sub-build uses, decided by the build that
    // requested the tool and recorded in the tool's store key (#710). Beats
    // every other source, `--toolchain` included, because the requesting build
    // already took `--toolchain` into account when it decided. Empty for every
    // user-facing invocation.
    std::string toolchain;
    // Use THIS manifest instead of reading `<project_root>/mcpp.toml`.
    //
    // Required for a `compat`-style registry package (Form B), which ships no
    // mcpp.toml at all — its manifest is synthesized from the `.lua`
    // descriptor during resolution. Without this the tool sub-build could only
    // ever handle packages that carry their own manifest (Form A), which
    // excludes most of the index, protobuf among them.
    //
    // Must be the PRISTINE manifest, before feature activation: the sub-build
    // activates its own feature set, and starting from an already-activated
    // copy would fold the same feature sources in twice.
    // A shared_ptr rather than an optional<Manifest>: BuildOverrides is an
    // EXPORTED struct, and embedding a large value type in the module
    // interface made GCC fail to write the cluster at all
    // ('failed to read compiled module cluster ...: Bad file data' when
    // mcpp.build.execute imported it). A pointer keeps the exported layout
    // trivial, and it also avoids copying the manifest per tool build.
    std::shared_ptr<const mcpp::manifest::Manifest> preloaded_manifest;
    // Nested source/tool builds inherit the consumer root's local development
    // OS. A dependency's own [xlings].subos is never consulted or propagated.
    std::shared_ptr<const mcpp::xlings::runtime::RuntimeSelection>
        inherited_runtime_selection;
    std::shared_ptr<const mcpp::platform::runtime::RuntimeBinding>
        inherited_runtime_binding;
    std::string target_triple;       // empty = host triple, fall through to [toolchain]
    // --accel: the device backends and architectures this build targets, in
    // the wire form mcpp.pack.abi_tag reads. Overrides `[build] accel`, the
    // same relationship --target has with [toolchain].
    std::string accel;
    bool        force_static = false; // --static (or implied by musl target)
    std::string package_filter;      // -p <name>: only build this workspace member
    // --profile <name>. Empty = fall through to `[build] default-profile`, then
    // to `profile_fallback` below, whose own default is "dev". The comment here
    // said "release" for as long as `mcpp build --help` did, and neither had
    // been true since the global default moved (see resolve_profile_name and
    // tests/e2e/87_build_default_profile.sh).
    std::string profile;
    // What `resolve_profile_name` falls back to when neither the command line
    // nor `[build] default-profile` says. Empty = "dev", which is every
    // interactive command. `mcpp pack` sets "release": see resolve_profile_name.
    std::string profile_fallback;
    std::string features;            // --features a,b,c (root package activation)
    bool        strict = false;      // --strict: schema warnings become errors
    std::string capabilities;        // --cap blas=openblas,lapack=mkl (provider pins)
    std::string cache_mode;          // --cache global|local|off ("" = unset)
    // Whether the caller intends to EXECUTE what it builds, which is what
    // decides the `when = "run"` tool tier. `mcpp run` and `mcpp test` set it;
    // `mcpp build`, `mcpp pack` and every internal sub-build do not.
    //
    // A SEPARATE FLAG FROM `includeDevDeps`, THOUGH `mcpp test` SETS BOTH.
    // One says which PACKAGES enter the graph, the other which TOOLS are
    // installed, and `mcpp run` needs the second without the first.
    bool        will_run = false;
    // ── The packaging pass, when this prepare is one (mcpp 2026.9.11.1+) ────
    //
    // `mcpp pack --format <name>` prepares TWICE, and these two fields are the
    // whole difference between the passes. The first sets neither: build
    // programs run, declare the formats they provide, and submit no dist
    // action because none was asked for. The second sets both, after the link
    // and after staging, so the claiming member submits an action whose input
    // is a directory that by then exists.
    //
    // NEITHER VALUE IS DERIVED HERE, AND THAT IS THE POINT. `pack_stage_dir` is
    // a function of the package name, the version, the resolved triple and the
    // mode, and the resolved triple is not known until a prepare has run.
    // Computing it a second time before prepare -- from the host triple, say --
    // is the shape where two derivations of one value agree on every machine
    // the author has. Both are read out of what the first pass and `make_plan`
    // already answered.
    std::string           pack_format;
    std::filesystem::path pack_stage_dir;
    // WHY THERE IS NO STAGED TREE, when there is none and a format was still
    // requested. Empty otherwise.
    //
    // A dispatched format does not require the built-in bundling to have
    // succeeded -- see the note in `mcpp.pack.pipeline`. When it did not, the
    // reason travels here so `${mcpp.stage_dir}`'s refusal can name it instead
    // of saying only that the placeholder is unavailable. A member author
    // reading "this build is not packaging" for a build that plainly is would
    // be sent looking in the wrong place.
    std::string           pack_stage_reason;
    // #649 E5: the strip decision and the debug-symbol directory that pass
    // resolved, "1" or "0" and absolute, set only beside `pack_format`. A
    // member that stages libraries of its own reads them through
    // `mcpp::pack_strip()` and `mcpp::pack_debug_symbols_dir()`, so
    // `--no-strip` reaches its files as it reaches the engine's.
    std::string           pack_strip;
    std::filesystem::path pack_debug_symbols_dir;
};

// ── git dependency helpers ──────────────────────────────────────────────────

// Is this git remote reachable without a network round-trip?
//
// `--offline` means "never touch the network" (docs/04-mcpp-toml.md), and its
// standing promise is that anything already on disk still builds. A remote that
// names a local directory — or a file:// URL — is served by plain filesystem
// reads, so refusing it would break that promise without buying any isolation.
// The dependency-download gate further down draws the same line.
//
// Recognising a scheme (`https://`, `ssh://`, `git://`) or scp-like syntax
// (`git@host:path`) as remote first keeps a Windows drive letter (`C:\repo`,
// which contains a colon but no `@`) on the local side.
bool is_local_git_remote(std::string_view url) {
    if (url.starts_with("file://"))                     return true;
    if (url.contains("://"))                            return false;
    if (url.contains('@') && url.contains(':'))         return false;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(url), ec);
}

// The commit a cached clone is actually parked on, or "" if it cannot be read.
//
// Used to detect a clone that was interrupted between `git clone` and
// `git checkout` — the directory exists and looks like a repository, but sits
// on the wrong commit. Only meaningful when the expected revision is a sha,
// i.e. for branch deps after resolution.
//
// stderr is folded in so a git warning cannot leak to the user's terminal;
// the last line is taken so such a warning cannot corrupt the sha either.
std::string git_cache_head(const std::filesystem::path& gitRoot) {
    auto r = mcpp::platform::process::capture(std::format(
        "git -C {} rev-parse HEAD 2>&1",
        mcpp::platform::shell::quote(gitRoot.string())));
    if (r.exit_code != 0) return {};
    std::string out = r.output;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'
                            || out.back() == ' '  || out.back() == '\t'))
        out.pop_back();
    if (auto nl = out.find_last_of("\r\n"); nl != std::string::npos)
        out.erase(0, nl + 1);
    return out;
}

// `prepare_build` builds the BuildContext for any verb that compiles.
//   includeDevDeps: when true, dev-dependencies are also fetched + scanned
//                   into the modgraph. mcpp test passes true; build/run pass false.
//   extraTargets:   additional Target entries (e.g. synthetic test targets)
//                   appended to the manifest before the modgraph runs.
//   overrides:      --target / --static.
// A dependency that "cannot be found" while an index is unreadable is almost
// never missing — it is unreachable, and the two need different actions from
// the user (publish it vs upgrade mcpp). The floor error is printed when the
// index is first opened, which can be hundreds of lines earlier; the message
// that STOPS the build has to carry the cause, because that is the one a user
// reads. See mcpp::pm::unusable_index_hint.
// Spelling-independent `[target.<triple>]` lookup.
//
// A section keyed `x86_64-w64-mingw32` matches a resolved `x86_64-windows-gnu`,
// and unparseable keys compare exactly (the escape hatch for custom triples).
// Factored out of the toolchain-override path because the sysroot override must
// use the SAME matching: two lookups that disagreed about spelling would give a
// section that applies to `toolchain` and not to `sysroot`, which is a defect
// nobody would think to look for.
const mcpp::manifest::TargetEntry*
find_target_entry(const mcpp::manifest::Manifest& m,
                  const mcpp::toolchain::triple::Triple& t)
{
    if (auto it = m.targetOverrides.find(t.str()); it != m.targetOverrides.end())
        return &it->second;
    for (auto const& [key, entry] : m.targetOverrides) {
        if (auto k = mcpp::toolchain::triple::parse(key); k && k->str() == t.str())
            return &entry;
    }
    return nullptr;
}

// The project's `[target.<triple>].sysroot`, or nullptr when it declared none.
const std::string*
sysroot_override(const mcpp::manifest::Manifest& m,
                 const mcpp::toolchain::triple::Triple& t)
{
    auto* e = find_target_entry(m, t);
    return (e && e->sysrootDeclared) ? &e->sysroot : nullptr;
}

// THE MSVC TOOLSET A CLANG `*-windows-msvc` BUILD COMPILES AGAINST.
//
// On an MSVC-ABI row the compiler is the toolchain and the MSVC toolset -- its
// STL and CRT, and the Windows SDK that follows it -- is the sysroot, named by
// `[target.<triple>].sysroot` (default `msvc@system`). Until this existed the
// clang driver searched the machine for headers and libraries while mcpp
// searched it again for `std.ixx`, by a different order, so a machine with two
// installations could compile one toolset's `std.ixx` against another's
// headers, and the choice reached neither the cache key nor any report. The
// choice is made here once, recorded on the toolchain, and handed to the
// driver by the link model.
//
// `msvc@system` that finds nothing returns without binding, so the build
// reaches the "targeting the MSVC ABI without a usable MSVC" diagnosis that
// already names the alternatives.
std::expected<void, std::string>
bind_msvc_sysroot(mcpp::toolchain::Toolchain& tc,
                  const mcpp::manifest::Manifest& m,
                  const std::function<std::expected<mcpp::config::GlobalConfig*,
                                                    std::string>()>& cfgOf)
{
    namespace msvc = mcpp::toolchain::msvc;
    auto tt = mcpp::toolchain::triple::parse(tc.targetTriple);
    if (!tt) return {};
    const std::string* declared = sysroot_override(m, *tt);
    const std::string text = declared ? *declared : std::string("msvc@system");
    auto spec = mcpp::toolchain::parse_toolchain_spec(text);
    if (!spec)
        return std::unexpected(std::format(
            "[target.{}].sysroot = '{}': {}", tt->str(), text, spec.error()));
    if (spec->family != mcpp::toolchain::Family::Msvc)
        return std::unexpected(std::format(
            "[target.{}].sysroot = '{}': on an MSVC-ABI row the sysroot is an "
            "MSVC toolset (msvc@system, msvc@<toolset> or xim:msvc@<toolset>)",
            tt->str(), text));

    msvc::ToolsetNeeds needs;
    needs.cl = false;   // clang compiles against the toolset; it does not run cl.exe
    const bool systemSel = spec->version.empty() || spec->version == "system";
    std::vector<msvc::VsInstance> instances;
    std::optional<msvc::ToolsetChoice> choice;
    if (!spec->ecosystemOnly) {
        instances = msvc::enumerate_vs_instances();
        choice = msvc::select_system_toolset(
            instances, msvc::msvc_env_snapshot(),
            systemSel ? std::string_view("system") : std::string_view(spec->version),
            needs);
        if (!choice && systemSel) return {};
    }

    std::string origin = "system";
    if (!choice) {
        // THE PACKAGE: `xim:` asked for it, or no installed toolset matched.
        auto cfg = cfgOf();
        if (!cfg) return std::unexpected(cfg.error());
        mcpp::toolchain::ToolchainSpec pkgSpec = *spec;
        pkgSpec.target = {};
        auto pkg = mcpp::toolchain::to_xim_package(pkgSpec);
        mcpp::fetcher::Fetcher fetcher(**cfg);
        mcpp::fetcher::InstallProgressHandler progress;
        auto payload = fetcher.resolve_xpkg_path(pkg.target(), /*autoInstall=*/true,
                                                 &progress);
        if (!payload) {
            // `xim:` never looked at the machine, so the refusal does not
            // report on it.
            if (spec->ecosystemOnly)
                return std::unexpected(std::format(
                    "[target.{}].sysroot = '{}': the package could not be "
                    "provided: {}\n"
                    "  packages: `mcpp toolchain list --available msvc`",
                    tt->str(), text, payload.error().message));
            std::string onMachine;
            for (auto const& line : msvc::describe_system_toolsets(instances, needs))
                onMachine += "\n    " + line;
            return std::unexpected(std::format(
                "[target.{}].sysroot = '{}' matches no toolset on this machine, "
                "and the package could not be provided: {}\n"
                "  installed on this machine:{}\n"
                "  packages: `mcpp toolchain list --available msvc`",
                tt->str(), text, payload.error().message,
                onMachine.empty() ? std::string(" none") : onMachine));
        }
        auto inst = mcpp::toolchain::resolve_managed_msvc(
            mcpp::config::make_xlings_env(**cfg), pkg, /*identifyVersion=*/false);
        if (!inst) return std::unexpected(inst.error());
        choice.emplace();
        choice->vsRoot   = inst->vsRoot;
        choice->version  = inst->toolsVersion;
        choice->toolsDir = inst->vsRoot / "VC" / "Tools" / "MSVC" / inst->toolsVersion;
        choice->product  = "xim:msvc@" + inst->toolsVersion;
        choice->via      = "package";
        origin = "managed";
    }
    for (auto const& n : choice->notes) mcpp::ui::info("note", n);

    // THE SDK FOLLOWS THE TOOLSET'S ORIGIN: a package binds the windows-sdk
    // installed with it, a machine's toolset takes the machine's SDK by the
    // search `msvc@system` has always used. The same function the cl.exe row
    // uses, asked about the toolset directory rather than a cl.exe.
    auto sdk = msvc::resolve_sdk_for(choice->toolsDir / "bin");
    if (!sdk.note.empty()) mcpp::ui::info("note", sdk.note);

    tc.msvcToolsDir     = choice->toolsDir;
    tc.msvcToolsVersion = choice->version;
    tc.msvcOrigin       = origin;
    tc.msvcProduct      = choice->product;
    if (sdk.sdk) {
        tc.windowsSdkRoot    = sdk.sdk->root;
        tc.windowsSdkVersion = sdk.sdk->version;
    }
    // The STL is this toolset's, so its version is the standard library's.
    tc.stdlibVersion = choice->version;

    // THE STD MODULE OF THIS TOOLSET, replacing the `std.ixx` detection found
    // by its own search. A toolset without one leaves `import std` unavailable
    // rather than borrowing another toolset's.
    //
    // Only `std` is rebound. Detection never gave this row a `std.compat`
    // source, and the clang builder for it passes the file without
    // `-x c++-module`: given `std.compat.ixx`, clang takes it for linker
    // input, `--precompile` writes nothing and exits 0, and the next command
    // fails on the missing BMI (measured on the Windows runners).
    std::error_code ec;
    const auto ixx = choice->toolsDir / "modules" / "std.ixx";
    const bool msvcStl = tc.stdModuleSource.empty()
                      || tc.stdModuleSource.filename() == "std.ixx";
    if (msvcStl && std::filesystem::exists(ixx, ec)) {
        tc.stdModuleSource   = ixx;
        tc.hasImportStd      = true;
        tc.importStdMinLevel = msvc::std_module_min_level_for_stl(ixx);
    } else if (msvcStl && !tc.stdModuleSource.empty()) {
        tc.stdModuleSource.clear();
        tc.hasImportStd = false;
    }

    mcpp::ui::info("Resolved", std::format(
        "sysroot {} → MSVC {} ({}: {}){}", spec->spec_str(), choice->version,
        origin, choice->product,
        tc.windowsSdkVersion.empty()
            ? std::string{}
            : std::format(" · Windows SDK {}", tc.windowsSdkVersion)));
    return {};
}

// ON THE CL.EXE ROW THE COMPILER IS ITS OWN SYSROOT: cl.exe cannot compile
// against another toolset's STL. A declared sysroot is therefore a second
// statement of the compiler's toolset, and one that names a different
// toolset is refused rather than silently ignored.
std::expected<void, std::string>
check_cl_row_sysroot(const mcpp::toolchain::Toolchain& tc,
                     const mcpp::manifest::Manifest& m)
{
    auto tt = mcpp::toolchain::triple::parse(tc.targetTriple);
    if (!tt) return {};
    const std::string* declared = sysroot_override(m, *tt);
    if (!declared) return {};
    auto spec = mcpp::toolchain::parse_toolchain_spec(*declared);
    if (!spec || spec->version.empty() || spec->version == "system") return {};
    // <tools>/bin/Host<h>/<t>/cl.exe → <tools> is named by the toolset.
    const auto toolset = tc.binaryPath.parent_path().parent_path()
                             .parent_path().parent_path().filename().string();
    if (mcpp::toolchain::msvc::toolset_version_matches(spec->version, toolset))
        return {};
    return std::unexpected(std::format(
        "[target.{}].sysroot = '{}' names a different toolset than the "
        "compiler ({}, toolset {}). With cl.exe the compiler is its own "
        "sysroot: pin the toolset in the toolchain (`msvc@<toolset>`) and "
        "drop `sysroot`, or build with clang to compile against another "
        "toolset.",
        tt->str(), *declared, tc.binaryPath.string(), toolset));
}

// The target-facing answers a `build.mcpp` may ask the engine for.
//
// ONE function because there are TWO call sites — the root project and each
// dependency — and four values derived independently in two places is the
// shape this codebase keeps paying for. A board package that got the right
// answer as a root project and a stale one as a dependency would fail only in
// the consuming build, which is the harder direction to debug.
// A NETWORK STEP OF A BUILD, RETRIED — AND IT HAD NO RETRY AT ALL.
//
// A dependency resolved by `git` is fetched on every machine that has not
// cached it, and a transport that hiccups once failed the whole build:
//
//     error: git clone of 'https://github.com/…' failed:
//     Cloning into '/home/runner/.mcpp/git/63269d80b47f71e6'...
//
// — no message from git, which is what a connection that dies mid-transfer
// looks like. Measured twice on 2026-08-23: once in continuous integration and
// once locally as `TLS connect error: … unexpected eof while reading`.
//
// THREE ATTEMPTS, AND THE LAST FAILURE IS REPORTED UNCHANGED. A wrong URL
// and a missing branch fail exactly as a transient fault does, so this cannot
// tell them apart and does not try: a permanent failure costs three seconds and
// produces the message it always did. Hiding a real error behind a retry is the
// worse trade, which is why the count is small and the report is untouched.
//
// BOTH NETWORK STEPS, not one. The first version retried only the clone —
// and a probe with a nonexistent repository failed in ONE second, because the
// step that runs first is `git ls-remote` and it was still bare. A retry on
// half of a path is a retry that reports success at having been added.
//
// `between` runs after a failed attempt: the clone needs the partial directory
// removed, or git's next attempt fails with "already exists and is not an empty
// directory" — a second, different error that says nothing about the first.
mcpp::platform::process::RunResult run_with_network_retry(
        std::string_view command,
        const std::function<void()>& between = {}) {
    mcpp::platform::process::RunResult r{};
    mcpp::platform::env::note_network_access();   // the envelope's `effects` (#648 A4)
    for (int attempt = 1; attempt <= 3; ++attempt) {
        r = mcpp::platform::process::capture(command);
        if (r.exit_code == 0) return r;
        if (between) between();
        if (attempt < 3)
            std::this_thread::sleep_for(std::chrono::seconds(attempt));
    }
    return r;
}

// `[package]`, for the build program of the package that declares it.
//
// ONE CALL RATHER THAN A FIELD PER SITE. Two places build a
// `BuildProgramEnv` -- the dependency loop and the root -- and the values a
// build program is told about its own package are the same question in both.
// Setting them field by field at each site is how the two answers drift: the
// root gained `packageName` and the dependency loop gained it separately, and
// a value added to only one of them is a rule package that works for a root
// project and not for a dependency, with nothing failing to say so.
// Forward-declared: defined below (#622 A11), and `fill_target_build_env`
// needs it before that point in the file.
std::string min_platform_version(const mcpp::manifest::Manifest& m,
                                 const mcpp::toolchain::triple::Triple& t,
                                 const std::filesystem::path& compilerPath);

void fill_package_build_env(mcpp::build::BuildProgramEnv& e,
                            const mcpp::manifest::Manifest& m)
{
    e.packageName        = m.package.name;
    e.packageNamespace   = m.package.namespace_;
    e.packageVersion     = m.package.version;
    e.packageDescription = m.package.description;
    e.packageLicense     = m.package.license;
    e.packageRepo        = m.package.repo;
    // ';' rather than ',': an author entry is conventionally `Name <mail@host>`
    // and a name may carry a comma, so a comma-joined list cannot be split back
    // into the entries it was made from.
    e.packageAuthors.clear();
    for (auto const& a : m.package.authors) {
        if (!e.packageAuthors.empty()) e.packageAuthors += ';';
        e.packageAuthors += a;
    }
}

void fill_target_build_env(mcpp::build::BuildProgramEnv& e,
                           const mcpp::manifest::Manifest& m,
                           const mcpp::toolchain::Toolchain* tc,
                           const mcpp::config::GlobalConfig* cfg)
{
    // The registry SubOS is where payloads are installed, whichever
    // toolchain or link mode resolved, so this is set before the toolchain
    // gate below.
    if (cfg) {
        const auto view = mcpp::xlings::paths::sysroot(mcpp::config::make_xlings_env(*cfg));
        e.pkgConfigLibdir = (view / "usr" / "lib" / "pkgconfig").generic_string()
            + mcpp::platform::env::path_list_separator()
            + (view / "usr" / "share" / "pkgconfig").generic_string();
    }
    e.toolchainDir  = (tc && !tc->binaryPath.empty())
        ? tc->binaryPath.parent_path().parent_path().string() : std::string{};
    e.targetSysroot = tc ? tc->targetSysrootRoot.string() : std::string{};
    e.compilerId    = !tc ? std::string{}
        : tc->compiler == mcpp::toolchain::CompilerId::GCC   ? "gcc"
        : tc->compiler == mcpp::toolchain::CompilerId::Clang ? "clang"
        : tc->compiler == mcpp::toolchain::CompilerId::MSVC  ? "msvc"
        : std::string{};
    e.targetLibc    = tc ? tc->targetSysrootPkg : std::string{};
    // Read from the toolchain the engine resolved, the same field the cache
    // key, the ABI tag and the toolchain fingerprint read. Not re-derived from
    // `compilerId`: clang answers "libc++" or "libstdc++" depending on how the
    // payload was configured, and deriving it here would restate an assumption
    // the resolver already measured.
    e.cxxStdlib     = tc ? tc->stdlibId : std::string{};
    if (!tc) return;

    // The two flags mcpp passes to ITS OWN compiler, so a rule package driving
    // a second compiler passes the same two. Both read from the single
    // producer that already decides them for the engine's own command lines —
    // `resolve_link_model` for the sysroot, `gcc::binutils_prefix_dir` for the
    // `-B` — rather than a fifth re-derivation of either.
    if (auto lm = mcpp::toolchain::resolve_link_model(*tc);
        lm.mode == mcpp::toolchain::CLibMode::Sysroot)
        e.toolchainSysroot = lm.sysroot.string();
    e.toolchainBinutilsDir = mcpp::toolchain::gcc::binutils_prefix_dir(*tc).string();

    // The C LIBRARY's sub-directory for this ISA profile, from the freestanding
    // table — the same single read point the compile flags use.
    //
    // Gated on there being a C library at all, and the gate is the point: the
    // value is a multilib convention, so on the zero-libc tier there is nothing
    // for it to be a convention OF. Emitting `rv64gc/lp64d` there would hand a
    // kernel a path into a directory that does not exist, and the name of the
    // accessor would be a lie. All three libc-facing answers are empty together.
    if (!e.targetSysroot.empty())
        if (auto spec = mcpp::freestanding::resolve(tc->targetTriple))
            e.targetLibcProfile = std::string(spec->libdir);

    // Which builtins library the RESOLVED toolchain ships. Freestanding only:
    // on a hosted target the driver links them without being asked, and
    // handing a package a name it must not use would invite it to.
    if (auto t = mcpp::toolchain::triple::parse(tc->targetTriple);
        t && t->is_freestanding()) {
        e.targetBuiltinsLib = mcpp::toolchain::is_clang(*tc)
            ? "clang_rt.builtins-" + t->arch
            : std::string("gcc");
    }

    // #622 A11: MCPP_TARGET_MIN_PLATFORM_VERSION. One call, so a new consumer
    // (`dist-apple`, `dist-apk`) reads the same answer the compiler flag and
    // the fingerprint slot already resolved, rather than restating it.
    if (auto tt = mcpp::toolchain::triple::parse(tc->targetTriple))
        e.minPlatformVersion = min_platform_version(m, *tt, tc->binaryPath);
}

// ── Tool tiers: which of a manifest's declared packages this verb needs ─────
//
// THE AXIS PACKAGE DEPENDENCIES HAVE HAD SINCE THE BEGINNING, AND TOOLS
// DID NOT.
//
// A board-support package names an emulator (needed to run) and a debug probe
// (needed to reach real hardware). Before this, declaring either installed
// both, for everyone, on every `mcpp build` — including a consumer who only
// wanted the library to compile. Packages have `[dependencies]`,
// `[build-dependencies]` and `[dev-dependencies]`; tools had one list.
//
// The tier is written on the ENTRY (`when = "run"`), not as a second table:
// `[xlings.workspace]` was made the one table on purpose, and the entry-level
// spelling is the one `[dependencies]` already uses for the same kind of
// refinement.
//
// `Dev` IS THE ONLY TIER THAT DOES NOT PROPAGATE. It means "when the package
// that declared it is itself being developed", so `isRoot` decides it. Every
// other tier reaches a consumer, which is the whole point of a board package
// knowing its own machine.
// Exported (unlike most of this file's helpers) because PrepareState, in the
// implementation partition `:state`, holds one and needs the type visible
// through `import mcpp.build.prepare;` -- a partition sees only what its
// imports export, module-linkage is not enough across that boundary.
export enum class ToolPurpose { Build, Run };

std::vector<std::string>
applicable_xlings_addresses(const mcpp::manifest::Manifest& man,
                            const std::vector<std::string>& activeFeatures,
                            ToolPurpose purpose, bool isRoot) {
    using W = mcpp::manifest::ToolWhen;
    std::vector<std::string> out;
    auto wanted = [&](const std::string& address) {
        switch (man.xlings.when_of(address)) {
            case W::Always: return true;
            case W::Build:  return true;
            case W::Run:    return purpose == ToolPurpose::Run;
            case W::Dev:    return isRoot;
        }
        return true;
    };
    auto add = [&](const std::string& address) {
        if (!wanted(address)) return;
        if (std::ranges::find(out, address) == out.end()) out.push_back(address);
    };
    for (auto const& a : man.xlings.deps) add(a);
    // `[feature-xlings.<f>]` contributes only while `<f>` is active. A consumer
    // that never asks for `hardware` never downloads a probe driver — which is
    // the same mechanism `[feature-deps]` gives a package, applied to tools.
    for (auto const& f : activeFeatures)
        if (auto it = man.xlings.featureDeps.find(f);
            it != man.xlings.featureDeps.end())
            for (auto const& a : it->second) add(a);
    return out;
}

// Install a set of `[xlings.workspace]` addresses, and record that the list was
// done.
//
// EXTRACTED SO THE DEPENDENCY GRAPH CAN USE THE SAME PATH. This was the
// root project's provisioning, inline and reachable only from there. A
// board-support package that declares the emulator its machine needs is
// precisely the thing that should say so once, and a consumer that has to
// repeat the declaration to get it installed is the duplication such a package
// exists to remove — so the graph pass calls this with what the dependencies
// declared, under the same stamp discipline and the same auto-install gate.
//
// `label` names the caller in every message, because "which of the two passes
// is this" is the first thing a reader of the failure needs.
std::expected<void, std::string>
provision_xlings_addresses(const mcpp::config::GlobalConfig& cfg,
                           const std::vector<std::string>& declaredDeps,
                           const std::filesystem::path& legacyStampRoot,
                           std::string_view label) {
    if (declaredDeps.empty()) return {};
            // THE STAMP RECORDS A GLOBAL EFFECT, SO IT LIVES WHERE THE
            // EFFECT DOES. It used to sit in `<project>/.mcpp/`, while the
            // installation goes to the registry a few lines below — the
            // scope difference is deliberate and explained there. Two
            // consequences followed from the mismatch: wiping or replacing
            // `MCPP_HOME` left a project still claiming the packages were
            // installed, and `mcpp clean` (which removes `target/` and
            // never `.mcpp/`) could not clear it. Keyed by the LIST, not by
            // the project, because the installation is shared: two projects
            // declaring the same packages should pay for it once.
            //
            // A STAMP IS NOT A PRESENCE CHECK (#716). It records that the
            // list was installed once; a payload removed since -- `xlings
            // remove`, a pruned cache, a deleted directory -- left the stamp
            // claiming it, the build skipped provisioning and succeeded with
            // `xpkg_dir` answering "". So the stamp counts only while every
            // address still resolves to a payload, answered by the same lookup
            // `xpkg_dir` uses. That costs one record read or directory scan per
            // address, and it was blocked until the lookup could answer an
            // unpinned or two-segment address the way xlings resolved it.
            const auto stampDir = mcpp::home::root() / "provisioned";
            // `std::uint64_t`, not `std::size_t`: the offset basis below is
            // a 64-bit constant and a 32-bit host would truncate it, giving
            // that host a different key space for no reason anyone could
            // see. A collision is not a correctness problem either way —
            // the file stores the LIST and the comparison below is against
            // its content, so two lists sharing a key re-provision rather
            // than silently adopt each other's record.
            auto stamp_key = [&] {
                std::uint64_t h = 1469598103934665603ull;   // FNV-1a
                for (auto const& d : declaredDeps)
                    for (unsigned char ch : d + "\n")
                        { h ^= ch; h *= 1099511628211ull; }
                return std::format("xlings-deps-{:016x}", h);
            };
            const auto stamp = stampDir / stamp_key();
            // Idempotence by CONTENT, not by existence: editing the list
            // has to re-provision, and an unchanged list must not pay for
            // an xlings round-trip on every build.
            auto join_deps = [&](std::string_view sep) {
                std::string out;
                for (auto const& d : declaredDeps) {
                    if (!out.empty()) out += sep;
                    out += d;
                }
                return out;
            };
            std::string want;
            for (auto const& d : declaredDeps) { want += d; want += '\n'; }
            std::string have;
            if (std::ifstream in{stamp}; in)
                have.assign(std::istreambuf_iterator<char>(in), {});
            // The stamp the previous location left behind. Read, never
            // deleted: an older mcpp sharing the checkout still uses it,
            // and a stale extra file is cheaper than a downgrade that
            // re-provisions on every build.
            //
            // IT DOES NOT MEAN "PROVISIONED SUCCESSFULLY". The release
            // that wrote it did not read the result — that is the defect
            // above — so it means only "this list was attempted". Treating
            // it as proof would carry the bug across the very upgrade that
            // fixes it: a project whose dependency never installed would
            // adopt the stamp and stay silently broken.
            //
            // So it is consulted in ONE place, below, where the alternative
            // is worse: the auto-install gate. Online, nothing is adopted
            // and every project re-provisions once, which is a cheap round
            // trip that re-validates the claim.
            const auto legacyStamp = legacyStampRoot / ".mcpp" / ".xlings-deps.stamp";
            auto legacy_stamp_matches = [&] {
                std::string legacy;
                if (std::ifstream in{legacyStamp}; in)
                    legacy.assign(std::istreambuf_iterator<char>(in), {});
                return legacy == want;
            };
            const auto xlEnv = mcpp::config::make_xlings_env(cfg);
            std::vector<std::string> missing;
            if (have == want)
                for (auto const& d : declaredDeps)
                    if (!mcpp::xlings::paths::xpkg_payload(
                            xlEnv, mcpp::xlings::paths::parse_xpkg_ref(d)))
                        missing.push_back(d);
            if (!missing.empty()) {
                std::string list;
                for (auto const& m : missing) list += (list.empty() ? "" : ", ") + m;
                mcpp::log::verbose("xlings", std::format(
                    "{}: recorded as provisioned in {}, but no payload is "
                    "installed for: {}", label, stamp.string(), list));
                if (mcpp::platform::env::offline_mode()
                    || mcpp::platform::env::no_auto_install()) {
                    std::string_view release =
                        mcpp::platform::env::offline_mode()
                        ? "drop --offline / unset MCPP_OFFLINE"
                        : "unset MCPP_NO_AUTO_INSTALL";
                    refusal::record(refusal::Code::OfflineDownloadRequired);
                    return std::unexpected(std::format(
                        "{} are recorded as provisioned, but these payloads "
                        "are not installed: {}\n"
                        "       record: {}\n"
                        "       install them yourself with:\n"
                        "         xlings install {}\n"
                        "       or {} to let mcpp do it.",
                        label, list, stamp.string(), join_deps(" "), release));
                }
            }
            bool needProvision = (have != want) || !missing.empty();
            if (needProvision && missing.empty()) {
                // THE AUTO-INSTALL GATE, WHICH THIS PATH DID NOT HAVE.
                //
                // `[toolchain]` is the precedent this whole mechanism cites
                // ("the same 'declare it and mcpp provisions it on first
                // use' contract"), and that path refuses on either knob and
                // names the one that fired — see the auto-install branch
                // above. This one honoured neither, so a CI exporting
                // MCPP_NO_AUTO_INSTALL specifically to prevent an unasked
                // download got one anyway, from a path that had never heard
                // of the variable.
                //
                // Placed inside `have != want`, so it gates the ATTEMPT and
                // not the block: a project whose packages are already
                // provisioned still builds offline, which is the behaviour
                // that would otherwise regress.
                if (mcpp::platform::env::offline_mode()
                    || mcpp::platform::env::no_auto_install()) {
                    // THE ONE PLACE THE LEGACY STAMP IS TRUSTED, and the
                    // reason is that relocating a record must not refuse a
                    // build that worked yesterday. Every project that had
                    // already provisioned carries the old stamp and no new
                    // one, so on the first build after upgrading it reads
                    // as un-provisioned — and here, with the network shut
                    // off, there is no way to find out otherwise. Refusing
                    // would be a regression caused entirely by moving a
                    // file, which is the least defensible kind.
                    //
                    // Proceeding is the pre-upgrade behaviour exactly: if
                    // the packages really are missing, the build fails
                    // downstream on a missing header, as it did before.
                    // The registry stamp is NOT written — nothing here
                    // verified anything.
                    if (!legacy_stamp_matches()) {
                        std::string_view release =
                            mcpp::platform::env::offline_mode()
                            ? "drop --offline / unset MCPP_OFFLINE"
                            : "unset MCPP_NO_AUTO_INSTALL";
                        refusal::record(refusal::Code::OfflineDownloadRequired);
                        return std::unexpected(std::format(
                            "{} are declared but not provisioned, "
                            "and auto-install is off.\n"
                            "       declared: {}\n"
                            "       install them yourself with:\n"
                            "         xlings install {}\n"
                            "       or {} to let mcpp do it.",
                            label, join_deps(", "), join_deps(" "), release));
                    }
                    mcpp::log::verbose("xlings",
                        std::format("{}: auto-install is off and this project "
                        "carries a pre-2026.9.1.1 provisioning stamp for the "
                        "same list; proceeding without re-checking", label));
                    // Deliberately NOT writing the registry stamp: nothing
                    // here verified anything, and a record of a check that
                    // did not happen is the defect this release removes.
                    needProvision = false;
                }
            }
            if (needProvision) {
                mcpp::ui::status("Provisioning",
                    std::format("{} ({})", label, join_deps(", ")));
                // An address whose index a `[index.repos.<name>]` table
                // redirects is installed from that source, and says so: an
                // installation from a branch checkout must not read as one
                // from the published index (#634, C4).
                std::set<std::string> redirected;
                for (auto const& d : declaredDeps) {
                    const auto colon = d.find(':');
                    if (colon == std::string::npos) continue;
                    const auto index = d.substr(0, colon);
                    for (auto const& r : cfg.indexRepos) {
                        if (!r.fromConfig || r.name != index) continue;
                        if (r.name == "mcpplibs" && r.url == mcpp::config::kMcpplibsIndexUrl)
                            continue;
                        if (redirected.insert(index).second)
                            mcpp::ui::status("Index", std::format(
                                "{} -> {} ([index.repos.{}] in config.toml)",
                                r.name, r.url, r.name));
                    }
                }
                // GLOBAL scope, and the scope is the whole point.
                //
                // The obvious alternative -- `install_packages` against
                // `make_project_xlings_env` -- installs at PROJECT scope,
                // and that measurably does not work: on a fresh MCPP_HOME
                // the headers land in
                // `<proj>/.mcpp/.xlings/subos/_/usr/include` while
                // `--sysroot` names `<MCPP_HOME>/registry/subos/default`,
                // so `#include <gbm.h>` still failed with the dependency
                // installed and declared. Two SubOS views, and the payload
                // in the one the compiler does not read.
                //
                // `make_xlings_env` is the GLOBAL env, so this lands in the
                // registry whose SubOS *is* mcpp's sysroot -- the same
                // place `[toolchain]` has always installed into. A project
                // dependency and a toolchain dependency now agree on where
                // they live, which is the only arrangement in which one
                // `--sysroot` can see both.
                //
                // `install_packages` rather than `resolve_xpkg_path`: the
                // latter requires `<name>@<version>` and rejects a bare
                // `mesa`, while a manifest is entitled to name a package
                // without pinning it. install_packages resolves the version
                // itself and reports an ambiguous name with its candidates,
                // which is the error the author can act on.
                // Built with the JSON library rather than by formatting
                // the strings in. `deps` is manifest input, so a name
                // containing a quote or a backslash would otherwise emit
                // malformed JSON and the failure would surface as an
                // unrelated xlings parse error naming neither the manifest
                // nor the key.
                nlohmann::json args;
                args["targets"] = declaredDeps;
                args["yes"]     = true;

                mcpp::fetcher::InstallProgressHandler progress;
                auto r = mcpp::xlings::call(
                    xlEnv, "install_packages", args.dump(), &progress);
                // `if (!r)` IS NOT THE FAILURE TEST, AND TESTING ONLY
                // IT MADE THIS PATH REPORT SUCCESS FOR EVERY FAILURE XLINGS
                // CAN REPORT.
                //
                // `xlings::call` returns `expected<CallResult, string>` and
                // is in the VALUE state whenever the child ran at all — the
                // error channel means "the call did not happen". A
                // capability's own status arrives inside `CallResult`,
                // parsed off the NDJSON `{"kind":"result","exitCode":N}`
                // line, because the xlings process itself exits 0 by design
                // once it has spoken the protocol.
                //
                // Measured before this fix: a manifest declaring a package
                // that cannot exist printed `Provisioning [xlings] deps
                // (…)`, xlings answered `E_NOT_FOUND` with `exitCode: 1`,
                // and mcpp stamped it as done and reported a successful
                // build. #531 was written because "the declaration looked
                // accepted and did nothing" is the worst shape a config key
                // can have; unread, its own fix reproduced that shape and
                // the stamp made it permanent.
                //
                // The correct idiom is not new — the dependency install
                // path in this same file reads `r->exitCode` — it was
                // simply not applied here.
                const bool called   = r.has_value();
                const int  childRc  = called ? r->exitCode : -1;
                if (!called || childRc != 0) {
                    // Prefer xlings' own message: for an unresolvable name
                    // it names the repos it searched and whether the index
                    // is current, which is the part the author can act on.
                    std::string why = !called ? r.error()
                        : (r->error ? r->error->message
                                    : std::format("xlings exited {}", childRc));
                    if (auto captured = progress.captured_error();
                        !captured.empty() && called && !r->error)
                        why = captured;
                    // The hint is where "run `xlings update` if the package
                    // was just published" lives, and for the commonest
                    // failure — a name that is not in the synced index —
                    // it is the whole of the actionable content.
                    if (called && r->error && !r->error->hint.empty())
                        why += "\n       " + r->error->hint;
                    // Shaped like the toolchain failure: say what failed and
                    // hand back a command the user can run themselves. An
                    // ambiguous bare name ("mesa" matching two repos) lands
                    // here, and xlings' own message names the candidates.
                    return std::unexpected(std::format(
                        "provisioning {} failed: {}\n"
                        "       you can install them manually with:\n"
                        "         xlings install {}",
                        label, why, join_deps(" ")));
                }
                // What xlings resolved each address to, where it says so
                // (protocol 1.1). Recorded per address so every later lookup
                // -- the root's, a member's, a dependency's build program --
                // gets xlings' answer rather than a re-derivation of it.
                for (auto const& e : r->dataEvents)
                    if (e.dataKind == "install_targets")
                        mcpp::xlings::paths::record_resolutions(xlEnv,
                            mcpp::xlings::paths::parse_install_targets(e.payloadJson));
                // Written only on success, for the same reason the check
                // above exists: a stamp is a record that the effect
                // happened, and recording an effect that did not is worse
                // than not recording it — the next build skips the attempt.
                std::error_code sec;
                std::filesystem::create_directories(stamp.parent_path(), sec);
                if (std::ofstream out{stamp}; out) out << want;
            }
    return {};
}

// THE PROJECT'S MINIMUM PLATFORM VERSION FOR THIS TARGET, in one place.
//
// Two platforms fuse it into the effective triple and each names it in its own
// words: macOS's deployment target lives in `[build]` because it applies to
// every Apple artefact a project produces, and Android's API level lives in
// `[target.<triple>]` because it applies to one row. `llvm_triple` takes one
// parameter for both, so the choice between them is made here rather than at
// each of its call sites -- there are two, and a decision made twice is the
// shape this codebase records most often.
std::string min_platform_version(const mcpp::manifest::Manifest& m,
                                 const mcpp::toolchain::triple::Triple& t,
                                 const std::filesystem::path& compilerPath) {
    if (t.is_android()) {
        if (auto it = m.targetOverrides.find(t.str()); it != m.targetOverrides.end())
            if (it->second.minApiLevel > 0)
                return std::to_string(it->second.minApiLevel);
        // AND THERE IS NO SUCH THING AS LEAVING IT OUT. This returned an empty
        // string with the comment "the NDK's own default, which clang
        // supplies", which was never verified and is false. Measured:
        //
        //     --target=aarch64-unknown-linux-android   (no level)
        //     sys/cdefs.h:365:2: error: Unversioned target triples are not
        //       supported!
        //
        // bionic refuses it, so the level is mandatory and a project that
        // never heard of API levels still needs one. The NDK declares the
        // floor it supports in `meta/platforms.json` and that is the honest
        // default -- the payload's own answer, which moves when the payload
        // does. macOS is the same shape and already works this way: its
        // default comes from the platform module, not from the manifest.
        // THE PAYLOAD'S OWN ANSWER FIRST, AND THE ENGINE'S DERIVATION AS
        // THE FALLBACK. `platform_floor` in `.mcpp-toolchain.json` is the
        // same number by a channel that does not require this engine to know
        // that an NDK keeps it in `meta/platforms.json`, nor that file's
        // schema. A payload shipping no descriptor still resolves, which is
        // what makes the descriptor additive.
        //
        // A MALFORMED descriptor is read as absence HERE ONLY, because this
        // function has no error channel and does not need one: a
        // payload-provided compiler reaches this point through
        // `payload_frontend`, which refuses a malformed descriptor by name
        // before any of these decisions are made.
        if (auto desc =
                mcpp::toolchain::payload_descriptor_for_compiler(compilerPath);
            desc && *desc && !(*desc)->platformFloor.empty())
            return (*desc)->platformFloor;
        if (auto level = mcpp::toolchain::ndk_min_api_level(compilerPath);
            level > 0)
            return std::to_string(level);
        return {};   // the caller refuses; see android_api_level_refusal
    }
    // APPLE'S TWO PLATFORMS ANSWER FROM TWO KEYS, ONE SLOT.
    //
    // "14.0" is a macOS version and means nothing to an iOS SDK, so the
    // project states them separately -- and only one of them can apply to any
    // one target, which is why they still share this function's single return
    // and the single fingerprint slot behind it.
    //
    // Empty is a legal answer here and not a refusal, unlike Android's, and
    // for the iOS rows prepare fills it with the located SDK's version before
    // this is read: an unversioned `arm64-apple-ios` made clang refuse
    // thread-local storage (measured, Xcode 16.4), so the driver's own
    // default is not the SDK's. Bionic rejects the unversioned triple
    // outright, which is the other half of the asymmetry.
    if (t.is_ios()) return m.buildConfig.iosDeploymentTarget;
    // AND ONLY FOR A macOS TARGET. `deployment_target` itself no longer
    // consults the host (#685); the discriminator is `t.os`, which is this
    // function's own target parameter and is available regardless of what
    // machine mcpp runs on. A non-Apple target (Linux, Windows, wasm,
    // freestanding) answers empty here, same as it always has.
    if (t.os == "macos")
        return mcpp::platform::macos::deployment_target(
            /*targetIsMacos=*/true, m.buildConfig.macosDeploymentTarget);
    return {};
}

std::string with_index_cause(std::string msg) {
    if (auto hint = mcpp::pm::unusable_index_hint(); !hint.empty())
        msg += "\n" + hint;
    return msg;
}

// The toolchain a host tool's package chose for itself, read the way its own
// build reads it (#710): the package's manifest with the root-position keys of
// the workspace that lists it (`inherit_workspace_root_position`), then its
// host row's `[target.<host>] toolchain`, then `[toolchain]`. nullopt when none
// names one. Exported for its unit test
// (tests/unit/test_workspace_inheritance.cpp).
export std::optional<std::string>
host_tool_declared_toolchain(const mcpp::manifest::Manifest& tool,
                             const std::filesystem::path& toolRoot,
                             std::string_view platform) {
    auto effective = tool;
    if (const auto wsRoot = mcpp::project::find_workspace_root(toolRoot); !wsRoot.empty())
        if (auto ws = mcpp::manifest::load(wsRoot / "mcpp.toml");
            ws && mcpp::project::is_workspace_member(*ws, wsRoot, toolRoot))
            mcpp::project::inherit_workspace_root_position(effective, *ws, wsRoot);
    if (auto* row = find_target_entry(effective, mcpp::toolchain::triple::host_triple());
        row && !row->toolchain.empty())
        return row->toolchain;
    return effective.toolchain.for_platform(platform);
}

// PrepareState (the state every phase reads and writes) and the phase
// declarations live in the implementation partition `:state` — see the
// file-header comment above for why this file imports no partition at all,
// interface or implementation, and therefore cannot name PrepareState here.
// prepare_build's own definition is driver.cpp; this is only its declaration,
// carrying the default arguments (they belong on exactly one declaration,
// and this is the one every caller sees).
export std::expected<BuildContext, std::string>
prepare_build(bool print_fingerprint, bool includeDevDeps = false,
              std::vector<mcpp::manifest::Target> extraTargets = {},
              BuildOverrides overrides = {});

} // namespace mcpp::build
