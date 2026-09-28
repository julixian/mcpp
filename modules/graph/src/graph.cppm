// mcpp.graph — Kahn's algorithm, dependency levels and closure, over
// index-addressed directed graphs.
//
// THE SHAPE EVERY CALLER ALREADY HAS
//
// A module-import graph, a package-dependency graph and a build-rule graph
// are unrelated domains, but by the time any of them asks "what order", "what
// depth" or "what does this reach", the caller has already reduced its own
// nodes to a dense index range 0..n-1 and its edges to an adjacency list.
// This package is that reduced question's one answer, factored out after
// `src/modgraph/graph.cppm`, `src/build/prepare/features.cpp` (twice),
// `src/build/dep_graph.cppm` and `src/build/prepare/plan.cpp` had each
// hand-written a variant of Kahn's algorithm or a DFS cycle check, with four
// different tie-break rules and three different cycle-reporting shapes.
//
// EDGE ORIENTATION
//
// `deps[u]` lists the nodes `u` depends on: an edge `u -> v` means "u depends
// on v", so a leaf with no dependencies is a node with an empty adjacency
// list. This is the direction every migrated call site already used for its
// own hand-rolled adjacency (a consumer's edge list, an importer's imports,
// a package's `[dependencies]`), so no call site has to invert its data to
// call in.
//
// THREE ORDERS, AND WHY A CALLER KEEPS THE ONE IT HAD
//
// An order among nodes that do not constrain each other is still observable
// when the order becomes a link order: Mach-O runs initializers in link order,
// and the module graph's unit order is the order of the objects on the link
// line. Changing it changed a program's initialization (measured 2026-09-29:
// openkal's `same-source` example, linked for aarch64-macos, crashed at start
// when its units moved to the stable order below). A caller whose order
// reaches an artifact therefore keeps its order, and this module offers each
// order the engine uses:
//
//   topological_order        Kahn, lowest ready index first (stable)
//   stack_topological_order  Kahn, most recently readied first, over edges in
//                            the order given (the module graph's unit order)
//   depth_first_order        dependencies first, depth first, from roots in
//                            the order given (the host-module orders)
//
// STABILITY
//
// `topological_order` and `levels` are Kahn's algorithm with the ready set
// kept as an ordered set rather than an arbitrary queue: whenever more than
// one node has no unmet dependency, the LOWEST INDEX is emitted next. Two
// graphs that differ only in an unconstrained sibling order therefore
// disagree nowhere it can be observed by anything downstream, and the answer
// for a fully-ordered chain (0 depends on nothing, 1 depends on 0, ...) is
// always the identity permutation.
//
// CYCLES
//
// `topological_order` and `levels` report a cycle as the ordered PATH that
// walks it — `cycle = {a, b, c, a}`, read "a depends on b depends on c
// depends on a" — not merely the set of nodes that Kahn's algorithm could not
// retire. A path names the actionable edges; a set makes the reader
// reconstruct them. `closure` asks a different question (reachability, not
// order) and is defined to tolerate a cycle rather than reject it: a visited
// set already terminates the walk, and rejecting would turn every caller that
// does not care about order into one that has to.

export module mcpp.graph;

import std;

export namespace mcpp::graph {

// `deps[u]` = the node ids `u` depends on, `u` and each entry of `deps[u]`
// being indices into `deps` itself (0..deps.size()-1). Duplicate entries
// within one node's list are tolerated (treated as one edge).
using AdjacencyList = std::vector<std::vector<std::size_t>>;

// A cycle, reported as the ring it walks: `cycle.front() == cycle.back()`,
// and consecutive entries are a real edge (`cycle[i]` depends on
// `cycle[i + 1]`). Never empty when returned as an error — a ring has at
// least the two entries closing a self-loop (`{a, a}`).
struct CycleError {
    std::vector<std::size_t> cycle;
};

// Dependency-first order: every node comes after every node it (transitively)
// depends on. See the module header for the stability and cycle contract.
std::expected<std::vector<std::size_t>, CycleError>
topological_order(const AdjacencyList& deps);

// `result[u]` = 0 when `u` has no dependencies, otherwise
// `1 + max(result[v] for v in deps[u])`. Same stability and cycle contract as
// `topological_order`, computed by walking that order once — every node's
// dependencies are already leveled by the time the node itself is reached.
std::expected<std::vector<std::size_t>, CycleError>
levels(const AdjacencyList& deps);

// Every node reachable from `roots` by following dependency edges
// transitively, returned sorted and deduplicated. `include_roots` selects
// whether the roots themselves are in the result (default: excluded, i.e. the
// STRICT closure — what the roots depend on, not the roots' own identity).
//
// Tolerant of cycles: a visited set already terminates the walk, so a cycle
// reachable from a root is silently absorbed into the answer rather than
// rejected. Closure is a reachability question; a caller that also needs to
// know a cycle is not well-formed asks `topological_order` or `levels`
// instead — that is exactly the caller (`plan.cpp`'s cache-key fold) that
// must reject one.
std::vector<std::size_t>
closure(const AdjacencyList& deps, std::span<const std::size_t> roots,
        bool include_roots = false);

// Kahn's algorithm with the ready nodes on a stack: the node readied most
// recently is emitted next. `edges` holds (dependent, dependency) pairs over
// nodes 0..n-1, in the order that decides the ties. This is the order of the
// module graph's units, and so of the objects on a link line (see the header).
std::expected<std::vector<std::size_t>, CycleError>
stack_topological_order(std::size_t n,
                        std::span<const std::pair<std::size_t, std::size_t>> edges);

// What a dependency on a node still on the depth-first path means.
enum class Cycles {
    Error,   // a cycle: the order is refused with the ring
    Skip,    // the edge is ignored; the node is emitted where it stands
};

// Dependencies first, depth first: from each root in the order given, a node
// is emitted after every node it depends on, its dependencies visited in the
// order of `deps[u]`. Only nodes reachable from `roots` are emitted.
std::expected<std::vector<std::size_t>, CycleError>
depth_first_order(const AdjacencyList& deps, std::span<const std::size_t> roots,
                  Cycles cycles = Cycles::Error);

} // namespace mcpp::graph

namespace mcpp::graph {

namespace {

// Shared Kahn's-algorithm walk. Returns the dependency-first order, or (when
// a cycle remains) the CycleError both public functions report verbatim.
std::expected<std::vector<std::size_t>, CycleError>
kahn_order(const AdjacencyList& deps) {
    const std::size_t n = deps.size();
    std::vector<std::vector<std::size_t>> dependents(n);   // reverse edges
    std::vector<std::size_t> remaining(n, 0);               // unmet deps left
    for (std::size_t u = 0; u < n; ++u) {
        for (auto v : deps[u]) {
            remaining[u]++;
            dependents[v].push_back(u);
        }
    }

    std::set<std::size_t> ready;   // ordered: smallest index served first
    for (std::size_t u = 0; u < n; ++u)
        if (remaining[u] == 0) ready.insert(u);

    std::vector<std::size_t> order;
    order.reserve(n);
    while (!ready.empty()) {
        auto it = ready.begin();
        std::size_t u = *it;
        ready.erase(it);
        order.push_back(u);
        for (auto w : dependents[u])
            if (--remaining[w] == 0) ready.insert(w);
    }

    if (order.size() == n) return order;

    // A cycle remains among the nodes Kahn's algorithm could not retire
    // (`remaining[u] > 0`). Walk it with an explicit-stack DFS restricted to
    // those nodes to recover the actual ring, rather than reporting the
    // leftover set: the set says something is wrong, the ring says what.
    std::vector<bool> stuck(n);
    for (std::size_t u = 0; u < n; ++u) stuck[u] = remaining[u] > 0;

    std::vector<char> state(n, 0);   // 0 unseen, 1 on the current path, 2 done
    std::vector<std::size_t> path;
    for (std::size_t start = 0; start < n; ++start) {
        if (!stuck[start] || state[start] != 0) continue;
        std::vector<std::pair<std::size_t, std::size_t>> frame{{start, 0}};
        while (!frame.empty()) {
            auto& [u, k] = frame.back();
            if (k == 0) {
                state[u] = 1;
                path.push_back(u);
            }
            bool descended = false;
            while (k < deps[u].size()) {
                auto v = deps[u][k++];
                if (!stuck[v]) continue;
                if (state[v] == 1) {
                    auto ring_start = std::ranges::find(path, v);
                    CycleError err;
                    err.cycle.assign(ring_start, path.end());
                    err.cycle.push_back(v);
                    return std::unexpected(std::move(err));
                }
                if (state[v] == 0) {
                    frame.push_back({v, 0});
                    descended = true;
                    break;
                }
            }
            if (descended) continue;
            state[u] = 2;
            path.pop_back();
            frame.pop_back();
        }
    }
    // Unreachable in theory: Kahn's algorithm left at least one stuck node,
    // and every stuck node's dependencies are themselves stuck (otherwise
    // Kahn's algorithm would have retired it), so the DFS above always
    // closes a ring before running out of stuck, unseen starting points.
    // Reported defensively rather than assumed, as an unclosed path rather
    // than a valid ring, so a defect here is legible instead of undefined.
    CycleError err;
    err.cycle = path;
    return std::unexpected(std::move(err));
}

} // namespace

std::expected<std::vector<std::size_t>, CycleError>
topological_order(const AdjacencyList& deps) {
    return kahn_order(deps);
}

std::expected<std::vector<std::size_t>, CycleError>
stack_topological_order(std::size_t n,
                        std::span<const std::pair<std::size_t, std::size_t>> edges) {
    std::vector<std::size_t> remaining(n, 0);
    std::vector<std::vector<std::size_t>> dependents(n);
    for (auto [dependent, dependency] : edges) {
        remaining[dependent]++;
        dependents[dependency].push_back(dependent);
    }
    std::vector<std::size_t> order;
    order.reserve(n);
    std::vector<std::size_t> ready;
    for (std::size_t u = 0; u < n; ++u)
        if (remaining[u] == 0) ready.push_back(u);
    while (!ready.empty()) {
        const std::size_t u = ready.back();
        ready.pop_back();
        order.push_back(u);
        for (auto w : dependents[u])
            if (--remaining[w] == 0) ready.push_back(w);
    }
    if (order.size() == n) return order;
    // The ring, found the way `topological_order` finds one.
    AdjacencyList deps(n);
    for (auto [dependent, dependency] : edges) deps[dependent].push_back(dependency);
    auto ring = kahn_order(deps);
    if (!ring) return std::unexpected(ring.error());
    return std::unexpected(CycleError{});
}

std::expected<std::vector<std::size_t>, CycleError>
depth_first_order(const AdjacencyList& deps, std::span<const std::size_t> roots,
                  Cycles cycles) {
    const std::size_t n = deps.size();
    std::vector<char> state(n, 0);   // 0 unseen, 1 on the current path, 2 done
    std::vector<std::size_t> order;
    std::vector<std::size_t> path;
    for (auto root : roots) {
        if (root >= n || state[root] != 0) continue;
        std::vector<std::pair<std::size_t, std::size_t>> frame{{root, 0}};
        state[root] = 1;
        path.push_back(root);
        while (!frame.empty()) {
            auto& [u, k] = frame.back();
            if (k < deps[u].size()) {
                const auto v = deps[u][k++];
                if (v >= n || state[v] == 2) continue;
                if (state[v] == 1) {
                    if (cycles == Cycles::Skip) continue;
                    CycleError err;
                    err.cycle.assign(std::ranges::find(path, v), path.end());
                    err.cycle.push_back(v);
                    return std::unexpected(std::move(err));
                }
                state[v] = 1;
                path.push_back(v);
                frame.push_back({v, 0});
                continue;
            }
            state[u] = 2;
            order.push_back(u);
            path.pop_back();
            frame.pop_back();
        }
    }
    return order;
}

std::expected<std::vector<std::size_t>, CycleError>
levels(const AdjacencyList& deps) {
    auto order = kahn_order(deps);
    if (!order) return std::unexpected(order.error());

    std::vector<std::size_t> level(deps.size(), 0);
    for (auto u : *order) {
        std::size_t lv = 0;
        for (auto v : deps[u]) lv = std::max(lv, level[v] + 1);
        level[u] = lv;
    }
    return level;
}

std::vector<std::size_t>
closure(const AdjacencyList& deps, std::span<const std::size_t> roots,
        bool include_roots) {
    std::set<std::size_t> seen;
    std::vector<std::size_t> stack(roots.begin(), roots.end());
    for (auto r : roots) seen.insert(r);
    while (!stack.empty()) {
        auto u = stack.back();
        stack.pop_back();
        if (u >= deps.size()) continue;
        for (auto v : deps[u])
            if (seen.insert(v).second) stack.push_back(v);
    }
    if (!include_roots)
        for (auto r : roots) seen.erase(r);
    return {seen.begin(), seen.end()};
}

} // namespace mcpp::graph
