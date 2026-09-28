#include <gtest/gtest.h>

import std;
import mcpp.graph;

// SUBSYSTEM-LEVEL: this package answers "what order / what depth / what does
// this reach" for an index-addressed directed graph, and nothing else. The
// migrated callers (src/modgraph/graph.cppm, src/build/prepare/features.cpp,
// src/build/prepare/graph.cpp, src/build/dep_graph.cppm,
// src/build/prepare/plan.cpp) restate a subset of these facts through their
// own edge shape in tests/unit/test_modgraph.cpp and in the e2e suite.

namespace g = mcpp::graph;

namespace {
g::AdjacencyList chain(std::size_t n) {
    // 0 <- 1 <- 2 <- ... <- n-1 (node i depends on i-1).
    g::AdjacencyList deps(n);
    for (std::size_t i = 1; i < n; ++i) deps[i] = {i - 1};
    return deps;
}
}

TEST(TopologicalOrder, EmptyGraphOrdersNothing) {
    auto r = g::topological_order({});
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

TEST(TopologicalOrder, SingleNodeNoDeps) {
    auto r = g::topological_order({{}});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0}));
}

TEST(TopologicalOrder, UnconstrainedNodesComeOutInInputIndexOrder) {
    // No edges at all: every node is ready from the start, and ties are
    // broken by index, so the order is the identity permutation.
    g::AdjacencyList deps(5);
    auto r = g::topological_order(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3, 4}));
}

TEST(TopologicalOrder, TiesAreBrokenByInputIndexEvenAgainstDeclarationOrder) {
    // Node 2 depends on nothing but is declared last among the roots; nodes
    // 0 and 1 depend on nothing either. All three become ready at once, so
    // the tie-break (lowest index first) — not the order dependents were
    // declared in — decides: 0, 1, 2, then 3 (which depends on all three).
    g::AdjacencyList deps(4);
    deps[3] = {2, 0, 1};   // declared in a scrambled order on purpose
    auto r = g::topological_order(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(TopologicalOrder, Diamond) {
    // 3 depends on {1, 2}; 1 and 2 both depend on {0}.
    g::AdjacencyList deps(4);
    deps[1] = {0};
    deps[2] = {0};
    deps[3] = {1, 2};
    auto r = g::topological_order(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(TopologicalOrder, DisconnectedComponentsOrderByIndexNotByComponent) {
    // Two unrelated chains, 0 -> 2 and 1 -> 3 (node 2 depends on node 0,
    // node 3 depends on node 1). Both roots are ready at once, so the
    // tie-break processes 0 then 1 before either child — the two
    // components' roots interleave by index rather than one component
    // finishing before the other starts.
    g::AdjacencyList deps(4);
    deps[2] = {0};
    deps[3] = {1};
    auto r = g::topological_order(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(TopologicalOrder, LongChainIsTheIdentityPermutation) {
    auto deps = chain(6);
    auto r = g::topological_order(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3, 4, 5}));
}

TEST(TopologicalOrder, SelfLoopIsACycleOfLengthOne) {
    g::AdjacencyList deps(1);
    deps[0] = {0};
    auto r = g::topological_order(deps);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().cycle, (std::vector<std::size_t>{0, 0}));
}

TEST(TopologicalOrder, LongerCycleReportsTheExactPath) {
    // a(0) -> b(1) -> c(2) -> a(0), plus an unrelated node 3 that depends on
    // nothing and must not appear in the ring.
    g::AdjacencyList deps(4);
    deps[0] = {1};   // a depends on b
    deps[1] = {2};   // b depends on c
    deps[2] = {0};   // c depends on a
    auto r = g::topological_order(deps);
    ASSERT_FALSE(r.has_value());
    // The path starts wherever the DFS first re-enters the ring; what is
    // pinned down is that it IS the ring a -> b -> c -> a, walked in one
    // direction, closed at both ends by the same node.
    const auto& cycle = r.error().cycle;
    ASSERT_GE(cycle.size(), 2u);
    EXPECT_EQ(cycle.front(), cycle.back());
    for (std::size_t i = 0; i + 1 < cycle.size(); ++i) {
        auto u = cycle[i], v = cycle[i + 1];
        EXPECT_TRUE(std::ranges::find(deps[u], v) != deps[u].end())
            << u << " does not declare a dependency on " << v;
    }
    // Exactly the three ring members, not the unrelated fourth node.
    std::set<std::size_t> members(cycle.begin(), cycle.end());
    EXPECT_EQ(members, (std::set<std::size_t>{0, 1, 2}));
}

TEST(TopologicalOrder, CycleAmongOtherwiseOrderableNodes) {
    // 0 is a normal leaf; 1 and 2 cycle on each other; 3 depends on 1.
    g::AdjacencyList deps(4);
    deps[1] = {2};
    deps[2] = {1};
    deps[3] = {1};
    auto r = g::topological_order(deps);
    ASSERT_FALSE(r.has_value());
    std::set<std::size_t> members(r.error().cycle.begin(), r.error().cycle.end());
    EXPECT_EQ(members, (std::set<std::size_t>{1, 2}));
}

TEST(Levels, LeavesAreLevelZero) {
    g::AdjacencyList deps(3);
    auto r = g::levels(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 0, 0}));
}

TEST(Levels, ChainCountsUp) {
    auto deps = chain(4);
    auto r = g::levels(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(Levels, DiamondTakesTheLongerArm) {
    // 4 depends on {1, 3}; 1 depends on {0}; 3 depends on {2}, {2} depends
    // on {0}. The two arms from 0 to 4 have different lengths (2 and 3), and
    // the level is the LONGER one.
    g::AdjacencyList deps(5);
    deps[1] = {0};
    deps[2] = {0};
    deps[3] = {2};
    deps[4] = {1, 3};
    auto r = g::levels(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)[0], 0u);
    EXPECT_EQ((*r)[1], 1u);
    EXPECT_EQ((*r)[2], 1u);
    EXPECT_EQ((*r)[3], 2u);
    EXPECT_EQ((*r)[4], 3u);
}

TEST(Levels, DisconnectedNodesAreIndependent) {
    g::AdjacencyList deps(3);
    deps[2] = {1};
    // node 0 is isolated and must not affect node 2's level.
    auto r = g::levels(deps);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)[0], 0u);
    EXPECT_EQ((*r)[1], 0u);
    EXPECT_EQ((*r)[2], 1u);
}

TEST(Levels, SameCycleErrorAsTopologicalOrder) {
    g::AdjacencyList deps(2);
    deps[0] = {1};
    deps[1] = {0};
    auto r = g::levels(deps);
    ASSERT_FALSE(r.has_value());
    std::set<std::size_t> members(r.error().cycle.begin(), r.error().cycle.end());
    EXPECT_EQ(members, (std::set<std::size_t>{0, 1}));
}

TEST(Closure, DirectNeighboursAreJustAdjacency) {
    g::AdjacencyList deps(3);
    deps[0] = {1, 2};
    EXPECT_EQ(deps[0], (std::vector<std::size_t>{1, 2}));
}

TEST(Closure, ExcludesRootsByDefault) {
    // 0 -> 1 -> 2 -> 3 (a chain of dependencies).
    g::AdjacencyList deps(4);
    deps[0] = {1};
    deps[1] = {2};
    deps[2] = {3};
    auto c = g::closure(deps, std::array{std::size_t{0}});
    EXPECT_EQ(c, (std::vector<std::size_t>{1, 2, 3}));
}

TEST(Closure, IncludeRootsAddsThemBack) {
    g::AdjacencyList deps(4);
    deps[0] = {1};
    deps[1] = {2};
    deps[2] = {3};
    auto c = g::closure(deps, std::array{std::size_t{0}}, /*include_roots=*/true);
    EXPECT_EQ(c, (std::vector<std::size_t>{0, 1, 2, 3}));
}

TEST(Closure, MultipleRootsUnionAndDeduplicate) {
    // 0 -> 2, 1 -> 2 -> 3: both roots reach 2, so it appears once.
    g::AdjacencyList deps(4);
    deps[0] = {2};
    deps[1] = {2};
    deps[2] = {3};
    auto c = g::closure(deps, std::array{std::size_t{0}, std::size_t{1}});
    EXPECT_EQ(c, (std::vector<std::size_t>{2, 3}));
}

TEST(Closure, DisconnectedNodeIsNeverReached) {
    g::AdjacencyList deps(3);
    deps[0] = {1};
    // node 2 is isolated: no root can reach it.
    auto c = g::closure(deps, std::array{std::size_t{0}});
    EXPECT_EQ(c, (std::vector<std::size_t>{1}));
}

TEST(Closure, TolerantOfACycleReachableFromARoot) {
    // 0 -> 1 -> 2 -> 1 (a cycle downstream of the root). Closure does not
    // reject it; it just returns the reachable set once.
    g::AdjacencyList deps(3);
    deps[0] = {1};
    deps[1] = {2};
    deps[2] = {1};
    auto c = g::closure(deps, std::array{std::size_t{0}});
    EXPECT_EQ(c, (std::vector<std::size_t>{1, 2}));
}

TEST(Closure, RootWithNoEdgesHasAnEmptyClosure) {
    g::AdjacencyList deps(2);
    auto c = g::closure(deps, std::array{std::size_t{0}});
    EXPECT_TRUE(c.empty());
}
