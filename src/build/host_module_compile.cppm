// mcpp.build.host_module_compile — the bundled `mcpp` module and the host modules
// a build program imports, compiled once per key and kept by provenance (#748, B1).
//
// Split out of hostprogram.cppm, where `build_mcpp_module` and `build_host_module`
// compiled into each program's own directory. That file's header states why it
// must not grow (a clang 22 miscompile that followed the growth of the module it
// was in); the compile moved here, and the store it reads and writes is
// mcpp.build.host_module_store, which states what an entry is and where it lives.
//
// THE COMPILE IS UNCHANGED, AND ITS PLACE IS NOT. The argument vectors are the
// ones the two functions built, per compiler family, from the same BmiTraits and
// CommandDialect rows. What moved is the directory they run in and write to: a
// scratch directory of the store, where the files are made under the names the
// entry will have, and which is renamed into place when the compile succeeds. A
// consumer then names the entry's files:
//
//   GCC    finds BMIs by name under `<cwd>/gcm.cache`, so a consumer is staged a
//          copy of each BMI it imports in its own `gcm.cache`, as bmi_cache's
//          consumers are. The compile of a host module is given the same staging
//          in its scratch directory, so it finds the BMIs it imports the way its
//          consumer will.
//   Clang  names a BMI with `-fmodule-file=<name>=<path>`, and the path is the
//          entry's.
//   MSVC   names it with `/reference <name>=<path>`.
//
// WHAT A KEY HOLDS. The store records the inputs and compares them field by field
// on a hit; this file decides what they are (`inputs_for`): the host compiler's
// identity, the standard flag, the flags of the compile, the flags that name the
// BMIs it imports and which BMIs those are, the digest of the text it compiles,
// and the provenance (the engine's version for the bundled module, the providing
// package for a host module). For GCC, whose use flags name nothing, the BMIs a
// compile can see are recorded by identity: an entry's key, or the std cache's.
// The compile of a host module is given every BMI made before it, in order, as
// it always was; that is the mechanism by which a rule can import another rule.
//
// A host module is compiled ALONE: its interface may import `std` and the bundled
// `mcpp` module and not a third package. A rule package is a leaf by construction.

export module mcpp.build.host_module_compile;

import std;
import mcpp.build.directives;           // kProtocolVersion — the announced value has ONE source
import mcpp.build.host_module_store;
import mcpp.build.hostprogram;          // the bundled module's text
import mcpp.libs.json;
import mcpp.log;
import mcpp.platform;
import mcpp.platform.process;
import mcpp.toolchain.dialect;
import mcpp.toolchain.fingerprint;      // hash_file / hash_string
import mcpp.toolchain.hostflags;        // bmi_reference_tokens
import mcpp.toolchain.model;
import mcpp.version;                    // MCPP_VERSION — the bundled module's text follows it

export namespace mcpp::build {

namespace fs = std::filesystem;

// A BMI a compile can see: what GCC would find under `gcm.cache`, and what the
// other two families are told by flag. `identity` is what a dependent entry's
// key records for it: the key of the entry it came from, or, for `std`, the name
// of the std cache's directory (which is its own identity hash).
struct VisibleBmi {
    std::string name;
    fs::path    path;
    std::string identity;
};

// What one of these modules contributes to the build.mcpp compile.
struct McppModule {
    std::vector<std::string> useFlags;   // how the consumer names the BMIs
    fs::path                 object;     // linked alongside build.mcpp
    // `mcpp.core` (#734, E8): the same interface under the layer's name, a unit
    // whose whole body re-exports `mcpp`. Empty for a host module.
    fs::path                 aliasObject;
    // The BMIs it made visible, for the compile after it.
    std::vector<VisibleBmi>  bmis;
    std::string              entryKey;
    bool                     reused = false;   // found in the store, not compiled here
    bool                     global = false;   // kept in the global cache
};

// Where a build program's compiled imports may be kept.
struct ModuleStores {
    // The global cache root; empty when `--cache local` or `off` asked for no
    // global entry, in which case everything is kept in the workspace store.
    fs::path cacheRoot;
    // `<workspace>/target/.build-mcpp/host-modules`.
    fs::path workspaceStore;
};

// One host module to provide, and where its text came from (D4 of the plan).
struct HostModuleSource {
    std::string logical;       // the module name its source declares
    fs::path    interface;
    // The provider's identity: the index, package and version for an index
    // package, and the manifest's own name and version otherwise.
    std::string index;
    std::string package;
    std::string version;
    // True when the provider is an index package whose sources are in the
    // immutable store. Only then may the entry be kept globally.
    bool        immutableSource = false;
    // The provider's package root; the interface's directory when empty.
    fs::path    sourceRoot;
};

using CompileEnv = std::vector<std::pair<std::string, std::string>>;

// The bundled `mcpp` module and its `mcpp.core` alias, for the build.mcpp
// compile in `consumerDir`.
std::expected<McppModule, std::string>
provide_mcpp_module(const ModuleStores& stores, const fs::path& consumerDir,
                    const fs::path& compiler, const std::vector<std::string>& base,
                    const std::string& stdFlag, const mcpp::toolchain::Toolchain& tc,
                    const CompileEnv& env);

// One host module. `useFlags` and `visible` are what the compile of the program
// would carry before this module: the flags that name the BMIs made so far, and
// those BMIs.
std::expected<McppModule, std::string>
provide_host_module(const ModuleStores& stores, const HostModuleSource& source,
                    const fs::path& consumerDir, const fs::path& compiler,
                    const std::vector<std::string>& base, const std::string& stdFlag,
                    const mcpp::toolchain::Toolchain& tc, const CompileEnv& env,
                    const std::vector<std::string>& useFlags,
                    const std::vector<VisibleBmi>& visible);

} // namespace mcpp::build

namespace mcpp::build {

namespace {

namespace hm = mcpp::build::hostmods;

constexpr int kStoreEpoch = 1;

std::string join(const std::vector<std::string>& argv) {
    std::string joined;
    for (auto const& a : argv) { if (!joined.empty()) joined += ' '; joined += a; }
    return joined;
}

// The host compiler's identity, as the std module's metadata states it: the
// compiler, its version, its driver (the declared identity, else a hash of the
// binary), the target, and the standard library.
//
// The driver's bytes are read once per path and process. A workspace of N
// programs asks N times and the binary is megabytes.
std::string driver_identity(const mcpp::toolchain::Toolchain& tc) {
    if (!tc.driverIdent.empty()) return mcpp::toolchain::hash_string(tc.driverIdent);
    if (tc.binaryPath.empty()) return {};
    static std::mutex m;
    static std::map<std::string, std::string> seen;
    // UTF-8, never the code page: the name is an identity.
    const auto u8 = tc.binaryPath.generic_u8string();
    const std::string key(reinterpret_cast<const char*>(u8.data()), u8.size());
    std::lock_guard lock(m);
    if (auto it = seen.find(key); it != seen.end()) return it->second;
    return seen.emplace(key, mcpp::toolchain::hash_file(tc.binaryPath)).first->second;
}

nlohmann::json toolchain_identity(const mcpp::toolchain::Toolchain& tc) {
    return {
        {"compiler",         std::string(tc.compiler_name())},
        {"compiler_version", tc.version},
        {"driver_identity",  driver_identity(tc)},
        {"target_triple",    tc.targetTriple},
        {"stdlib",           tc.stdlibId},
        {"stdlib_version",   tc.stdlibVersion},
    };
}

// The inputs that hold for every entry of this file: what the compile is run
// with, apart from the text it compiles and where the text came from.
nlohmann::json common_inputs(std::string_view role, const mcpp::toolchain::Toolchain& tc,
                             const std::vector<std::string>& base, const std::string& stdFlag,
                             const CompileEnv& env, const std::vector<std::string>& use,
                             const std::vector<VisibleBmi>& visible) {
    nlohmann::json j;
    j["epoch"]     = kStoreEpoch;
    j["role"]      = std::string(role);
    j["toolchain"] = toolchain_identity(tc);
    j["std_flag"]  = stdFlag;
    j["base"]      = base;
    j["use"]       = use;
    // The toolchain's own environment (MSVC's INCLUDE and LIB) names the headers
    // the compile reads, and two installs of one cl.exe can differ in it.
    nlohmann::json envj = nlohmann::json::array();
    for (auto const& [k, v] : env) envj.push_back({k, v});
    j["env"] = std::move(envj);
    nlohmann::json imports = nlohmann::json::array();
    for (auto const& b : visible) imports.push_back({{"module", b.name}, {"identity", b.identity}});
    j["imports"] = std::move(imports);
    return j;
}

// `-fmodule-file=<name>=<bmi>` and `/reference <name>=<bmi>`; nothing for GCC,
// which finds the BMI by name under `gcm.cache`.
std::vector<std::string> use_flags(const mcpp::toolchain::Toolchain& tc, std::string_view name,
                                   const fs::path& bmi) {
    if (tc.compiler == mcpp::toolchain::CompilerId::MSVC)
        return mcpp::toolchain::bmi_reference_tokens(std::format(" /reference {}=", name), bmi);
    if (mcpp::toolchain::is_clang(tc))
        return mcpp::toolchain::bmi_reference_tokens(std::format("-fmodule-file={}=", name), bmi);
    return {"-fmodules"};
}

// What GCC finds by name: copies of the BMIs a compile imports, where the
// compile will look. A copy rather than a link, as the std module is staged, and
// for the same reason: a BMI that is rebuilt in place must not change under a
// compile that is reading it.
std::expected<void, std::string>
stage_for_gcc(const fs::path& cwd, std::string_view bmiDir,
              const std::vector<std::pair<std::string, fs::path>>& bmis) {
    std::error_code ec;
    const fs::path dir = cwd / std::string(bmiDir);
    fs::create_directories(dir, ec);
    for (auto const& [file, from] : bmis) {
        if (from.empty() || !fs::exists(from, ec)) continue;
        fs::copy_file(from, dir / file, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return std::unexpected(std::format("staging {} for a build program failed: {}",
                                               file, ec.message()));
    }
    return {};
}

std::string substituted(std::string_view text, std::string_view placeholder,
                        std::string_view value) {
    std::string out(text);
    if (auto p = out.find(placeholder); p != std::string::npos)
        out.replace(p, placeholder.size(), value);
    return out;
}

// The text of the bundled module and of its alias, as they are written and
// compiled. The placeholders stand for what the engine's own scanner would read
// as a second module of hostprogram.cppm (see kMcppModuleSource).
std::pair<std::string, std::string> module_texts() {
    auto mod = substituted(kMcppModuleSource, "@MODULE@", "export module");
    // Substituted rather than hardcoded so the announced version can never
    // drift from the one the engine checks against.
    mod = substituted(mod, "@PROTOCOL@",
                      std::to_string(mcpp::build::directives::kProtocolVersion));
    auto alias = substituted(kMcppCoreAliasSource, "@MODULE@", "export module");
    alias = substituted(alias, "@EXPORT@", "export");
    return {std::move(mod), std::move(alias)};
}

hm::Home home_for(const ModuleStores& stores, const mcpp::toolchain::Toolchain& tc,
                  bool global, std::string_view index, std::string_view package,
                  std::string_view version) {
    const auto traits = mcpp::toolchain::bmi_traits(tc);
    hm::Home h;
    h.bmiDirName  = std::string(traits.bmiDir);
    h.manifestTag = std::string(traits.manifestPrefix);
    if (global && !stores.cacheRoot.empty()) {
        h.kind      = hm::Home::Kind::Global;
        h.cacheRoot = stores.cacheRoot;
        h.index     = std::string(index);
        h.package   = std::string(package);
        h.version   = std::string(version);
    } else {
        h.kind           = hm::Home::Kind::Workspace;
        h.workspaceStore = stores.workspaceStore;
    }
    return h;
}

// Runs one compile of the store's producer. `what` names the step in the log
// and in the failure. The command is logged, always: it is the only place the
// argv of a SUCCESSFUL compile is observable (the `-isystem` rows that say
// whether a host toolchain's C library was attached, #622).
std::expected<void, std::string>
run_step(std::string_view subject, const fs::path& cwd, const CompileEnv& env,
         std::vector<std::string> argv, const char* what) {
    mcpp::log::verbose("buildmcpp-host",
        std::format("{} {}: {}", subject, what, join(argv)));
    auto r = mcpp::platform::process::capture_exec(argv, env, cwd.string());
    if (r.exit_code != 0)
        return std::unexpected(std::format("{} {} failed (exit {}):\n{}",
                                           subject, what, r.exit_code, r.output));
    return {};
}

// Moves what GCC wrote under `gcm.cache` to the entry's `bmi/`.
std::expected<void, std::string>
collect_gcc_bmis(const fs::path& scratch, std::string_view bmiDir,
                 const std::vector<std::string>& files) {
    std::error_code ec;
    for (auto const& f : files) {
        fs::rename(scratch / std::string(bmiDir) / f, scratch / "bmi" / f, ec);
        if (ec)
            return std::unexpected(std::format("the compiler wrote no {}: {}", f, ec.message()));
    }
    return {};
}

} // namespace

std::expected<McppModule, std::string>
provide_mcpp_module(const ModuleStores& stores, const fs::path& consumerDir,
                    const fs::path& compiler, const std::vector<std::string>& base,
                    const std::string& stdFlag, const mcpp::toolchain::Toolchain& tc,
                    const CompileEnv& env)
{
    const auto traits = mcpp::toolchain::bmi_traits(tc);
    const auto& dial  = mcpp::toolchain::dialect_for(tc);
    const bool msvc   = tc.compiler == mcpp::toolchain::CompilerId::MSVC;
    const bool clang  = mcpp::toolchain::is_clang(tc);
    const std::string ext(traits.bmiExt), obj(dial.objExt);

    const auto texts = module_texts();
    const std::string& moduleSrc = texts.first;
    const std::string& aliasSrc  = texts.second;

    // What the entry holds, under the names a consumer will use.
    hm::Files files;
    files.bmi = {"mcpp" + ext, "mcpp.core" + ext};
    files.obj = {"mcpp" + obj, "mcpp_core" + obj};

    auto inputs = common_inputs("build-module", tc, base, stdFlag, env, {}, {});
    // The text the entry is compiled from: the module and its alias, exactly as
    // written. The version is there too, for the reader of entry.json, and because
    // the text follows it.
    inputs["mcpp_version"]    = std::string(mcpp::MCPP_VERSION);
    inputs["interface_sha256"] = hm::sha256_hex(moduleSrc + '\x1f' + aliasSrc);

    // The bundled module is the engine's own text: identical in every project for
    // one mcpp version and one host compiler, so it is kept globally (D4).
    const auto home = home_for(stores, tc, /*global=*/true, "_engine", "mcpp-build-module",
                               std::string(mcpp::MCPP_VERSION));

    auto entry = hm::obtain(home, inputs, files,
        [&](const fs::path& S) -> std::expected<void, std::string> {
            constexpr std::string_view kSubject = "mcpp module";
            std::error_code ec;
            {
                std::ofstream os(S / "mcpp.cppm", std::ios::trunc);
                os << moduleSrc;
                if (!os) return std::unexpected(std::string("could not write mcpp module source"));
            }
            {
                std::ofstream os(S / "mcpp_core.cppm", std::ios::trunc);
                os << aliasSrc;
                if (!os) return std::unexpected(std::string("could not write the mcpp.core unit"));
            }
            const fs::path bmiOut = S / "bmi", objOut = S / "obj";
            auto with_base = [&](std::vector<std::string> head) {
                for (auto const& b : base) head.push_back(b);
                return head;
            };
            auto step = [&](std::vector<std::string> argv, const char* what) {
                return run_step(kSubject, S, env, with_base(std::move(argv)), what);
            };

            if (msvc) {
                // cl produces the .ifc and the .obj in one step.
                const fs::path ifc = bmiOut / ("mcpp" + ext), coreIfc = bmiOut / ("mcpp.core" + ext);
                const fs::path o1 = objOut / ("mcpp" + obj), o2 = objOut / ("mcpp_core" + obj);
                std::vector<std::string> argv{compiler.string()};
                for (auto f : dial.alwaysFlagsArgv) argv.emplace_back(f);
                argv.push_back(stdFlag);
                argv.push_back("/interface");
                for (auto f : dial.forceCxxLangArgv) argv.emplace_back(f);
                argv.push_back(dial.compileOnly == std::string_view("/c") ? "/c" : "-c");
                argv.push_back("mcpp.cppm");
                argv.push_back("/ifcOutput"); argv.push_back(ifc.string());
                argv.push_back(std::string(dial.outputObjPrefix) + o1.string());
                if (auto r = step(std::move(argv), "compile"); !r) return r;
                auto use = mcpp::toolchain::bmi_reference_tokens(" /reference mcpp=", ifc);
                std::vector<std::string> av{compiler.string()};
                for (auto f : dial.alwaysFlagsArgv) av.emplace_back(f);
                av.push_back(stdFlag);
                av.push_back("/interface");
                for (auto f : dial.forceCxxLangArgv) av.emplace_back(f);
                av.push_back(dial.compileOnly == std::string_view("/c") ? "/c" : "-c");
                av.push_back("mcpp_core.cppm");
                av.push_back("/ifcOutput"); av.push_back(coreIfc.string());
                av.push_back(std::string(dial.outputObjPrefix) + o2.string());
                for (auto& f : use) av.push_back(f);
                return step(std::move(av), "mcpp.core compile");
            }

            if (clang) {
                const fs::path pcm = bmiOut / ("mcpp" + ext), corePcm = bmiOut / ("mcpp.core" + ext);
                const fs::path o1 = objOut / ("mcpp" + obj), o2 = objOut / ("mcpp_core" + obj);
                if (auto r = step({compiler.string(), stdFlag, "--precompile",
                                   "mcpp.cppm", "-o", pcm.string()}, "precompile"); !r) return r;
                if (auto r = step({compiler.string(), stdFlag, "-c",
                                   pcm.string(), "-o", o1.string()}, "object"); !r) return r;
                auto use = mcpp::toolchain::bmi_reference_tokens("-fmodule-file=mcpp=", pcm);
                std::vector<std::string> pre{compiler.string(), stdFlag, "--precompile",
                                             "mcpp_core.cppm", "-o", corePcm.string()};
                for (auto& f : use) pre.push_back(f);
                if (auto r = step(std::move(pre), "mcpp.core precompile"); !r) return r;
                std::vector<std::string> ob{compiler.string(), stdFlag, "-c",
                                            corePcm.string(), "-o", o2.string()};
                for (auto& f : use) ob.push_back(f);
                return step(std::move(ob), "mcpp.core object");
            }

            // GCC: BMIs are implicit under <cwd>/gcm.cache, so nothing to name.
            const fs::path o1 = objOut / ("mcpp" + obj), o2 = objOut / ("mcpp_core" + obj);
            if (auto r = step({compiler.string(), stdFlag, "-fmodules", "-c",
                               "mcpp.cppm", "-o", o1.string()}, "compile"); !r) return r;
            if (auto r = step({compiler.string(), stdFlag, "-fmodules", "-c",
                               "mcpp_core.cppm", "-o", o2.string()}, "mcpp.core compile"); !r) return r;
            return collect_gcc_bmis(S, traits.bmiDir, files.bmi);
        });
    if (!entry) return std::unexpected(entry.error());
    mcpp::log::verbose("buildmcpp-host",
        std::format("bundled module mcpp: entry {} {} ({})", entry->key,
                    entry->global ? "global" : "workspace",
                    entry->reused ? "reused" : "compiled"));

    McppModule out;
    out.entryKey = entry->key;
    out.reused   = entry->reused;
    out.global   = entry->global;
    out.object      = entry->obj(files.obj[0]);
    out.aliasObject = entry->obj(files.obj[1]);
    out.bmis = {{"mcpp",      entry->bmi(files.bmi[0]), entry->key},
                {"mcpp.core", entry->bmi(files.bmi[1]), entry->key}};
    if (msvc || clang) {
        out.useFlags = use_flags(tc, "mcpp", out.bmis[0].path);
        for (auto& f : use_flags(tc, "mcpp.core", out.bmis[1].path)) out.useFlags.push_back(f);
    } else {
        out.useFlags = {"-fmodules"};
        if (auto r = stage_for_gcc(consumerDir, traits.bmiDir,
                                   {{files.bmi[0], out.bmis[0].path},
                                    {files.bmi[1], out.bmis[1].path}}); !r)
            return std::unexpected(r.error());
    }
    return out;
}

std::expected<McppModule, std::string>
provide_host_module(const ModuleStores& stores, const HostModuleSource& source,
                    const fs::path& consumerDir, const fs::path& compiler,
                    const std::vector<std::string>& base, const std::string& stdFlag,
                    const mcpp::toolchain::Toolchain& tc, const CompileEnv& env,
                    const std::vector<std::string>& useFlags,
                    const std::vector<VisibleBmi>& visible)
{
    std::error_code ec;
    if (!fs::exists(source.interface, ec)) {
        return std::unexpected(std::format(
            "host module '{}': no interface unit at {}\n"
            "       A package offering build rules must have a lib root "
            "(src/<name>.cppm or [lib] path).",
            source.logical, source.interface.string()));
    }
    const auto traits = mcpp::toolchain::bmi_traits(tc);
    const auto& dial  = mcpp::toolchain::dialect_for(tc);
    const bool msvc   = tc.compiler == mcpp::toolchain::CompilerId::MSVC;
    const bool clang  = mcpp::toolchain::is_clang(tc);
    const std::string ext(traits.bmiExt), objExt(dial.objExt);

    // A filesystem-safe stem. Partition separators and any path separator that
    // sneaks into a logical name would otherwise create directories that do
    // not exist. Dots are left ALONE on purpose: `a.b.rules.o` is a legal
    // filename, GCC's own gcm.cache uses the dotted module name verbatim, and
    // rewriting them would make the object name disagree with the BMI name for
    // no gain.
    std::string stem(source.logical);
    for (auto& c : stem) if (c == ':' || c == '/' || c == '\\') c = '-';

    hm::Files files;
    files.bmi = {stem + ext};
    files.obj = {stem + objExt};

    const auto digest = hm::sha256_file(source.interface);
    if (digest.empty())
        return std::unexpected(std::format("host module '{}': cannot read {}",
                                           source.logical, source.interface.string()));

    auto inputs = common_inputs("host-module", tc, base, stdFlag, env, useFlags, visible);
    inputs["module"]           = source.logical;
    inputs["interface_sha256"] = digest;
    inputs["provider"] = {{"index", source.index}, {"name", source.package},
                          {"version", source.version}};
    // A package whose sources can change in place: what the interface includes is
    // part of what was compiled, and it may sit anywhere in the package, so the
    // package's tree is part of the key.
    const bool global = source.immutableSource && !stores.cacheRoot.empty();
    inputs["source"] = global ? "immutable" : "workspace";
    if (!global) {
        auto tree = hm::tree_digest(source.sourceRoot.empty() ? source.interface.parent_path()
                                                              : source.sourceRoot);
        inputs["source_tree"] = tree.complete ? tree.hex : ("unbounded:" + hm::process_nonce());
    }

    const auto home = home_for(stores, tc, global, source.index, source.package, source.version);

    // The interface's LANGUAGE, stated rather than inferred from its extension.
    //
    // Measured on macOS CI: a rule package whose lib root is `rulepkg.ixx`
    // made `clang++ --precompile rulepkg.ixx -o rulepkg.pcm` EXIT 0 AND WRITE
    // NOTHING — clang's driver does not recognise `.ixx`, so it treated the file
    // as a linker input, warned that it was unused, and succeeded. The failure
    // surfaced one step later as `no such file or directory: …/rulepkg.pcm`,
    // naming an output rather than the input that was never read.
    //
    // Every other module compile in mcpp already says this (BmiTraits::
    // moduleInterfaceLangFlag — `/interface /TP`, `-x c++-module`, `-x c++`);
    // the host-module path was the one place that still let the driver guess.
    // It is positional on GNU-style drivers, so it goes immediately before the
    // input.
    std::vector<std::string> langArgv;
    {
        std::string_view lang = traits.moduleInterfaceLangFlag;
        for (std::size_t i = 0; i < lang.size(); ) {
            while (i < lang.size() && lang[i] == ' ') ++i;
            auto j = lang.find(' ', i);
            if (j == std::string_view::npos) j = lang.size();
            if (j > i) langArgv.emplace_back(lang.substr(i, j - i));
            i = j;
        }
    }

    auto entry = hm::obtain(home, inputs, files,
        [&](const fs::path& S) -> std::expected<void, std::string> {
            const std::string subject = std::format("host module '{}'", source.logical);
            const fs::path bmiOut = S / "bmi", objOut = S / "obj";
            auto with_base = [&](std::vector<std::string> head) {
                for (auto const& b : base)     head.push_back(b);
                for (auto const& f : useFlags) head.push_back(f);
                return head;
            };
            auto step = [&](std::vector<std::string> argv, const char* what) {
                return run_step(subject, S, env, with_base(std::move(argv)), what);
            };
            const fs::path o = objOut / files.obj[0];

            if (msvc) {
                const fs::path ifc = bmiOut / files.bmi[0];
                std::vector<std::string> argv{compiler.string()};
                for (auto f : dial.alwaysFlagsArgv) argv.emplace_back(f);
                argv.push_back(stdFlag);
                argv.push_back("/interface");
                for (auto f : dial.forceCxxLangArgv) argv.emplace_back(f);
                argv.push_back("/c");
                argv.push_back(source.interface.string());
                argv.push_back("/ifcOutput"); argv.push_back(ifc.string());
                argv.push_back(std::string(dial.outputObjPrefix) + o.string());
                return step(std::move(argv), "compile");
            }

            if (clang) {
                const fs::path pcm = bmiOut / files.bmi[0];
                std::vector<std::string> pre{compiler.string(), stdFlag, "--precompile"};
                for (auto const& l : langArgv) pre.push_back(l);
                pre.push_back(source.interface.string());
                pre.push_back("-o"); pre.push_back(pcm.string());
                if (auto r = step(std::move(pre), "precompile"); !r) return r;
                // The precompile can succeed and write nothing when the driver
                // ignored the input, which is exactly what happened above.
                // Checked here so the diagnostic names the interface rather than
                // a missing output.
                std::error_code e2;
                if (!fs::exists(pcm, e2)) {
                    return std::unexpected(std::format(
                        "host module '{}': the compiler accepted '{}' and produced no "
                        "BMI.\n"
                        "       The interface's language is passed explicitly, so this "
                        "is not an extension\n"
                        "       the driver failed to recognise — check that the file "
                        "really is a module interface.",
                        source.logical, source.interface.string()));
                }
                return step({compiler.string(), stdFlag, "-c", pcm.string(), "-o", o.string()},
                            "object");
            }

            // GCC: BMIs are implicit under <cwd>/gcm.cache, so nothing to name —
            // which is also why the compile has to find every BMI it imports
            // there.
            {
                std::vector<std::pair<std::string, fs::path>> staged;
                for (auto const& b : visible)
                    staged.emplace_back(b.path.filename().string(), b.path);
                if (auto r = stage_for_gcc(S, traits.bmiDir, staged); !r) return r;
            }
            std::vector<std::string> gccArgv{compiler.string(), stdFlag, "-fmodules", "-c"};
            for (auto const& l : langArgv) gccArgv.push_back(l);
            gccArgv.push_back(source.interface.string());
            gccArgv.push_back("-o"); gccArgv.push_back(o.string());
            if (auto r = step(std::move(gccArgv), "compile"); !r) return r;
            return collect_gcc_bmis(S, traits.bmiDir, files.bmi);
        });
    if (!entry) return std::unexpected(entry.error());
    mcpp::log::verbose("buildmcpp-host",
        std::format("host module '{}': entry {} {} ({})", source.logical, entry->key,
                    entry->global ? "global" : "workspace",
                    entry->reused ? "reused" : "compiled"));

    McppModule out;
    out.entryKey = entry->key;
    out.reused   = entry->reused;
    out.global   = entry->global;
    out.object   = entry->obj(files.obj[0]);
    out.bmis     = {{source.logical, entry->bmi(files.bmi[0]), entry->key}};
    if (msvc || clang) {
        out.useFlags = use_flags(tc, source.logical, out.bmis[0].path);
    } else {
        out.useFlags = {"-fmodules"};
        if (auto r = stage_for_gcc(consumerDir, traits.bmiDir,
                                   {{files.bmi[0], out.bmis[0].path}}); !r)
            return std::unexpected(r.error());
    }
    return out;
}

} // namespace mcpp::build
