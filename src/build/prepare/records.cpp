// records.cpp -- P13, the records half: mcpp.lock and resolution.json.

module mcpp.build.prepare;
import :state;

import mcpp.build.prepare_inputs;

import std;
import mcpp.diag;
import mcpp.build.stage;
import mcpp.build.refusal;
import mcpp.build.version_floor;
import mcpp.home;
import mcpp.platform.axis;
import mcpp.libs.json;
import mcpp.log;
import mcpp.manifest;
import mcpp.source_kind;
import mcpp.toolchain.clang;
import mcpp.toolchain.hostflags;   // the compile-token producer the package std module reuses
import mcpp.toolchain.cppfly;
import mcpp.toolchain.detect;
import mcpp.toolchain.dialect;
import mcpp.toolchain.model;      // is_msvc_target — the MSVC-ABI default (#718)
import mcpp.toolchain.fingerprint;
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
import mcpp.build.resources;    // #365 Windows resources: synthesise / scan / find rc
import mcpp.build.backend;      // BuildOptions for the tool sub-build
import mcpp.build.ninja;        // make_ninja_backend — driving that sub-build
import mcpp.lockfile;
import mcpp.config;
import mcpp.xlings;
import mcpp.runtime.binding;
import mcpp.platform.runtime_search;
import mcpp.toolchain.post_install;
import mcpp.platform;
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
import mcpp.bmi_cache;

namespace mcpp::build {

// The two files a build writes about itself, cut out of plan.cpp at a step
// boundary (check_file_lengths.sh). The statements are the ones phase13_finish
// runs, called from it in the same order, and are declared in the `:state`
// partition (state.cppm) as the phases are.

std::expected<void, std::string> step13_lockfile(PrepareState& state, BuildContext& ctx) {
    // Write/update mcpp.lock for any version-based deps that succeeded.
    // Path deps are intentionally NOT locked — their source is local filesystem.
    //
    // mcpp#363: the version entries come from `resolved` — what the walk
    // actually picked — not from `m->dependencies`, which still holds the
    // constraint the user wrote and only covers DIRECT deps. Reading the input
    // instead of the output made the lock record `^1.92.8` (a range locks
    // nothing) and omit the transitive graph entirely. Git entries deliberately
    // stay on `m->dependencies`: their lock line is read back as a resolution
    // anchor (#329), keyed by the root manifest's map key, and that contract is
    // unchanged here.
    {
        mcpp::lockfile::Lockfile lock;
        lock.schemaVersion = 2;

        // The lock key for a dep the ROOT declares is the map key it declared
        // it under (`compat.imgui`, `gtest`) — that is the key #329's git anchor
        // lookup uses, and changing it would silently unpin every branch dep.
        // A dep reached only transitively has no such key, so it is written
        // under its fully-qualified identity.
        auto lock_name_for = [&](const ResolvedKey& k) -> std::string {
            for (auto const& [n, s] : state.m->dependencies) {
                const std::string sn = s.shortName.empty() ? n : s.shortName;
                if (s.namespace_ == k.ns && sn == k.shortName) return n;
            }
            return mcpp::pm::compat::qualified_name(k.ns, k.shortName);
        };

        // Lock custom index shas from manifest [indices] section.
        for (auto const& [idxName, spec] : state.m->indices) {
            if (spec.is_local() || spec.is_builtin()) continue;
            mcpp::lockfile::LockedIndex li;
            li.name = idxName;
            li.url  = spec.url;
            li.rev  = spec.rev;   // may be empty if not yet resolved
            lock.indices.push_back(std::move(li));
        }

        // Git deps: root-declared only, unchanged (see the note above).
        for (auto const& [name, spec] : state.m->dependencies) {
            if (!spec.isGit()) continue;
            mcpp::lockfile::LockedPackage lp;
            lp.name    = name;
            lp.version = spec.gitRev;
            auto gitIt = state.root_git_lock_identities.find(name);
            if (gitIt == state.root_git_lock_identities.end()) {
                lp.source = std::format("git+{}#{}={}",
                    spec.git, spec.gitRefKind, spec.gitRev);
                lp.hash = "fnv1a:" + mcpp::toolchain::hash_string(lp.source);
            } else {
                lp.source = gitIt->second.source;
                lp.hash = gitIt->second.hash;
            }
            lock.packages.push_back(std::move(lp));
        }

        // Version deps: the whole resolved graph, at the versions actually
        // chosen. `resolved` is an ordered map, so the file is deterministic.
        for (auto const& [key, rec] : state.resolved) {
            if (rec.source != "version") continue;   // path / git handled elsewhere
            if (rec.version.empty()) continue;
            // See ResolvedRecord::devOnly: `mcpp test` resolves dev-deps and
            // `mcpp build` does not, so writing them would make the file depend
            // on which command ran last.
            if (rec.devOnly) continue;
            mcpp::lockfile::LockedPackage lp;
            lp.name       = lock_name_for(key);
            lp.namespace_ = key.ns;
            lp.version    = rec.version;
            // Use the namespace and resolved version as the source identifier.
            // For custom indices, include the index name for traceability.
            auto sourceIndex = lp.namespace_.empty()
                ? std::string(mcpp::pm::kDefaultNamespace)
                : lp.namespace_;
            lp.source     = std::format("index+{}@{}", sourceIndex, lp.version);
            // Use a deterministic hash based on namespace + name + version.
            // A future PR can replace this with a real content hash from the
            // xpkg.lua's declared sha256 or from the install plan.
            //
            // NOT `std::hash<std::string>`: its output is implementation-defined
            // (MSVC FNV-1a, libstdc++/libc++ MurmurHash), so the same dependency
            // used to hash differently on Windows and Linux while the `fnv1a:`
            // prefix claimed otherwise. `index_package_digest` is FNV-1a on
            // every host.
            lp.hash = mcpp::pm::index_package_digest(sourceIndex, lp.name, lp.version);
            lock.packages.push_back(std::move(lp));
        }
        if (!lock.packages.empty() || !lock.indices.empty()) {
            auto lockPath = state.workRoot / "mcpp.lock";
            // `--locked` ASSERTS THAT THIS RESOLUTION IS THE RECORDED ONE.
            //
            // The file has always been written after the walk and never read
            // back as a constraint; its own header says so ("does not yet pin
            // future builds"). Making it an input to resolution is a change to
            // the resolver. Making it an ASSERTION is not, and it is the half
            // that reproducibility actually needs: a release build, a CI job or
            // an audit can demand that what resolved today is what was recorded,
            // and find out when it is not.
            //
            // THE FAILURE NAMES THE DIFFERENCE. "The lock is out of date" is
            // true and useless; which package moved, from which version to
            // which, is what the reader does something about.
            if (mcpp::platform::env::get("MCPP_LOCKED").value_or("") == "1") {
                auto prior = mcpp::lockfile::load(lockPath);
                if (!prior) {
                    return std::unexpected(std::format(
                        "--locked was given and there is no readable mcpp.lock at {}\n"
                        "       Run the same command without --locked once to record "
                        "this resolution, then commit mcpp.lock.",
                        lockPath.string()));
                }
                auto key = [](const mcpp::lockfile::LockedPackage& p) {
                    return p.namespace_.empty() ? p.name
                                                : p.namespace_ + "." + p.name;
                };
                std::map<std::string, std::string> was, now;
                for (auto const& p : prior->packages) was[key(p)] = p.version;
                for (auto const& p : lock.packages)    now[key(p)] = p.version;
                std::vector<std::string> drift;
                for (auto const& [k, v] : now) {
                    auto it = was.find(k);
                    if (it == was.end())      drift.push_back(k + " " + v + " (not in the lock)");
                    else if (it->second != v) drift.push_back(k + " " + it->second + " -> " + v);
                }
                for (auto const& [k, v] : was)
                    if (!now.contains(k)) drift.push_back(k + " " + v + " (no longer resolved)");
                if (!drift.empty()) {
                    std::string msg = "--locked was given and this resolution "
                                      "differs from mcpp.lock:";
                    for (auto const& d : drift) msg += "\n         " + d;
                    msg += "\n       Re-run without --locked to update the lock, "
                           "or pin the dependency that moved.";
                    return std::unexpected(msg);
                }
            }
            (void)mcpp::lockfile::write(lock, lockPath);
        }

        // Same data, second consumer: the "Compiling <dep> v<version>" banner.
        // It reads this rather than re-deriving from the manifest, so the banner
        // and the lock cannot disagree about what was built.
        for (auto const& [key, rec] : state.resolved) {
            if (rec.source != "version" || rec.version.empty()) continue;
            ctx.resolvedVersions[lock_name_for(key)] = rec.version;
        }
    }
    return {};
}

void step13_resolution_json(PrepareState& state, BuildContext& ctx) {
    // Per-build resolution manifest: the durable, provider-neutral facts that
    // `mcpp why runtime` interprets without resolving again or probing the
    // current host.  The post-link validator replaces `validation.pending`
    // with the exact artifact verdict produced at the link seam.
    {
        const std::string tcAbi =
            ctx.tc.targetTriple.find("musl") != std::string::npos ? "musl"
            : ctx.tc.stdlibId == "libc++"                          ? "libc++"
            : ctx.tc.compiler == mcpp::toolchain::CompilerId::MSVC ? "msvc"
            :                                                         "glibc";
        auto package_json = [](const mcpp::manifest::PackageId& id) {
            return nlohmann::json{
                {"canonical", id.canonical()},
                {"namespace", id.namespace_},
                {"name", id.name},
                {"version", id.version},
                {"source", id.sourceProvenance},
            };
        };
        auto path_array = [](auto const& paths) {
            nlohmann::json values = nlohmann::json::array();
            for (auto const& path : paths)
                values.push_back(path.lexically_normal().generic_string());
            return values;
        };
        nlohmann::json j;
        j["schema_version"] = 2;
        j["toolchain"] = {
            {"spec", ctx.tc.label()}, {"abi", tcAbi},
            {"triple", ctx.tc.targetTriple}, {"stdlib", ctx.tc.stdlibId},
        };
        nlohmann::json dirs = nlohmann::json::array();
        for (auto& d : ctx.plan.runtimeLibraryDirs) dirs.push_back(d.string());
        nlohmann::json legacyCaps = nlohmann::json::array();
        nlohmann::json providers = nlohmann::json::array();
        for (auto& [cap, prov] : ctx.plan.runtimeProviders)
        {
            legacyCaps.push_back({{"capability", cap},
                                  {"provider", prov.canonical()}});
            providers.push_back({{"capability", cap},
                                 {"provider", package_json(prov)}});
        }
        nlohmann::json requirements = nlohmann::json::array();
        for (auto const& requirement : ctx.plan.runtimeRequirements) {
            requirements.push_back({
                {"kind", requirement.kind},
                {"value", requirement.value},
                {"phase", requirement.phase},
                {"requester", package_json(requirement.requester)},
                {"required", requirement.required},
            });
        }
        nlohmann::json artifacts = nlohmann::json::array();
        for (auto const& artifact : ctx.plan.runtimeArtifacts) {
            artifacts.push_back({
                {"role", artifact.role},
                {"provider", package_json(artifact.provider)},
                {"path", artifact.path.lexically_normal().generic_string()},
                {"provenance", artifact.provenance},
                {"abi", artifact.abi},
                {"digest", artifact.digest},
                {"host_fingerprint", artifact.hostFingerprint},
                // A requirement must land on a THING, and the thing must be
                // the one that was declared. mcpp already enforces this for
                // the private libc; recording it per artifact makes a stale
                // binding visible instead of leaving `providers:` naming
                // something nobody checked.
                {"identity", std::string(
                    mcpp::build::runtime_validation::to_string(
                        mcpp::build::runtime_validation
                            ::artifact_identity_verdict(artifact)))},
            });
        }
        nlohmann::json binding = nlohmann::json::parse(
            mcpp::platform::runtime::serialize_runtime_binding(
                ctx.plan.runtimeBinding), nullptr, false);
        if (binding.is_discarded()) binding = nlohmann::json::object();

        // ASKED OF THE PARSED TRIPLE, with the substring test kept only for a
        // spelling `parse` rejects. This field is the SECOND copy of a
        // derivation `mcpp.build.dist::format_for` already owns, and it had
        // the same defect: mcpp's canonical `aarch64-macos` contains neither
        // "apple" nor "darwin", so an explicit `--target aarch64-macos`
        // recorded `"elf"` while the native build on the same machine recorded
        // `"macho"` -- one report contradicting the other about one machine.
        std::string format = "elf";
        if (auto t = mcpp::toolchain::triple::parse(ctx.tc.targetTriple)) {
            format = std::string(mcpp::toolchain::triple::to_string(t->object_format()));
            std::ranges::transform(format, format.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (format == "mach-o") format = "macho";
        } else {
            auto triple = ctx.tc.targetTriple;
            std::ranges::transform(triple, triple.begin(),
                [](unsigned char c) { return std::tolower(c); });
            const bool pe = triple.find("windows") != std::string::npos
                         || triple.find("mingw") != std::string::npos;
            const bool macho = triple.find("darwin") != std::string::npos
                            || triple.find("apple") != std::string::npos;
            format = pe ? "pe" : macho ? "macho" : "elf";
        }
        // The ORDERED run-time search closure with provenance. Order is
        // semantics here, not presentation: it is what the loader will walk,
        // and the mutable SubOS farm sitting last is the invariant that keeps
        // libc resolving from the pinned payload. Recorded so "why does my GL
        // program find its driver" is answerable without readelf, and so a
        // regression in the ordering is visible to CI and to `mcpp why`.
        nlohmann::json closure = nlohmann::json::array();
        for (auto const& dir : ctx.plan.runtimeSearch) {
            closure.push_back({
                {"path", dir.path.generic_string()},
                {"origin", std::string(
                    mcpp::platform::search::to_string(dir.origin))},
                {"machine_local",
                    mcpp::platform::search::is_machine_local(dir.origin)},
            });
        }
        nlohmann::json search = {
            {"format", format},
            {"link_library", format == "pe" ? "libpath" : "library_path"},
            {"transitive_needed", format == "elf" ? "rpath_link" : "none"},
            {"runtime", format == "pe" ? "deploy"
                         : format == "macho" ? "loader_rpath" : "runpath"},
            {"closure", closure},
        };
        // #418 — the contract each ROLE actually got, after any downgrade.
        //
        // `CompileFlags::contractByRole` was written and never read: a valuable
        // observation with no way out of the process. Since #414 the shared
        // library role can legitimately end up on a different contract from the
        // binaries beside it, so "which one did my .so actually get?" is a
        // question a user has, and the only answer available was to run
        // `readelf` and infer.
        //
        // Recorded as the RESOLVED value, not the requested one — a request
        // that was downgraded is exactly the case worth being able to see.
        // `compute_flags` is pure in the plan; prepare does not otherwise hold
        // the result, and threading it through just for this would widen a
        // signature for one field.
        const auto roleFlags = mcpp::build::compute_flags(ctx.plan);
        nlohmann::json contracts = nlohmann::json::object();
        for (std::size_t i = 0; i < mcpp::build::dist::kRoleCount; ++i) {
            contracts[std::string(mcpp::build::dist::to_string(
                          static_cast<mcpp::build::dist::Role>(i)))] =
                std::string(mcpp::build::dist::to_string(roleFlags.contractByRole[i]));
        }

        // #634, X: the resolved dependency graph. One entry per package, the
        // root first: its identity as `runtime` records identities, every
        // request that reached it with the key as written and the table that
        // declared it, and for a library the link form with its reason. It is
        // what `mcpp why deps` prints, and what a test of a resolution rule
        // reads instead of a warning's wording.
        {
            nlohmann::json graphPackages = nlohmann::json::array();
            for (std::size_t i = 0; i < state.packages.size(); ++i)
                graphPackages.push_back(state.graph_package_entry(i, /*forBuildProgram=*/false));
            j["graph"] = { {"packages", std::move(graphPackages)} };
        }

        // What sits beside the program, as the runtime placement resolver
        // decided it (mcpp.build.runtime_placement), with the kind of each
        // source and the C++ runtime set chosen. `mcpp pack` and `mcpp why
        // runtime` read the same decision; this is its record.
        nlohmann::json placement = nlohmann::json::array();
        for (auto const& d : roleFlags.runtimeDeploy) {
            using Origin = mcpp::build::BuildPlan::DeployFile::Origin;
            placement.push_back({
                {"dest", d.dest.generic_string()},
                {"sources", path_array(d.sources)},
                {"kind", d.origin == Origin::Derived   ? "derived"
                       : d.origin == Origin::Toolchain ? "toolchain" : "declared"},
            });
        }
        nlohmann::json crtSet = {
            {"rule", roleFlags.runtimeCrtPolicy},
            {"kind", roleFlags.runtimeCrtKind},
            {"version", roleFlags.runtimeCrtVersion},
        };
        j["runtime"] = {
            {"cxx_runtime_by_role", contracts},
            {"placement", std::move(placement)},
            {"crt_set", std::move(crtSet)},
            {"placement_notes", roleFlags.runtimeNotes},
            {"library_dirs", dirs},
            {"dlopen_libs", ctx.plan.runtimeDlopenLibs},
            {"capabilities", legacyCaps},
            {"binding", binding},
            {"requirements", requirements},
            {"artifacts", artifacts},
            {"providers", providers},
            {"link_intent", {
                {"libraries", ctx.plan.linkIntent.libraries},
                {"link_library_dirs",
                    path_array(ctx.plan.linkIntent.linkLibraryDirs)},
                {"transitive_needed_dirs",
                    path_array(ctx.plan.linkIntent.transitiveNeededDirs)},
                {"runtime_search_dirs",
                    path_array(ctx.plan.linkIntent.runtimeSearchDirs)},
                {"frameworks", ctx.plan.linkIntent.frameworks},
                {"deploy_files", path_array(ctx.plan.linkIntent.deployFiles)},
                {"deploy", [&] {
                    auto a = nlohmann::json::array();
                    for (auto const& d : ctx.plan.linkIntent.deploy)
                        a.push_back({{"from", d.from.generic_string()},
                                     {"to", d.to}});
                    return a;
                }()},
            }},
            {"search", search},
            {"validation", {
                {"status", format == "elf" ? "pending" : "not_exercised"},
                {"source", "post_link"},
                {"artifacts", nlohmann::json::array()},
            }},
        };
        // THE MSVC SYSROOT OF THE CLANG ROW: which toolset and SDK the build
        // compiled against, and where each came from. Absent on every other
        // row, so a reader can tell "not this row" from "not recorded".
        if (!ctx.plan.toolchain.msvcToolsDir.empty()) {
            const auto& tcr = ctx.plan.toolchain;
            j["msvc_toolset"] = {
                {"version", tcr.msvcToolsVersion},
                {"origin",  tcr.msvcOrigin},
                {"product", tcr.msvcProduct},
                {"root",    tcr.msvcToolsDir.generic_string()},
            };
            j["windows_sdk"] = {
                {"version", tcr.windowsSdkVersion},
                {"root",    tcr.windowsSdkRoot.generic_string()},
            };
        }
        std::error_code ec;
        std::filesystem::create_directories(ctx.plan.outputDir, ec);
        auto path = ctx.plan.outputDir / "resolution.json";
        auto tmp = path;
        tmp += ".tmp";
        if (std::ofstream js(tmp); js) {
            js << j.dump(2) << "\n";
            js.close();
            std::filesystem::rename(tmp, path, ec);
            if (ec) {
                ec.clear();
                std::filesystem::remove(path, ec);
                ec.clear();
                std::filesystem::rename(tmp, path, ec);
            }
        }
    }
}

} // namespace mcpp::build
