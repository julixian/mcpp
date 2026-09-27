// driver.cpp -- prepare_build itself: construct the PrepareState, run the
// phases in order, and return what the last one builds. An early error of any
// phase ends the call with that phase's message.

module mcpp.build.prepare;
import :state;

import mcpp.build.prepare_inputs;

import std;
import mcpp.build.version_floor;
import mcpp.manifest;
import mcpp.source_kind;
// For `resolve_version_match` / `list_installed_versions`: a bare compiler
// family named by the dependency graph resolves to a concrete version through
// exactly the path `mcpp toolchain default <family>` uses.
import mcpp.build.plan;
import mcpp.build.flags;          // compute_flags — the per-role contracts (#418)
import mcpp.build.graph_shape;  // #407: the graph says which mode wrote it
import mcpp.build.build_program;
import mcpp.build.backend;      // BuildOptions for the tool sub-build
import mcpp.build.ninja;        // make_ninja_backend — driving that sub-build
import mcpp.platform;

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
