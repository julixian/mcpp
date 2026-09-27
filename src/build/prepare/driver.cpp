// driver.cpp -- prepare_build itself: construct the PrepareState, run the
// phases in order, and return what the last one builds. An early error of any
// phase ends the call with that phase's message.

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

std::expected<BuildContext, std::string>
prepare_build(bool print_fingerprint,
              bool includeDevDeps,
              std::vector<mcpp::manifest::Target> extraTargets,
              BuildOverrides overrides) {
    PrepareState state(print_fingerprint, includeDevDeps,
                        std::move(extraTargets), std::move(overrides));
    pending_flag_words_notes().clear();

    if (auto r = phase0_manifest_and_workspace(state); !r) return std::unexpected(r.error());
    if (auto r = phase1_toolchain_spec_and_axes(state); !r) return std::unexpected(r.error());
    if (auto r = phase2_define_toolchain_resolver(state); !r) return std::unexpected(r.error());
    if (auto r = phase3_xlings_before_graph(state); !r) return std::unexpected(r.error());
    if (auto r = phase4a_graph_load(state); !r) return std::unexpected(r.error());
    if (auto r = phase4b_graph_worklist(state); !r) return std::unexpected(r.error());
    if (auto r = phase5_toolchain_after_graph(state); !r) return std::unexpected(r.error());
    if (auto r = phase6_features_and_host_tools(state); !r) return std::unexpected(r.error());
    if (auto r = phase9_target_side(state); !r) return std::unexpected(r.error());
    if (auto r = phase11_scan(state); !r) return std::unexpected(r.error());

    return phase13_finish(state);
}


} // namespace mcpp::build
