---
subject: design
status: active
---

# The workspace as the unit of build: one graph per configuration, one scheduler, product directories, and a reusable graph module

- Status: design, agreed; implementation in progress (revision 4)
- Date: 2026-09-29
- Origin: the #734 validation on a five-member Windows workspace (the validation
  project's pull request), and the discussion that followed it
- Replaces: E1 of `.agents/docs/2026-09-28-build-cost-foreign-toolsets-and-library-surface-design.md`
  (a shared member built once through a per-consumer admission check)

Revision 2 settles the configuration key field by field (§3), places each
member's products in its own directory instead of refusing shared names (§5),
adds hard-linked placement (§5.3), folds the build key over the graph module's
order (§9), and adds the self-review (§12). Revision 3 records a planning
regression of 2026.9.28.3 and its cause (§1.1). Revision 4 drops the separate
patch release that revision 3 proposed: the regression and the cost underneath
it are both fixed by this design, in one release (§14), and §15 divides the
work into tasks with their dependencies across the repositories.

## 0. Scope

`mcpp build --workspace` builds the members one after another, each as an
independent root with its own plan and its own ninja. This document replaces that
loop with a workspace-level build: the members and everything they depend on are
nodes of one graph per configuration, and one scheduler runs all graphs of a
command. It also extracts the graph algorithms the engine has written by hand
several times into one module under `modules/`.

Out of scope: the parallelism an action carries inside itself (a `cmake --build
--parallel` inside a `prepare` action), which is a resource declaration of actions
and needs its own design (§10).

## 1. Readings

| Reading | Value | Source |
|---|---|---|
| `mcpp build --workspace`, 2026.9.28.3, vcpkg binaries cached | 1828 s | validation project, run 36464299728 |
| the same with 2026.9.28.2 | 1726 s | run 36378870254 |
| compilations of the core library in one workspace build | 3 (itself, CLI, GUI) | the build log: `Compiling gpp.core (path)` in each consumer |
| E1 on that workspace | admitted `gpp.version`, refused the core library | the build log; the reason is printed under `-v` (the run that follows revision 1) |
| lines printed per consumer by E1's admission | the member's full header: `Workspace building member`, the toolchain resolution, the build-program lines | the build log |
| `mcpp pack --format release`, two members | 209 s | run 36464299728 |
| what the build directory's name hashes today | the toolchain, the triple, the standard library, the C++ standard, the runtime contract, the std BMI, the mcpp version, the lock, and **every package's** flags (`canonical_compile_flags` of the root plus `canonical_package_build_metadata` of all packages) | `modules/toolchain-model/src/fingerprint.cppm`, `src/build/prepare/scan.cpp` |
| ninja in the xlings index | 1.12.1 only; the jobserver client arrived in ninja 1.13 | `xim-pkgindex/pkgs/n/ninja.lua` |
| how a ninja edge names a file | by the declared string: an absolute and a relative path to the same bytes are two nodes | `.agents/docs/2026-08-29-build-rule-package-spec.md` §11.4 |
| the working directory of every edge | the build directory; advice and `${mcpp.target_file:}` are build-dir-relative | `src/build/advice.cppm`, the same spec |
| MinGW `as`, `ld`, `collect2`, `ar` in a directory outside the ANSI code page | work only with relative paths (measured) | `.agents/docs/2026-09-25-issues-693-696-triage-and-repair-plan.md` |
| hand-written graph algorithms | module order (Kahn, `modgraph/graph.cppm`, reports the nodes left rather than the cycle), unit order by imports (iterative DFS, `prepare/features.cpp`), host-module order (post-order DFS with a cycle path, same file), dependency closure (`build/dep_graph.cppm`), the build-key fold (`prepare/plan.cpp`) | the sources |

### 1.1 The planning regression of 2026.9.28.3

A user's nine-member workspace took 6.3 s for a build with nothing to do under
2026.9.28.2 and 79 s under 2026.9.28.3, while compiling took about one second.
Reproduced locally (Linux, released binaries, a build with nothing to do):

| Workspace | 2026.9.28.2 | 2026.9.28.3 |
|---|---|---|
| nine members, eight programs using one library | 4.8 s, 9 toolchain resolutions | 5.2 s, 17 |
| five libraries in a chain and one program, `--workspace` | 7.6 s, 6 | 36.8 s, 63 |
| the same chain, `-p app` only | not measured | 24.1 s, 32 (2^5) |

**Cause.** E1 plans a shared member with a new `BuildOverrides`, whose
`tool_depth` is 0, so the nested plan meets E1's own condition and plans that
member's member dependencies again, recursively. A chain of n members costs 2^n
full plans for `-p` on its end, and each plan resolves the toolchain, resolves
dependencies, checks the xlings payloads and the build programs.

**The cost underneath.** Even under 2026.9.28.2, `--workspace` takes no fast
path: every member is planned in full on every command (about 1.2 s per member
here), because the fast path is recorded per root and the workspace loop builds
each member as a root.

How other build systems place outputs, for §5:

| Tool | Intermediate outputs | Final outputs | Runtime files |
|---|---|---|---|
| Cargo | `target/<profile>/deps/`, file names carrying a metadata hash | flat in `target/<profile>/`; a shared name is a warning and the later one wins | none; `cargo run` sets the library path |
| Bazel | `bazel-bin/<package path>/` | `bazel-bin/<package path>/<target>` | one `<binary>.runfiles/` tree per binary (symbolic links) |
| CMake | the source directory's mirror under the build directory | the same, or one flat directory on Windows (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`) so that DLLs are shared | install rules |
| Meson | the source directory's mirror | the same | install rules |

## 2. Principles

1. **A build directory is one configuration and holds one ninja graph whose
   paths are relative to it.** This is the convention of CMake, Meson and GN
   (`ninja -C <build dir>`), and three facts of this engine depend on it: file
   identity in ninja is the declared string, every edge runs in the build
   directory, and the MinGW tools need relative paths. The design keeps it.
2. **The workspace is the unit of build.** A command on a workspace plans the
   workspace; `-p` selects goals inside that plan.
3. **A node is a (package, configuration) pair.** A package appears once per
   configuration; deduplication is a property of the graph, not a check.
4. **No configuration is refused.** Members that differ in toolchain, target,
   standard or runtime contract build, in separate graphs.
5. **A program's directory is its runtime closure.** `mcpp run` sees what
   `mcpp pack` stages. Each member's products therefore live in the member's own
   product directory.
6. **One mechanism, reused.** The planner already builds a root with its
   dependencies in one graph, each package with its own flags, features and
   build program; the workspace plan is that planner with a virtual root.
   Graph algorithms live in one module.

## 3. Configurations

A **configuration** is the set of inputs that every node of one graph must share,
because the compiled outputs of two configurations cannot be linked or imported
together. Everything else is an attribute of a package node: it enters that
package's command lines and its build key, and it does not decide which graph a
package is in.

| Input (field of today's fingerprint) | Class | Why |
|---|---|---|
| compiler, its version and driver identity | configuration | one toolchain per graph |
| target triple | configuration | one target per graph |
| standard library and its version | configuration | objects must agree on it |
| C++ runtime contract (`/MD` against `/MT`, static against shared runtime) | configuration | a whole-program property (docs/20) |
| the std module BMI | configuration | one per graph; every importer must match it |
| C++ standard (`[package] standard`) | configuration | a BMI is refused across standards |
| dialect flags (`dialect_cxxflags`) | configuration | they must reach `import std` as well |
| macOS deployment target | configuration | the std module and the ABI follow it |
| the C-environment realisation (`[c-abi]` tokens, the openkal kernel ABI) | configuration | objects disagree otherwise (the design of 2026-09-18 §3.4) |
| profile (`opt`, `debug`, `lto`), defined at the workspace root | configuration | selected per command, one per graph |
| mcpp version | configuration | one per build |
| `cflags`, `cxxflags`, `ldflags`, defines, include directories, per-glob flags | package | per-package flags exist today (e2e 50) |
| `c_standard` | package | per-package since 2026.9.26 |
| features | package | per-package since the start |
| `mcpp.lock` | not an input | the lock is not hashed today (`dependencyLockHash` is empty); a dependency's version reaches its own commands through its source paths |

Members whose configuration inputs are equal share one graph; members whose
inputs differ get separate graphs, each compiled correctly.

**The directory name.** Today the directory hashes every package's flags, so
editing one `cxxflag` of one member moves the whole graph to a new directory and
rebuilds everything. In a workspace graph that would rebuild every member for an
edit to one. The directory name is therefore the configuration key alone;
a change to a package's flags changes that package's command lines, and ninja
rebuilds exactly the edges whose commands changed (it records them in
`.ninja_log`). Criterion: an edit to one member's `cxxflags` recompiles that
member's units and nothing else (§11).

## 4. One graph per configuration

For each configuration the planner builds one graph with a **virtual workspace
root**:

- the root has no sources; it depends by `path` on every member of that
  configuration;
- the link units are the members' targets (programs, libraries, tests);
- every package, member or dependency, appears once, with its own flags,
  features, target rows and build program, as dependencies appear today;
- the build directory is `<workspace root>/target/<triple>/<configuration key>/`,
  with paths relative to it.

A shared member is compiled once because it is one node. E1's admission check,
its nested plans, its stage edges from member directories and its lock are no
longer needed (§7).

A workspace whose root is itself a package (#725) has that package among the
members of its configuration.

## 5. Where outputs go

### 5.1 Two kinds of output

| Kind | Place | Shared by |
|---|---|---|
| intermediate: objects, BMIs, static libraries, generated sources | `<build dir>/obj/<package>/`, as today | every consumer in the graph |
| products: a member's programs and shared libraries, with their deployed files and DLLs | the member's **product directory** `<build dir>/bin/<package name>/` | that member only |

The product directory keeps principle 5: the files a program reads at run time
and the DLLs it loads are beside it, exactly as `mcpp pack` stages them. Two
members with a program of the same name do not collide, and the deployment sets
of two programs never mix.

### 5.2 Names

- A member's product directory is named by its **package name** (`bin/cli/`,
  `bin/gui/`).
- When two members of one configuration share a package name (they are in
  different namespaces), both use the qualified name `namespace.name`.
- A package that is not in a workspace, and the root package of a rooted
  workspace, keep `bin/` itself, so every single-package project keeps its paths.
- A member's tests keep the placement they have today, under its product
  directory.

### 5.3 Hard-linked placement

A runtime file that several programs need (a Qt DLL, a data tree) is placed in
each product directory that needs it. On one file system the placement links
instead of copying: the bytes exist once and the placement costs no copy. The
placement already writes out of place and renames, so replacing the file at one
path never changes the other. Where a link cannot be made (another volume, a file
system without links, a permission) it copies, which is today's behaviour.
Criterion: two product directories that place the same file share its inode or
file index where the file system allows it (§11).

## 6. One scheduler for all configurations

A command may need several configurations: members with different toolchains,
and host tools built for the build machine. The configurations form a small
dependency graph: a host tool must exist before the build program that uses it
runs, because build programs run while planning.

- **Order.** Configurations are planned and built in that dependency order;
  independent configurations are independent tasks.
- **Concurrency.** Independent configurations run at the same time, each as its
  own `ninja -C <its build dir>`, drawing on one pool so that the total stays
  within the command's `-j`: with ninja 1.13 or later mcpp is the jobserver; with
  ninja 1.12.1, the index's version today, the pool is divided statically.
- The common case, every member in one configuration, is one ninja and needs
  none of this.

## 7. Commands and compatibility

### 7.1 Commands on a workspace

| Command | Behaviour |
|---|---|
| `mcpp build --workspace` | every configuration's graph, all goals |
| `mcpp build -p X` | X's configuration's graph, planned for X and what X reaches |
| `mcpp build` in a member's directory | as `-p X` for that member |
| `mcpp build` at the root of a rooted workspace | as `-p` for the root package |
| `mcpp run -p X`, `mcpp test -p X`, `mcpp pack -p X` | the same graph; the artifact is in X's product directory |
| a package that is not in a workspace | unchanged |

`-p X` plans X and what X reaches, not the whole workspace, in the same build
directory as `--workspace`. Revision 3 planned the whole graph for `-p X`; the
ecosystem review (§12) refuted that: the package index is a virtual workspace
of 172 example members whose CI runs `mcpp test -p <member>` once per member,
so a whole-graph `-p` would resolve and fetch every example's dependencies on
every run, and one member whose planning fails would fail every other
member's `-p`. The build directory is still shared: a node's path and command
are functions of the package, the configuration and the package's features, so
switching between `-p X`, `-p Y` and `--workspace` rebuilds only a package whose
unified features differ between the selections, as in Cargo.

Supporting changes: one fast-path record per configuration, so a workspace
build with nothing to do costs one check per configuration instead of one full
plan per member (§1.1); one compile database per configuration covering every
member (S1); output with one header per configuration and one line per package
compiled.

### 7.2 Compatibility and upgrade

- **Build directories move.** A member's outputs move from
  `<member>/target/...` to `<workspace root>/target/<triple>/<key>/bin/<name>/`.
  The first build after the upgrade is a full build; `mcpp clean --stale`
  removes the old member directories. A script that reads a member's `target/`
  directly reads the product directory instead; `mcpp build --message-format
  json` and `mcpp run -p` name the artifacts.
- **Editing one member's flags no longer rebuilds the others** (§3).
- **E1 is removed.** Its behaviour is subsumed, and it has no user-facing
  interface; its printed line disappears with it.
- **Unchanged:** a package outside a workspace; the build-program protocol; the
  manifest vocabulary; `-p` naming.

## 8. Cross-platform notes

- Paths stay relative to the build directory on every host (principle 1).
- A product directory adds one segment (`bin/<name>/`); long paths are covered
  by the extended-length form the engine already uses on Windows.
- Links: NTFS, ext4, APFS and XFS support hard links; FAT and exFAT, and a
  destination on another volume, fall back to copying.
- A Windows program loads its DLLs from its own directory, which is its
  product directory.
- The jobserver of §6 is a named semaphore on Windows and a FIFO elsewhere in
  ninja 1.13; the static division works everywhere.

## 9. A reusable graph module (`modules/graph`, `mcpp.graph`)

One module over index-addressed directed graphs, with no engine types:

| Operation | Used by |
|---|---|
| stable topological order (ties in input order), with a cycle reported as the path of its nodes | module order (`modgraph`), host-module order, unit order by imports, the configuration scheduler, the build-key fold |
| dependency levels | the configuration scheduler |
| transitive closure and direct neighbours | `build/dep_graph.cppm`, kept as its API and implemented on the module |

The build-key fold in `prepare/plan.cpp` keeps its computation (a value per
package from its dependencies' values, with the local-source taint); what it
takes from the module is the order and the cycle report, so the recursive walk
becomes a loop over that order. The module has its own unit tests (orders, ties,
cycles, levels, closure).

## 10. Outside this design

| Item | Why |
|---|---|
| The parallelism inside an action (`cmake --build --parallel` in a `prepare` action) | an action's resource declaration; it oversubscribes ninja's `-j` today as well |
| Running the build programs of one configuration in parallel while planning | a planning-time optimisation, measurable after this lands |
| Adding ninja 1.13 to the xlings index | an ecosystem step; §6 is correct without it |

## 11. Criteria

| Criterion | Test |
|---|---|
| Each package is compiled once per configuration in a workspace build | e2e: a shared member, counted from the one `.ninja_log` |
| `-p` and `--workspace` share one build directory | e2e: `--workspace`, then `-p X` compiles nothing |
| Units of different members overlap | e2e: a unit of one member starts before another member's units finish (`.ninja_log` start times) |
| An edit to one member's flags recompiles that member only | e2e |
| Members of two configurations both build | e2e: a member with another standard or target row beside a default member |
| Two members with a program of the same name both build, in their own product directories | e2e |
| Two members with the same package name in different namespaces get qualified product directories | e2e |
| A file placed in two product directories shares its storage where links are possible | e2e (inode on POSIX; file index on Windows) |
| `mcpp clean --stale` removes the old member directories | e2e |
| `mcpp.graph` | unit tests |
| A workspace build with nothing to do is one fast-path check per configuration | e2e: the chain of §1.1 with nothing changed prints no toolchain resolution and finishes in well under a second |
| Planning is linear in the members | e2e: the chain of §1.1, one resolution per configuration when something changed |
| The validation project | its Windows CI: `mcpp build --workspace` against 1828 s, the core library compiled once, the Release layout unchanged |

## 12. Self-review

Each angle states what was checked and what changed because of it.

**Architecture.**
- The design has two levels, configurations and packages, and each has one owner:
  the scheduler owns configurations (§6), the planner owns packages (§4). No
  mechanism spans both.
- *Found:* the planner treats `packages[0]` as a root with sources, targets, a
  toolchain pin, hooks and a build program. A virtual root has none of these.
  *Resolution:* the implementation enumerates every read of the root in the
  planner (a table in the implementation record) and states for each whether it
  reads the workspace (toolchain pin, profile, lock) or the members (targets,
  hooks, build programs).
- *Found (revision 4, ecosystem review):* planning the whole graph for `-p X`
  multiplies the cost of every `-p` by the workspace and couples every member
  to the planning of every other; the package index runs `mcpp test -p` once
  for each of its 172 members. *Resolution:* `-p X` plans X's closure in the
  shared build directory (§7.1); the shared state is kept by node identity
  rather than by one `build.ninja`.

**Compatibility.**
- Workspace members' output paths change; this is the one visible break, stated
  in the CHANGELOG with the replacement (§7.2). Single-package projects keep
  every path.
- *Found:* taking per-package flags out of the directory name changes when a
  full rebuild happens. *Resolution:* it only removes rebuilds (§3), and a
  criterion checks that an edited member is rebuilt.
- E1 shipped in 2026.9.28.3 without an interface; removing it breaks no manifest.

**Elegance and simplicity.**
- E1's admission check, nested plans, member stage edges and lock are removed;
  the workspace uses the dependency machinery the planner already has.
- Four hand-written graph algorithms become calls to one module.
- *Found:* revision 1 refused shared program names, a rule that existed only
  because of a layout choice. *Resolution:* product directories remove the case
  instead of guarding it.

**Cross-platform.**
- Relative paths are kept, so the MinGW and MAX_PATH constraints are unaffected.
- Hard links have a copy fallback on every host (§8).
- *Found:* concurrency across configurations needs a jobserver that the index's
  ninja lacks. *Resolution:* static division now, the jobserver when ninja 1.13
  is in the index; the common single-configuration case needs neither.

**Usability.**
- `run`, `test` and `pack` with `-p` work as before; `mcpp build` in a member's
  directory means that member, as in Cargo.
- A workspace build prints one header per configuration and one line per
  package, instead of a header per member and per shared dependency.
- Switching between `-p` targets rebuilds nothing.

**Semantic clarity.**
- Three terms carry the design, each defined once: configuration (§3), node
  (§2), product directory (§5).
- "The directory is the configuration" and "a program's directory is its
  runtime closure" are stated as principles and each has a criterion.
- *Found:* revision 1 called the per-package inputs "removed from the key",
  which read as if they stopped mattering. *Resolution:* §3 names them package
  attributes and states where they go.

## 13. Decisions

| # | Decision | Status |
|---|---|---|
| 1 | The configuration key is the table of §3; the directory name is that key alone | agreed |
| 2 | The workspace build directory is `<workspace root>/target/<triple>/<key>/`; member directories become stale | agreed |
| 3 | Products in `bin/<package name>/`; qualified names when two members share a name; single packages and root packages keep `bin/` | agreed (package name) |
| 4 | Configurations run concurrently under one pool; static division until ninja 1.13 is in the index | agreed |
| 5 | E1 is removed | agreed |
| 6 | `mcpp.graph` holds order, levels and closure; the build-key fold keeps its computation and uses the module's order | clarified |
| 7 | Hard-linked placement with a copy fallback | agreed |
| 8 | No separate patch release: E1's removal and the planning cost land with this design (§14) | agreed |


## 14. The planning cost, fixed at its cause

Revision 3 proposed two steps: a patch release that removed E1 and restored the
planning of 2026.9.28.2, then this design. Both steps address one cause, a
workspace planned as many roots, and the first step would have shipped the
cost of §1.1 (one full plan per member on every command) again. This design
removes the cause in one release:

| Cost | Cause | What removes it |
|---|---|---|
| 2^n plans for a chain of n members (2026.9.28.3) | E1 plans a shared member as the root of a nested build, and the nested build meets E1's condition again | E1 is removed (§7.2); a member is a node of the one graph and is planned once, as every dependency is |
| one full plan per member on every command (2026.9.28.2 and 2026.9.28.3) | the workspace loop plans each member as a root, and the fast-path record belongs to a root | one plan per configuration (§4), and one fast-path record per configuration (§7.1) |
| a shared member compiled once per consumer | each consumer's graph holds its own copy of the member | one node per package per configuration (§2, principle 3) |
| a flag edit in one member rebuilds every member | the directory name hashes every package's flags | the directory name is the configuration key (§3) |

What exists only for E1 is removed with it: the nested plans, the comparison
of build keys between a member's own build and a consumer's graph
(`packageKeys` and `packageKeyInputs` in the plan, which had no other reader),
the stage edges that compared content for units served from a member's
directory (`servedFromMember`), and the member lock. No depth guard and no
per-process memo is added: both would keep a mechanism the design deletes.
e2e 830, which asserted E1's behaviour, is removed; its criterion (a shared
member compiled once) is the first row of §11.

Criteria, beside §11: the chain of §1.1 resolves the toolchain once per
configuration for `--workspace` and for `-p app` when something changed; with
nothing changed it prints no toolchain resolution and finishes in well under
a second. The CHANGELOG entry states the regression, its cause and the one
affected version (2026.9.28.3).

## 15. The virtual root, read by read

The planner treats its root, `packages[0]`, differently from a dependency at
about fifty sites. Each was classified (the inventory of 2026-09-29) as a value
of the workspace, which the virtual root holds, or a duty of the project being
developed, which every selected member now carries. The rules below state the
result; the sites follow them.

**Selection.** Every command on a workspace selects members: `--workspace`, and
a virtual root without `-p`, select every member; `-p X`, and a command run in
X's directory, select X; a command at the root of a rooted workspace selects the
root package. The selection is planned by one mechanism in every case: a
virtual root whose `member` edges name the selected members. A member's node is
therefore the same node, with the same commands, whichever selection reaches
it.

**Configurations.** The selected members are grouped by their effective
root-position values: the toolchain request, `[package] standard`,
`dialect_cxxflags`, `cxx_runtime`, `dependency_linkage`, the target and the
profile, each after workspace inheritance and the command line. Each group is
one plan with one virtual root, whose root-position values are the group's.
Two groups that resolve to one directory (two spellings of one toolchain) are
built one after the other in it.

**The virtual root** has no sources, targets, hooks or build program. It holds
what is one value per plan: the toolchain, the target, the profile, the
root-position keys, the lock, `[build] device_extensions` and the rule-module
set (the union over the members), and the fast-path record.

**A member edge** puts a member in the graph and links nothing into the root,
as an `artifacts` edge does (#711). A member is loaded as a `path` dependency,
so its flags, features, build program and target rows follow the path every
dependency already takes. What a member carries beyond a dependency:

| Duty | Rule |
|---|---|
| targets | each target of a selected member is a link unit of the plan, in the member's product directory (§5); the link unit records its member |
| entry sources | resolved against the member's root, compiled with the member's own flags, as an `artifacts` program's entry is |
| link flags | a member's link unit carries the link flags of the member's closure (the member and what it reaches without an `artifacts` edge), in the order a root build would give them, and not those of other members |
| runtime files | the deploy set of a member's closure is placed in the member's product directory; a shared library of the closure is placed there too, by a link (§5.3) |
| tests | under `mcpp test`, the selected members' `[dev-dependencies]` are loaded as their own, non-transitive, and their test targets are the link units |
| `--features` | applies to each selected member that declares the feature; a feature no selected member declares is refused |
| `[xlings]` entries with `when = "dev"` | installed for a selected member, as for a root |
| `[hooks]` | the hooks of the selected members run around the one build, in member order |
| build program | runs as a dependency's does, with the member's own artifacts directory (`<member>/target/.build-mcpp`, unchanged) and the resolved-graph document a root's program receives |
| graph-wide keys (`cxx_runtime`, `linkage`, `dialect_cxxflags`) | read from the virtual root; a member's value is checked for contradiction, as a dependency's is |

**The lock** is `<workspace root>/mcpp.lock`, which for a rooted workspace is
the file it already has. `--workspace` writes the complete record; `-p X`
updates the entries of X's closure and keeps the others. Where the workspace
lock is absent, a selected member's own lock supplies the anchors (the git
commits and identities), so the first build after the upgrade does not move a
git dependency.

**The fast path.** A record is written per selection and configuration. The
header of `build.ninja` names the selection and the requested features, since
neither moves the directory any longer, and the fast path compares it; the
freshness walk covers the selected members' roots and their path dependencies
instead of the whole workspace tree.

**Commands.** `run`, `test` and `pack` with `-p X` take X's link units by their
member; `pack` reads X's manifest for the package's identity and `[pack]`.

## 16. Tasks and their dependencies

All engine work is one pull request in `mcpp-community/mcpp`, released as one
version. The other repositories change only where the release requires it.

| # | Task | Repository | Depends on |
|---|---|---|---|
| T1 | `modules/graph` (`mcpp.graph`) with unit tests; migrate module order, unit order, host-module order, dependency closure and the build-key fold | mcpp | none |
| T2 | remove E1 (nested plans, member stage edges, member lock, `packageKeys`, e2e 830) | mcpp | none |
| T3 | hard-linked placement of shared libraries, with unit tests | mcpp | none |
| T4 | directory name = configuration key (§3) | mcpp | none |
| T5 | selection and configuration groups; the virtual root and member edges | mcpp | T2 |
| T6 | member duties of §15 (targets, product directories, link flags, runtime files, tests, features, hooks, build programs, lock) | mcpp | T5, T3 |
| T7 | commands: `build`, `run`, `test`, `pack`, `clean --stale`, compile database per configuration, output with one header per configuration | mcpp | T6 |
| T8 | scheduler: groups built concurrently under one `-j` (static division) | mcpp | T5 |
| T9 | fast path per selection and configuration | mcpp | T4, T6 |
| T10 | e2e criteria of §11 and §14; documentation (docs/07 and docs/zh/07, docs/12, the SPECs that name member output paths), CHANGELOG, version | mcpp | T1-T9 |
| T11 | release; GitCode assets mirrored locally as they appear; xim-pkgindex version entry | mcpp, xim-pkgindex | T10 |
| T12 | the package index: CI pin and `latest_mcpp` if required; full sweep | mcpp-index | T11 |
| T13 | the release canaries (xlings, mcppls), both rooted workspaces, and the plugins' CI | xlings, mcppls, mcpp-plugins | T11 |
| T14 | sandbox verification with the CN mirror; issues commented and closed | all | T12, T13 |
| T15 | acceptance on the validation project's pull request: its Windows CI against 1828 s, the core library compiled once, the Release layout | the validation project | T14 |

## 17. Implementation record (2026-09-29)

Implemented in one change on `feat/workspace-build-graph`, released as
2026.9.29.1. Where the implementation settled a question the sections above
left open, the answer is recorded here.

| Question | Answer | Where |
|---|---|---|
| The root of a rooted workspace | Every workspace plan has a virtual root; the workspace's own package is the member `"."`, whose products keep `bin/`. A command at the root selects it alone. The package is then the same node in every selection. | `select_workspace_members` (prepare/manifest.cpp) |
| Which values separate plans | Every root-position value (`root_position_key`): the toolchain and target rows, the standard, the graph-wide `[build]` keys, the profiles, the indices, the capability and tool pins. The virtual root copies them from the group's first member, so a group and its root cannot disagree. | `mcpp.project` |
| A member's build program | Runs after every dependency's program, where a root's runs, with its own artifacts directory and a graph document of its closure in which it is `root`. A workspace member reached as another member's dependency runs there too, so its program sees one environment in every selection. | `step9_member_build_programs` (prepare/target_side.cpp) |
| A member's identity | A path member that declares no namespace keeps its bare name (a root's identity) instead of the default namespace a path dependency receives. | prepare/graph.cpp |
| Per-member link data | Each package's normalised link flags are read when the plan is made; a member's link group holds the plan's flags followed by its closure's, and its closure's runtime contract, and `compute_flags` renders it from a copy of the plan with the group swapped in. The link edge carries the group's `ldflags` and `c_ldflags`. | `make_plan` step 6, `swap_link_group`, ninja_backend.cppm |
| Graph-built shared libraries | Linked once at `bin/`, placed in each product directory whose units load them. | `LinkGroup::placements` |
| Linked placement | Shared libraries only; other deployed files are copied, since a program may write a file beside itself and a link would carry the write to its source. A linked destination is never written in place. | mcpp.build.stage |
| `-p X` and `--workspace` in one directory | `build.ninja` records a request tag (the plan's members and the requested features), and every fast path compares it; one `.build_cache` entry per selection and configuration group. Alternating selections re-plan (one plan of the selection) and recompile nothing. | graph_shape.cppm, execute.cppm |
| The lock | `<workspace>/mcpp.lock`. A plan of all members writes the whole record; a plan of some keeps the other entries, and `--locked` then reports no entry of another member as drift. A selected member's git dependencies are locked as a root's. | prepare/records.cpp |
| Tests | `mcpp test` keeps one plan per member (`-p X` each), in the shared directory; the members' dev-dependencies are their own. | cmd_build.cppm |
| Concurrency | Groups are planned in turn and built on threads with a static share of the jobs; the `.build_cache` write is one locked step. | cmd_build.cppm |
| The orders of `mcpp.graph` | Three: the stable order, the module graph's own (Kahn with the ready units on a stack, over the edges in their recorded order) and a depth-first post-order from roots in the given order. Each migrated site keeps the order it had: the module graph's unit order is the order of the objects on a link line, which Mach-O uses as initializer order, and the first migration to the stable order made openkal's `same-source` example crash at start on aarch64-macos from all three build hosts (CI, run 36486818199). With the orders restored, the link line of that example is byte-identical to 2026.9.28.3's. | modules/graph |
| Module names across members | A graph has one module namespace (BMIs are found by name), so two members that each provide a module of one name are refused in one `--workspace` plan, as two `artifacts` programs are (#732, which tracks per-provider BMI names). Measured over the package index's 172 members: no module name is provided twice. | scanner |

Readings with the implementation (Linux, llvm 22.1.8):

| Reading | 2026.9.28.3 | 2026.9.29.1 |
|---|---|---|
| chain of five libraries and a program, `--workspace`, nothing built | 36.8 s | 0.51 s |
| the same, nothing changed | not replayed | 3 ms |
| `-p app`, nothing changed | not replayed | 3 ms |

mcpp's own repository, a rooted workspace, builds itself with the new engine
(full build, gcc 16.1.0, 115 s), and its module tests run through member plans
(`mcpp test -p graph`, `-p versioning`).
