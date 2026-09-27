// options.cpp -- the invocation options prepare_build resolves: the MSVC
// guidance, the build-cache mode and the profile. Declared, with their
// documentation, in prepare.cppm.

module mcpp.build.prepare;
import :state;

import mcpp.build.prepare_inputs;

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

std::string msvc_unavailable_guidance(const mcpp::toolchain::Toolchain& tc) {
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

std::optional<CacheMode> parse_cache_mode(std::string_view v) {
    if (v == "global") return CacheMode::Global;
    if (v == "local")  return CacheMode::Local;
    if (v == "off" || v == "none") return CacheMode::Off;
    return std::nullopt;
}

std::string_view cache_mode_name(CacheMode m) {
    switch (m) {
        case CacheMode::Local: return "local";
        case CacheMode::Off:   return "off";
        default:               return "global";
    }
}

CacheMode resolve_cache_mode(const mcpp::manifest::Manifest& m,
                                    std::string_view override_mode) {
    if (auto v = parse_cache_mode(override_mode)) return *v;
    if (const char* e = std::getenv("MCPP_BUILD_CACHE"); e && *e)
        if (auto v = parse_cache_mode(e)) return *v;
    if (auto v = parse_cache_mode(m.buildConfig.cacheMode)) return *v;
    return CacheMode::Global;
}

std::string resolve_profile_name(const mcpp::manifest::Manifest& m,
                                        std::string_view override_name,
                                        std::string_view fallback) {
    if (!override_name.empty())                 return std::string(override_name);
    if (!m.buildConfig.defaultProfile.empty())  return m.buildConfig.defaultProfile;
    return fallback.empty() ? std::string("dev") : std::string(fallback);
}

std::string profile_override_from_flags(std::string_view profileOption,
                                               bool release, bool dev) {
    if (!profileOption.empty()) return std::string(profileOption);
    if (release)                return "release";
    if (dev)                    return "dev";
    return {};
}

} // namespace mcpp::build
