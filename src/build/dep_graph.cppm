// mcpp.build.dep_graph — queries over the resolved consumer→dependency edge
// graph.
//
// WHY THIS EXISTS
//
// prepare.cppm records one authoritative edge graph during resolution, and by
// now fourteen places read it. Most ask one of exactly two questions —
// "what does package X depend on directly?" and "what is X's transitive
// closure?" — and each had hand-written the loop. Two of them (the two
// build.mcpp call sites emitting MCPP_DEP_<NAME>_DIR) were near-identical
// copies, and #355 added a third variant plus a hand-rolled BFS.
//
// That is the shape this codebase keeps paying for (#233/#240/#242/#344): the
// same decision derived in N places does not fail when you add the N+1th, it
// fails later, somewhere else. Feature activation already learned it the hard
// way — activation and resolution each walked their own idea of "the edges"
// and silently disagreed about transitive requests (#242/#243).
//
// Templated on the edge type rather than owning it: `DependencyEdge` is a
// local struct inside prepare_build, and moving it out would be a much larger
// change than the one this pays for. An edge only has to expose
// `consumerPackageIndex` and `dependencyPackageIndex`.
//
// WHAT IS DELIBERATELY *NOT* HERE
//
// The build cache's per-package key walk (plan.cpp) is NOT a closure query
// and its fold is not expressed through this module. It computes a value per
// node (that package's full cache key) FROM its dependencies' already-computed
// values, treats a cycle as a hard ERROR rather than something to skip, and
// threads a taint flag alongside. It does use mcpp.graph — `topological_order`
// turns what used to be a memoized recursion into a loop over that order — but
// the per-node fold and the taint are still the caller's, because folding them
// into a generic traversal would either lose those properties or force the
// abstraction to grow until it described exactly one caller. `closure` and
// `topological_order` ask two genuinely different questions (reachability,
// order), and neither is "compute this fold".

export module mcpp.build.dep_graph;

import std;
import mcpp.graph;

export namespace mcpp::build::dep_graph {

// Package indices this consumer depends on DIRECTLY, in edge-record order,
// deduplicated. Order is preserved because several callers surface it to the
// user (dependency dirs, diagnostics) and a stable order keeps output
// diffable.
template <class Edge>
std::vector<std::size_t>
direct_dependencies(const std::vector<Edge>& edges, std::size_t consumer) {
    std::vector<std::size_t> out;
    for (auto const& e : edges) {
        if (e.consumerPackageIndex != consumer) continue;
        if (std::find(out.begin(), out.end(), e.dependencyPackageIndex) == out.end())
            out.push_back(e.dependencyPackageIndex);
    }
    return out;
}

// Every package reachable from `from`, excluding `from` itself. Sorted and
// deduplicated, so a caller folding it into a cache key gets a stable answer
// without re-sorting. Implemented on mcpp.graph's closure: the edges are
// reduced to an adjacency list once and mcpp::graph::closure walks it, which
// is the same "consumer depends on dependency" direction the edge already
// carries (`u -> v` in mcpp.graph means "u depends on v").
//
// A cycle is TOLERATED here (mcpp::graph::closure's visited set terminates
// it) rather than reported. This is a reachability question, and the callers
// that must reject a cycle — the build-cache key walk — detect it where they
// can say which package the cycle runs through, which is the only form of
// that message worth printing.
template <class Edge>
std::vector<std::size_t>
transitive_dependencies(const std::vector<Edge>& edges, std::size_t from) {
    std::size_t n = from + 1;
    for (auto const& e : edges) {
        n = std::max(n, e.consumerPackageIndex + 1);
        n = std::max(n, e.dependencyPackageIndex + 1);
    }
    mcpp::graph::AdjacencyList deps(n);
    for (auto const& e : edges)
        deps[e.consumerPackageIndex].push_back(e.dependencyPackageIndex);
    return mcpp::graph::closure(deps, std::array{from}, /*include_roots=*/false);
}

// The two spellings a dependency is addressable by: its canonical package name
// and, when it is namespaced, the namespace-stripped tail.
//
// Not a graph query, but it lives here for the same reason: three call sites
// had each open-coded the `rfind('.')` split, and a fourth was about to. A
// consumer may write `compat.zlib` or `zlib`, and every place that surfaces a
// dependency by name has to accept both.
inline std::vector<std::string> name_spellings(const std::string& canonical) {
    std::vector<std::string> out{canonical};
    if (auto dot = canonical.rfind('.');
        dot != std::string::npos && dot + 1 < canonical.size())
        out.push_back(canonical.substr(dot + 1));
    return out;
}

} // namespace mcpp::build::dep_graph
