// options.cpp -- the invocation options prepare_build resolves: the MSVC
// guidance, the build-cache mode and the profile. Declared, with their
// documentation, in prepare.cppm.

module mcpp.build.prepare;
import :state;

import mcpp.build.prepare_inputs;

import std;
import mcpp.build.version_floor;
import mcpp.manifest;
import mcpp.source_kind;
import mcpp.toolchain.hostflags;   // the compile-token producer the package std module reuses
import mcpp.toolchain.detect;
import mcpp.toolchain.dialect;
import mcpp.toolchain.fingerprint;
import mcpp.toolchain.msvc;
import mcpp.toolchain.registry;
import mcpp.toolchain.linkmodel;
// For `resolve_version_match` / `list_installed_versions`: a bare compiler
// family named by the dependency graph resolves to a concrete version through
// exactly the path `mcpp toolchain default <family>` uses.
import mcpp.toolchain.lifecycle;
import mcpp.toolchain.stdmod;
import mcpp.toolchain.post_install;
import mcpp.toolchain.abi;
import mcpp.toolchain.triple;
import mcpp.build.plan;
import mcpp.build.flags;          // compute_flags — the per-role contracts (#418)
import mcpp.build.graph_shape;  // #407: the graph says which mode wrote it
import mcpp.build.build_program;
import mcpp.build.backend;      // BuildOptions for the tool sub-build
import mcpp.build.ninja;        // make_ninja_backend — driving that sub-build
import mcpp.toolchain.post_install;
import mcpp.platform;

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
