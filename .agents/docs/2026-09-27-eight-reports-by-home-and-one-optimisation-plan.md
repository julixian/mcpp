---
subject: triage
status: active
---

# Eight reports after 2026.9.27.1: what each one is, where it belongs, and one optimisation plan

**Status:** active, revision 3 (2026-09-27). Every decision is settled (§12).
Nothing described here has been implemented.

- **Revision 1** routed the reports and asked seven questions.
- **Revision 2** recorded the reviewer's first answers:
  - D1 was settled by measurement.
  - D2 and D3 were revised to satisfy both mcpp's specifications and the industry
    norm.
  - Two items the reviewer raised were added: progress (§9.1) and the index floor
    (§9.2).
  - The first self-review was recorded.
- **Revision 3** records the remaining answers and a second self-review (§13.4):
  - The reviewer pointed out that `-p` is declared as `--package <NAME>`, so D1
    resolves the package identity first.
  - D2, D3, D8 and D9 are accepted.

**Basis.** Engine code was read at `b439fd97` (origin/main, mcpp 2026.9.27.1). The
reports cite `52549fbb`, which predates the decomposition of
`src/build/prepare.cppm`; every line number below is that of `b439fd97`.

- A statement marked *measured* was run on Linux x86_64 with released binaries of
  `xim-x-mcpp`, using the reports' own fixtures where they give one:
  - 2026.9.27.1 and 2026.9.26.1 in general;
  - 2026.9.18.1 for the index floor (§9.2).
- A statement marked *read* names a file and a line, and was not executed.
- A statement marked *inferred* is labelled as such.

Nothing was run on Windows or macOS.

## 0. Scope, and the rules applied

The numbers 717 to 725 are eight reports and one release pull request (#719,
merged; it is not a report). One report is excluded: #721 is labelled
`upstream-bug` and is a GCC 16.1 defect, and upstream issues are out of scope for
this round. The remaining seven reports contain eleven separable items, because
#724 carries four and #725 carries two.

Every item is assigned exactly one home, and each assignment states why the cheaper
home does not suffice. The rules applied are the following:

1. **Home, in order of preference.** The order is usage, then a project-local
   plugin, then the official plugin repository, then ecosystem data, then the
   engine. The order filters features, not defects: a silent drop, a lying record
   or a wrong diagnostic is the engine's to fix wherever it appears.
2. **The engine names no tool** (SPEC-007 §0). When a plugin meets a gap that is
   general, the engine closes it with a general mechanism. The plugin does not
   work around it, and the engine does not learn Qt.
3. **No slot for workarounds, and no rule that nothing enforces.** An invariant is
   enforced by construction, by a property test over every row, or by a negative
   test. It is not enforced by a list of exceptions or by a sentence in a document.
4. **A new condition is first tried in the existing condition syntax.** SPEC-004
   §6 requires `[target.<selector>.<section>]` before any new syntax is discussed.
5. **Planning does not change the project** (SPEC-005 R2.1, R2.5).
   `emit build-database` writes nothing into the project and runs no action.

None of the seven reports is a usage question as a whole. Two of them contain a
usage reading, and this record answers it:

- **#720.** A maintainer comment reads the report as a build dependency used by the
  package body. The fixture refutes that reading: `app/main.cpp` imports nothing,
  and only `build.mcpp` imports `repro.rules` (measured).
- **#717.** No current spelling expresses the report's need (measured, §6).

Two further items come from the reviewer rather than from a report: download
progress (§9.1) and the index floor (§9.2). The same rules route them.

## 1. The ledger

| Item | What it is | Kind | Home | Verdict | Order |
|---|---|---|---|---|---|
| #725 | A rooted workspace built as itself carries no workspace context to its path dependencies. Its members are loaded as strangers, and 2026.9.26.1 silently ignored `[workspace.dependencies]` | defect (older than #714; #714 made it loud) | engine | fix | W1 |
| #725 `-p` | `-p, --package <NAME>` promises a package, while the resolvers and docs/07 match only a member's directory. mcppls's members are named `mcppls-base` in `modules/base` | defect (measured) | engine | `-p` resolves the package identity first, and keeps the directory spellings as a fallback | W1 |
| #720 | A host-module package's lib root is compiled before the siblings it imports | defect | engine | fix | W2 |
| #724 §1 | A rule-claimed device source is a C++ compile unit of the plan. It appears in S1 and also in `mcpp build`'s own `compile_commands.json`, and `build.ninja` carries a dead edge for it | defect | engine | fix | W3 |
| #724 A | A failed build program is reported as unclaimed device sources, and the true diagnostic is discarded | defect (SPEC-005 R5.2) | engine | fix | W4 |
| #724 B | `emit` writes `<root>/.mcpp/.xlings.json` into the project | defect (SPEC-005 R2.1) | engine | fix | W5 |
| #723 | Two deploys of one identical generated file are refused at planning | defect: the check compares a proxy (a source path), not content | engine | fix differently from all three proposed options | W6 |
| #722 | Split the longest phase functions | internal | engine | do, in the same round (stage 2) | W7 |
| #717 | `dialect_cxxflags` under a target condition | feature: a grammar gap | engine (manifest) | do; decline the build-program directive | W8 |
| #718 | CRT for the LLVM row on the MSVC ABI | gap: an existing key is not consumed by one row, and the rows' defaults disagree | engine | do with the existing keys; `toolchain-coupled` is the default for every MSVC-ABI row; no new keys; defer the debug CRT | W9 |
| #724 §2 | Describe generated outputs, and give the path the build writes | feature | engine and S1 (mcppls repository) | do, as one record | W10 |
| #724 §2.3 | Run side-effect-free generators under `emit` | feature | not the engine | decline | none |
| review | An index that requires a newer mcpp prints `error: ... [E0006]` at the start of a run that then succeeds | defect (measured) | engine | a closing tip at most, and only when the run refreshed an index | W12 |
| review | Library, git and index acquisitions show no progress, while toolchains do | gap | engine; xlings if its `update_packages` emits no events | one renderer, more producers; non-terminal output without `\r` | W11 |
| #721 | GCC 16.1 ICE | upstream | not in scope for this round | excluded | none |

Everything is one round and one release, in three stages:

- **Stage 1** holds the defects: W1 to W6, and W12.
- **Stage 2** is #722 (W7).
- **Stage 3** holds the features: W8 to W11.

§11 gives the order and its reasons.

## 2. #725: a member is a member however the build is rooted

### 2.1 What was measured

- **2026.9.27.1**, using the report's fixture exactly: `mcpp build` at `root/`
  fails with "no workspace declares 'cmdline'" and exits with 2.
- **2026.9.26.1:** the same build resolves `cmdline → v0.0.2` and finishes.
- **2026.9.26.1 was not correct.** With only `[workspace.dependencies] cmdline =
  "0.0.1"` changed, 2026.9.26.1 still resolves and locks `cmdline@0.0.2`. The
  member's `cmdline.workspace = true` was never merged against the workspace. It
  fell through as an unconstrained dependency, which happened to resolve to the
  version the report used.

So #714 did not break a working feature. It added the refusal that SPEC-004 §9
item 9 requires (`unresolved_workspace_dependency_error`, `src/project.cppm:404`),
and that refusal exposed a gap older than itself. The fix remains first in the
order, because the refusal now blocks builds that used to succeed. Among them is
mcpp-language-server's nightly run against the latest mcpp.

### 2.2 Cause (read)

The workspace context is not set in the rooted-workspace branch:

- `src/build/prepare/manifest.cpp:227-283` sets `state.wsManifest` and
  `state.runtimeWorkspaceRoot` only in the branch that switches to a member. That
  branch is taken for `-p`, or for a virtual workspace.
- A rooted workspace built as itself takes the branch at `:266-268`. That branch
  merges the root's *own* `workspace = true` entries and sets neither field.

The dependency loader then decides membership from exactly those two fields:

- `src/build/prepare/graph.cpp:1999-2003` computes `depIsMember` from them. With
  both fields empty, `depIsMember` is false.
- A member reached through the root package's `[dependencies] a = { path = "a" }`
  is therefore loaded as an ordinary path dependency.
- It receives none of the three inheritances that SPEC-004 §9 item 1 requires:
  `[workspace.package]`, `[workspace.build]` and `x.workspace = true`.

The `depIsMember` block is byte-identical in 2026.9.26.1. The comment at
`graph.cpp:1987` states the intended invariant: "A MEMBER IS A MEMBER HOWEVER IT IS
REACHED".

**Silent twin (inferred from the same fields).** In this position a member also
loses `[workspace.package]` and `[workspace.build]`:

- A member that omits `package.version` because the workspace supplies it is
  refused for a missing version.
- A member receives none of the workspace's build flags.

The report shows only the loudest of these three losses.

**The `-p` symptom is a second defect: the option promises a package and the
resolvers answer a directory** (measured with the layout of mcpp-language-server,
read from its repository).

- **The report's own fixture does not show it.** In that fixture, `mcpp build -p a`
  at the root succeeds, because the member's directory and its package are both
  named `a`.
- **mcppls's layout does show it.** mcppls declares `members =
  ["modules/base", ...]`. Its member packages are named `mcppls-base` and
  `mcppls-platform`, and the root package depends on them under those names.
- **Which spellings work.** In a fixture with this shape, `-p base` and
  `-p modules/base` succeed. `-p mcppls-base` answers "workspace member
  'mcppls-base' not found in [workspace].members".
- **What the interface promises.** Every command declares the option as
  `-p, --package <NAME>`, "the named workspace member" (`src/cli.cppm:377`, `:421`,
  `:521`, `:686`).
- **What the code and docs/07 do instead.** Both resolvers match only a member's
  directory basename or its path string (`src/build/prepare/manifest.cpp:230-243`,
  `src/project.cppm:486-494`). They agree with each other, and no subcommand
  matches a package name. docs/07 §5.3, in both languages, documents that
  directory behaviour.

The option's name, SPEC-001's identity and the root's own `[dependencies]` keys
all name the package. The resolvers and docs/07 name a directory. The fix is
§2.3 item 2.

### 2.3 Fix

1. **The workspace context.** The workspace context is a property of where the
   manifest lives, not of the branch that was taken. When the root manifest has
   `[workspace]`, the context is set before any dependency is loaded, whichever
   branch follows:
   - `wsManifest` is the root manifest.
   - `runtimeWorkspaceRoot` is the root directory.

   The member-switch branch keeps setting the same two values. SPEC-004 §9 item 1
   gains the missing position: "a path dependency of the rooted workspace's own
   package".

   The context carries the inheritance of SPEC-004 §9 item 1 and nothing more.
   `[toolchain]`, `[target.<triple>]` and `[indices]` stay root-position keys
   (§9 item 10), and a member reached by path does not take them. The criterion
   in §2.4 checks this.

   **Compatibility note.** Such members now receive `[workspace.build]` and
   `[workspace.package]` as SPEC-004 §9 item 1 always required. Their compile
   commands can therefore change, and the CHANGELOG says so.
2. **`-p` means the package, as its name says.** The two resolvers become one
   function, and the reviewer confirmed this reading (D1). A value is resolved in
   this order:
   1. **Qualified name.** A value equal to a member's qualified name
      (`<namespace>.<name>`, SPEC-001) selects that member.
   2. **Package name.** Otherwise, a value equal to a member's package name
      selects it. When two members share that name under different namespaces,
      the value is refused, and the message names both qualified names.
   3. **Directory or path.** Otherwise, a value equal to a member's path in
      `[workspace] members`, or to its directory basename, selects that member.
      These are the spellings docs/07 documents today, and they are kept, so a
      script written against them keeps working. Two members can share a
      directory basename, as `apps/core` and `libs/core` do. Today the first such
      member in `members` is selected silently. It stays selected, and a warning
      now names the others and their paths.

   A value can match one member at step 2 and a different member at step 3, for
   example a member whose directory is named like another member's package. The
   step-2 member is selected, because the option names a package. A warning names
   the other member and its path spelling. This changes which member is selected
   in such a layout, and the CHANGELOG says so.

   The "not found" message lists every member with its package name and its path.
   docs/07 §5.3, in both languages, and the option's help text state the order.

### 2.4 Criterion

The criterion is an e2e test on a rooted workspace:

- The root has both `[workspace]` and `[package]`.
- The root package reaches member `a` by `path`.
- `a` uses `x.workspace = true` for a dependency pinned in
  `[workspace.dependencies]` to a version that is not the latest.
- `a` omits `package.version`, and `[workspace.package]` supplies it.
- The workspace declares a `[toolchain]` that `a`'s standalone build would not
  choose.
- A plain `mcpp build` locks the pinned version.
- A plain `mcpp build` compiles `a` with the root's toolchain, and `a`'s commands
  carry `[workspace.build]`'s flags.

The non-latest pin also catches the accidental pass seen in 2026.9.26.1. The test
fails on both 2026.9.26.1 and 2026.9.27.1. Existing coverage misses the shape:
e2e 770 builds a member through `-p` on a virtual workspace, and the unit test only
formats the error.

For `-p`, the same fixture gives member `a` the directory `modules/base` and the
package name `ws-base`:

- `-p ws-base`, `-p base` and `-p modules/base` select the same member.
- Suppose a second member's directory basename is `ws-base`. Then `-p ws-base`
  selects the package `ws-base` and warns, naming the second member's path.
- Two members named `ws-common` under different namespaces make `-p ws-common`
  refused. Each qualified name selects its own member.

`-p ws-base` fails on 2026.9.27.1.

## 3. #720: the lib root is a node of its package's order

### 3.1 What was measured

The report's fixture, run on Linux with 2026.9.27.1, gives `fatal error: module
'repro.helper' not found`. Changing only `lib.path` to `src/helper.cppm` makes the
build pass. The ordering does not depend on the platform.

### 3.2 Cause (read)

- `src/build/prepare/features.cpp:976-1093` orders a host-module package's units.
  It resolves the lib root (`:992`) and pushes it first (`:995`) before reading
  any of its imports.
- It sorts only the *other* units topologically (`:1032-1089`, excluding the root
  at `:1040`) and appends them after the root. The comment at `:1051` states the
  assumption: "the lib root [is] already ahead of every entry here".
- `src/build/build_program.cppm:1385-1425` compiles the list in order, and each
  unit sees only the BMIs of the units ahead of it.

The topological sort came from `d4a83244` (2026.9.8.1, #589). That change replaced
an alphabetical order, and the old order had made mcpp-plugins fold everything into
its lib roots. The change took "the root is first" as an axiom. The axiom holds
until a lib root imports a sibling.

### 3.3 Fix, silent twins and criterion

- **Fix.** The root is an ordinary node of the same sort. Its name and its imports
  are read the way the other units' are, and the existing diagnostic for a missing
  lib root is unchanged. With no import edges from the root, the order is
  byte-for-byte today's, because the sort keeps the original order as its stable
  order.
- **Silent twins.** None were found. Ordinary dependency builds order units through
  the scanner and ninja, `emit` does not compile host modules, and host tools are
  full sub-builds.
- **Criterion.** An e2e test uses the report's fixture, with an explicit `lib.path`
  and with the conventional lib root. It fails on 2026.9.27.1.
- **Ecosystem follow-up.** This is optional and not required. mcpp-plugins may
  split the monolithic lib roots of its rule packages, which were grown to work
  around the old order.

## 4. #724: the build database and rule-generated files

### 4.1 §1: a device source is not a compile unit

**Facts (read; measured on the fixture of `examples/12-a-new-device-language`):**

- `classify()` gives `SourceKind::Device` to an extension a rule claims
  (`src/build/prepare/features.cpp:590-645`, `:765-780`).
- `src/build/plan.cppm:1848-1880` turns *every* graph unit into a `CompileUnit`,
  including device units.
- `is_implementation_source` (`plan.cppm:741`) keeps device units out of link
  inputs. Even so, `build.ninja` still carries a `cxx_object` edge for the device
  file, and nothing references that edge (measured).
- `unit_invocations` (`src/build/compile_commands.cppm:433`) excludes NASM units
  only. It feeds both the S1 document (`build_database.cppm:369`) and `mcpp
  build`'s own `compile_commands.json` (`compile_commands.cppm:505`). Both list the
  device source with `g++ … -c <file>.toy` (measured).

The defect is therefore not specific to `emit`. The plan states that a device
source is a C++ compile unit, and three consumers read the plan.

**Fix.** The plan does not make a device-kind source a compile unit. This is
decided once, where units become `CompileUnit`s, so that ninja, the compile
database and S1 agree without each needing its own filter. The source remains in
`watch`, because the sources glob matches it. SPEC-005 R3.7 names device sources
beside NASM units.

**Criterion.** A fixture with a device source is used:

- `build.ninja` has no `cxx_object` edge whose input is the device source.
- Neither `compile_commands.json` nor the S1 document lists it.

The criterion fails on 2026.9.27.1.

### 4.2 Side finding A: the true diagnostic is discarded

**Facts (read):**

- When the build program fails, `src/build/prepare/target_side.cpp:1611-1625` does
  two things:
  - records `MCPP_BUILD_DATABASE_PROGRAM_FAILED` in `planNotes`;
  - applies none of the program's directives.
- The device-source check (`:1767-1805`) runs anyway, for every package. With no
  actions recorded, every device source is reported as orphaned, and the check
  returns an error.
- `phase9_target_side` fails, so `prepare/driver.cpp:44` returns before `phase13`
  copies `planNotes`. The recorded diagnostic is lost.
- `cmd_build.cppm:402-408` then reports `MCPP_BUILD_DATABASE_PLAN_FAILED` with the
  orphan text.
- `hasProgram` (`:1784`) is true when a `build.mcpp` exists. The text therefore
  says "`build.mcpp` ran" about a program that never ran successfully.

SPEC-005 R5.2 already specifies the intended behaviour. A package whose build
program failed is described without that program's directives, and it carries
`PROGRAM_FAILED`.

**Fix.**

1. A check whose premise is a build program's directives does not run for a
   package whose program failed in this pass. The package is then described as
   R5.2 says, and the member keeps its one error diagnostic, `PROGRAM_FAILED`. It
   is not failed for a symptom of that error.
2. Notes recorded before a phase fails are carried on the failure path. Today they
   are lost on *every* failure after phase 9, not only on this one.
3. `hasProgram` means that the program ran and succeeded.

**Criterion.** The fixture is a package that declares a device source and whose
`build.mcpp` does not compile:

- `emit` reports `PROGRAM_FAILED`, with `path` set to `build.mcpp`.
- `emit` mentions no device source.
- The package is described.

The criterion fails on 2026.9.27.1.

### 4.3 Side finding B: `emit` writes into the project

**Facts (read):**

- `ensure_project_index_dir` (`src/build/config.cppm:929-975`) writes
  `<dir>/.mcpp/.xlings.json` when the project declares `[xlings]` payloads or
  custom repositories.
- The call site (`src/build/prepare/xlings.cpp:432-441`) uses the private work
  root only when `runtimeSelection.ownerRoot == workRoot`.
- `ownerRoot` is the project root regardless of `emit`'s private `work_dir`
  (`src/xlings/runtime_selection.cppm:51`). A project with `[xlings] deps`, which
  every rules-qt consumer has, is therefore written to during planning.

The reporter measured this behaviour. This record did not, because the effect
needs a fixture with payloads.

**Why the criterion missed it.** e2e 688 asserts R2.1 with a digest of the project
tree. Its fixture declares no `[xlings]` payloads, so the branch that writes is
never taken. The criterion lacked the case that mattered.

**Fix and criterion.**

- Under a private `work_dir`, the file is written where the sibling branch already
  writes it: the private root.
- e2e 688's tree digest is repeated on a fixture with `[xlings] deps`. A stub
  xlings, as in e2e 733, is enough and needs no network.

### 4.4 §2: generated outputs

**Facts (read and measured):**

- The plan holds each action's `id`, `role`, argv, inputs and outputs
  (`modules/manifest/src/types.cppm:487-551`). The S1 document carries none of
  them.
- `emit` plans under `$MCPP_HOME/cache/build-database/<key>`
  (`cmd_build.cppm:396`, `prepare/manifest.cpp:344`), as R2.1 requires, and action
  outputs resolve there.
- `prepare_actions` (`modules/buildmcpp/src/directives.cppm:1324-1384`) writes an
  empty placeholder for a compilable output that does not exist yet, under both
  `build` and `emit`. A header gets no placeholder.
  - Under `emit`, `moc_*.cpp` is therefore 0 bytes and `ui_*.h` is absent.
  - The TU entries for the placeholders point at empty files.
- One generated file has two paths (measured): `<root>/target/.build-mcpp/out/…`
  under `build`, and `<cache>/target/.build-mcpp/out/…` under `emit`. The #699
  design fixed these path spaces (D2, D6) and did not treat generated outputs, so
  this is a new gap, not a reversal.

**The three asks:**

1. **Describe the generated outputs: do.** This is general, the data exists, and
   it names no tool.
2. **Say where a build put them: do, but as a fact of the plan rather than of the
   filesystem.** The record gives the path that `mcpp build` of the same
   configuration writes, whether or not the file exists now.
   - A statement of existence would be stale the moment the user builds, and
     `emit`'s `watch` set does not cover `target/`.
   - The consumer checks existence itself and watches the path.
   - The mapping from the private tree to the project's tree then stays inside
     mcpp, which was the reporter's concern.
3. **Run "side-effect-free" generators under `emit`: decline.**
   - R2.5 exists so that planning cannot fail on construction, and cannot be slowed
     by it.
   - A per-action purity claim is a promise the engine cannot verify, so it would
     be a rule that nothing enforces.
   - The generators are payloads that may not be installed at planning time.
     SPEC-007 R8.3's criterion is a plan made without the payloads.
   - Because the record in ask 1 carries the argv, a consumer that wants generated
     files for a project that was never built can run the command itself into its
     own cache, with its user's consent. The decision stays with the party that
     owns the consent.

**Proposal (W10).**

- **One record for each action output that a set can see.** This covers
  source-role outputs and every include directory under the output tree. Each
  record carries:
  - the planned path and the path the build writes;
  - the action `id`, the role, the inputs and the argv;
  - whether the output is a compilable source, a header or a directory.
- **Where the shape is defined.** The shape is written into S1, in the mcppls
  repository, as an addition to profile 0.2.0. S1-11.2-1 lets S1 consumers ignore
  unknown fields.
- **Where it does not go.** `compile_commands.json` must not carry the record:
  clangd and clang-tidy reject a database that contains one unknown key (measured
  2026-09-26 during the #699 design).
- **Placeholders.** A TU whose source is a placeholder is identified as generated
  through the record. No new `ide.role` is introduced.

**Criterion (W10).** The fixture is the device-language example
(`examples/12-a-new-device-language`), extended with an action that generates a
header into an include directory:

- The S1 document from `emit` carries a record for the header. The record gives
  the planned path, the build's path, the action `id`, the inputs and the argv.
- A following `mcpp build` writes the header at exactly the build's path.
- `compile_commands.json` carries no such field.

The first check fails on 2026.9.27.1, which emits no record.

## 5. #723: one destination, one content

### 5.1 Facts (read)

- `resolve_runtime_contract` (`src/build/plan.cppm:877-996`) merges every package's
  deploy entries into one list for the whole plan.
- `add_deploy` (`plan.cppm:1436-1456`) refuses two different normalised source
  *paths* for one destination.
- Deploys become `stage_file` edges (`src/build/ninja_backend.cppm:2978`), and
  those edges run `mcpp stage`. `mcpp stage` already compares bytes (`same_content`
  in `src/build/stage.cppm`) so that it skips identical writes.
- `mcpp pack` reads the placed files by destination (`src/pack/pipeline.cppm:470`),
  after the build.

The collision is therefore not specific to `artifacts` (inferred from the merge).
Any two packages of one graph whose build programs deploy one generated file name
collide in the same way. An example is a Qt library dependency and its Qt consumer
when both ask rules-qt for translations.

### 5.2 The invariant, and the four options

The invariant is that a destination holds one content. The present check tests a
proxy for it, one source path, because at planning time a generated source has no
content yet.

| Option | What it gets wrong | Verdict |
|---|---|---|
| 1. The consumer takes precedence, with a note | When the contents differ, the artifact program runs with a file it was not built for. A note is then the only record | reject |
| 2. Actions with equal commands and inputs produce the same file | A heuristic about tools. A tool whose output depends on its output path or working directory breaks it, and the engine cannot know which tools do. It also covers only pairs of actions | reject |
| 3. A per-edge exclusion list | A slot for exceptions. The author must know which files collide, and a list written today hides a real divergence tomorrow | reject |
| 4. Check the invariant where the contents exist | none of the above | **adopt** |

Option 4 works as follows:

- Two or more source paths for one destination become one `stage_file` edge, with
  every source as an input.
- `mcpp stage` places the file when all sources are byte-identical, using the
  comparison it already implements.
- Otherwise it fails, naming every source and the destination.

The plan no longer refuses at planning. `emit` no longer fails on the collision,
since it runs no deploy.

**One destination, one writer (found in self-review).** Three mechanisms write
into a program's directory today, and only the first goes through `add_deploy`:

1. **Declared deploys.** These are `[runtime] deploy` and the `deploy` directive.
2. **The toolchain's runtime DLLs under `toolchain-coupled`.** These are staged by
   `flags.cppm:1515-1560`, which lets a declared deploy of the same name win, with
   a diagnostic.
3. **R4.3's placement after a PE link.** It reads the program's import table and
   copies DLLs from the runtime search directories (`ninja_backend.cppm:1917-1937`,
   `pack.cppm:1378`). It never consults the deploy list, so a DLL of the same name
   can be written by a second, unrelated edge.

W6 makes the merged deploy list the single authority for a destination:

- **Declared deploys.** Among them, the content check above applies.
- **Toolchain runtime DLLs.** A declared deploy keeps outranking them, with the
  existing diagnostic, because an explicit statement outranks a derived default.
- **R4.3 placement.** It skips a name that the list already places. It compares
  the contents and reports a difference, rather than writing over the file.

W9's default staging of the redistributable (§7.3) depends on this rule. Without
it, a plugin's runtime search directory that ships its own `vcruntime140.dll`
would race with the staged copy.

### 5.3 Cost and criterion

- **Code.** `DeployFile` carries a list of sources; `mcpp stage` accepts several;
  `add_deploy` merges instead of refusing; the edge lists every source. Consumers
  keep keying on the destination.
- **Plugin option.** rules-qt could deploy `qtbase_<lang>.qm` straight from the SDK
  when one catalog suffices, which also removes an `lconvert` action. This is an
  optimisation of the plugin, not the answer, because two independent plugins can
  produce the same file.
- **Criterion.**
  - Setup: two packages of one graph each generate the same bytes into their own
    output directory and deploy them to the same name.
  - Expected: the build succeeds and one file is placed.
  - Control with different bytes: the build fails, naming both sources.
  - The criterion fails on 2026.9.27.1 at planning.
- **Criterion for one writer (Windows leg).**
  - Setup: a PE program's runtime search directory holds a DLL whose name the
    deploy list also places, with different bytes.
  - Expected: after the build the program's directory holds the listed file, and
    the build reports the difference.
  - On 2026.9.27.1 the file present depends on which edge ran last.

## 6. #717: a graph-wide flag under a target condition

### 6.1 Facts

- **Measured:** `[target.linux.build] dialect_cxxflags = ["-DX717"]` produces
  "unsupported key 'dialect_cxxflags' (ignored)", and the flag reaches no command.
- **`BuildInputs` versus `dialectCxxflags` (read):**
  - `BuildInputs` (`modules/manifest/src/types.cppm:342`) holds the additive,
    per-package inputs that a condition may carry.
  - `dialectCxxflags` (`types.cppm:932`) is graph-wide. Only the root's value is
    read (`src/build/prepare/scan.cpp:202-216`).
  - It reaches the std BMI and the scan (`target_side.cpp:1923-1928`), the
    translation units (`plan.cppm:1393`) and the fingerprint
    (`prepare_inputs.cppm:568-571`, `cache_key.cppm:323`).
  - `[workspace.build] dialect_cxxflags` is prepended into the member being
    built, and into every member pulled in as a path dependency
    (`inherit_workspace_build`, `src/project.cppm:327`).
- **A dependency's own value (read).** A dependency's `[build] dialect_cxxflags`
  is parsed and enters that dependency's fingerprint (`prepare_inputs.cppm:663`),
  but it reaches no command.
- **Precedents (read).**
  - `[target.<pred>.abi]` for `threads` and `exceptions` is a graph-wide switch
    under a condition, with only the root's value rendered (`toml.cppm:3476-3501`).
  - `[target.<triple>] cxx_runtime` and `linkage` are a second precedent.
- **Promotion list (read).** `-fms-runtime-lib` is not in the promotion list
  (`types.cppm:2310-2325`), and that is correct.

### 6.2 Assessment and design

The need is general: a graph-wide switch that exists only on some targets. No
current spelling expresses it, so its home is the manifest grammar.

SPEC-004 §3.1 and §6 fix the spelling. The key lives in `[build]`, so its
conditional form is `[target.<selector>.build] dialect_cxxflags`. The engine keeps
`BuildInputs` separate from graph-wide keys. The conditional section is parsed into
two destinations:

- the package's additive inputs;
- a conditional graph-wide list.

The rules for the graph-wide list are these:

- **Who contributes.** Only the root of the build contributes: the command's
  package, or the member that `-p` selects. The order is `[workspace.build]`, the
  root's `[build]`, then each matching `[target.<selector>.build]` in manifest
  order. Entries are appended, as `cxxflags` are.
- **Which target decides.** The resolved target decides, including a host build's
  host row (SPEC-004 §4.6). A build program and a host tool are sub-builds with
  their own root, so the program's host std BMI is not changed after the fact,
  which the report requires.
- **Dependencies.** A dependency's graph-wide keys still reach no command. SPEC-004
  §9 item 10 adds `dialect_cxxflags`, with its conditional form, to the
  root-position keys. This makes today's behaviour a stated rule. There is no
  diagnostic, as for `[toolchain]`: a dependency legitimately declares these keys
  for its own builds as a root.
- **Caching.** The resolved list enters the fingerprint and the std BMI key through
  the variable already used.
- **A key enters a fingerprint only where it reaches a command (found in
  self-review).**
  - **Today.** A dependency's own `dialect_cxxflags` enters that dependency's
    fingerprint (`prepare_inputs.cppm:663`) although it reaches no command.
    `inherit_workspace_build` (`src/project.cppm:327`) also prepends
    `[workspace.build] dialect_cxxflags` into every member pulled in as a path
    dependency. W1 therefore makes more members carry an inert value, and each
    such member would be rebuilt once for nothing.
  - **Change.** W8 removes a package's own graph-wide keys from its fingerprint
    contribution. Every package's cache key already carries the resolved
    graph-wide list (`language.dialect_flags`, `cache_key.cppm:323`), and that
    list is what reaches the commands.

**The build-program directive (`mcpp::dialect_cxxflag`): decline.**

- The condition is a target predicate, which the manifest states as data. A program
  earns its place only when the decision needs something only a program can
  compute, and no such need is shown.
- A directive would give a build program authority over the std BMI and over every
  package's translation units, and would need an authority rule for dependency
  programs.
- The report leaves this choice to the maintainers.

After W9, the report's motivating flag is no longer written by the project at all.
W8 remains useful for other target-specific graph-wide flags.

**Criterion.** On a Linux host:

- `[target.linux.build] dialect_cxxflags` reaches the std BMI, scan and TU
  commands.
- `[target.windows.build]` reaches none of them.
- Switching between the two rebuilds the std BMI (A, then B, then A).

The criterion fails on 2026.9.27.1.

## 7. #718: the CRT on the MSVC ABI

### 7.1 Facts (read; the closure comment of #649 E10)

- **Which dialect a row gets.** `dialect_for` returns the MSVC dialect only for cl
  (`modules/toolchain-model/src/dialect.cppm:260`). The LLVM row, which is clang++
  targeting `*-windows-msvc`, gets the GNU dialect.
- **What each row emits.** The CRT block (`src/build/flags.cppm:1045-1053`, and its
  mirror for the std module in `prepare/scan.cpp:690-702`) emits nothing for the
  LLVM row, so clang links `libcmt` by default. cl gets `/MD` by default and `/MT`
  on request.
- **The shared derivation.** `msvc_wants_static_crt(linkage, cxxRuntime)`
  (`dialect.cppm:160`) is the single derivation of #422. The translation units and
  the std BMI both use it.
- **What E10 records.** E10 (`src/build/distribution.cppm:725-742`) records the
  LLVM row as `self-contained`. It downgrades an explicit request for any other
  value, with a warning that names `msvc@system`.
- **Rows mcpp drives.** mcpp drives no clang-cl row, so the report's clang-cl
  column has nothing to apply to.
- **Debug CRT.** No debug CRT exists anywhere.
- **A flag given only at compile time does not reach the link.** This was measured
  for the E10 record (`2026-09-16-646-649-four-issues-by-home.md` §4.5). With
  `-fms-runtime-lib=dll` at compile time, the objects carry
  `--dependent-lib=msvcrt`, yet the clang driver's link step still passes
  `-defaultlib:libcmt`. The clang driver chooses the CRT separately at compile time
  and at link time.

### 7.2 The default: two questions, and the norm that answers each

A default CRT answers two questions that mcpp's vocabulary already separates:

1. **Which CRT is compiled against.** Static or dynamic; this is a matter of ABI.
2. **Where the DLLs come from at run time.** This is a matter of deployment.

**The industry norm answers the first question with the dynamic CRT.** The
compiler drivers default to the static CRT when no flag is given. Every
mainstream build system and package manager for Windows overrides that and
defaults to the dynamic CRT:

| Party | Default |
|---|---|
| cl.exe, clang-cl, the clang++ driver, with no flag | static (`/MT`, `libcmt`) |
| Visual Studio project templates | `/MD` (Release), `/MDd` (Debug) |
| CMake 3.15+ (policy CMP0091) | `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`, which gives `/MD`, and `/MDd` for Debug |
| Meson (`b_vscrt = from_buildtype`) | `/MD`, and `/MDd` for the debug build type |
| Cargo and rustc (`*-pc-windows-msvc`) | dynamic; `+crt-static` opts into static |
| vcpkg's default triplet `x64-windows`; Conan's `compiler.runtime` | dynamic |
| Qt's official binaries | `/MD` |

The reasons are structural, not a matter of taste:

- **Every object in an image must agree.** Every object and prebuilt library linked
  into one image must use the same CRT: the `RuntimeLibrary` mismatch check fails
  the link with LNK2038. The prebuilt ecosystem ships `/MD`, which is the case of
  GalTranslPP with Qt and vcpkg.
- **Each `/MT` image has its own CRT state.** Under `/MT`, each DLL carries its own
  heap, `FILE*` table, `errno` and locale. Memory or CRT objects that cross a DLL
  boundary are therefore unsound. A program made of several images needs one
  process-wide CRT. Examples are `dependency_linkage = "shared"` and plugins
  loaded at run time.

**mcpp's own norm answers the second question.** `cxx_runtime` defaults to
`self-contained`, "portable by default": a built artifact runs on a machine where
nothing was installed (docs/20). On the MSVC ABI, mcpp's three values already
split the two questions:

| Value | CRT | Run time |
|---|---|---|
| `self-contained` | `/MT` | nothing outside the image |
| `toolchain-coupled` | `/MD` | the toolset's own `vcruntime140*.dll` and `msvcp140*.dll` are staged beside the artifact |
| `host-coupled` | `/MD` | the target has the redistributable installed |

`ucrtbase.dll` is a component of Windows 10 and later. Microsoft permits
app-local deployment of the redistributable files from `VC\Redist`. A
`toolchain-coupled` artifact therefore runs on a clean Windows 10+ machine.

**The answer: `toolchain-coupled` is the default for every row whose target is
the MSVC ABI.** This covers cl and clang++ alike, because the CRT is a property of
the target ABI and not of the compiler. The default satisfies both norms: the
dynamic CRT that the ecosystem is built against, and an artifact that runs
without an installer.

The choice keeps the existing shape of mcpp's defaults: docs/20 already lets the
default of the shared-library role depend on the hazard of the target format.
Here the default depends on the ABI, because the hazard of a CRT per image is
the ABI's.

No new key is added. `linkage` and `cxx_runtime` state static versus dynamic, and
also where the DLLs come from, which the proposed `msvc_crt_linkage` would not. A
second key would be a second spelling of one fact.

**Two deliberate departures from the industry norm, each with its reason:**

1. **The redistributable is staged by default.** The industry relies on an
   installer or a central redistributable instead. mcpp's promise is that the
   built directory runs as it is. The cost is a few DLLs, about 1 to 2 MB, beside
   each program. `host-coupled` removes them.
2. **The dev profile does not select the debug CRT.** CMake, Meson and Visual
   Studio select `/MDd` for Debug. mcpp's dev profile states debug information,
   not a different ABI:
   - The debug CRT changes `_ITERATOR_DEBUG_LEVEL` for every prebuilt library.
   - Its DLLs may not be redistributed.
   - The report itself asks that `debug = true` not imply it.

   The debug CRT stays deferred, as an opt-in axis to be designed when a consumer
   needs it.

### 7.3 Change (W9)

- **Which rows receive the model, and where it reaches.** Every MSVC-ABI row
  receives the CRT model:
  - cl spells it `/MT` or `/MD`;
  - clang++ spells it `-fms-runtime-lib=static` or `-fms-runtime-lib=dll`.

  The word reaches the translation units, the std and std.compat BMIs, and the
  link command, because of the fact in §7.1. It comes from one helper, and it
  enters the fingerprint and the std BMI key. MinGW (`*-windows-gnu`) is not the
  MSVC ABI and is unaffected.
- **Default.** The undeclared contract on the MSVC ABI resolves to
  `toolchain-coupled` for every role. The contract is whole-project on this ABI:
  docs/20 already refuses a per-role override there. docs/20's per-format table today
  says only "PE (Windows)" for its `self-contained` shared-library default. It is
  amended to say that this row applies to the GNU ABI (MinGW). With this change the record and the flags agree: today the cl row's
  default records `host-coupled` while emitting `/MD`.
- **Explicit values.**
  - `self-contained`, or `linkage = "static"`: `/MT`.
  - `host-coupled`: `/MD` without staging.
  - `toolchain-coupled`: `/MD` with staging.

  `msvc_wants_static_crt` keeps its inputs.
- **What is staged, and from where.** The existing mechanism is used, and no
  second one is added.
  - **The mechanism.** The planning-time `toolchain-coupled` staging
    (`flags.cppm:1515-1560`) produces `stage_file` edges. The same directory is put
    on the `mcpp run` and `mcpp test` search path.
  - **The source.** The source directory is the resolved toolset's
    `VC\Redist\MSVC\<version>\<arch>\Microsoft.VC*.CRT`. It is carried as its own
    toolchain field.
    - cl's `linkRuntimeDirs` holds exactly that directory today
      (`src/toolchain/msvc.cppm:1573`).
    - On the LLVM row the field comes from the row's `sysroot` resolution. The
      LLVM row's `linkRuntimeDirs` holds LLVM's own directories
      (`src/toolchain/clang.cppm:190`), so copying it would stage the wrong files.
  - **The gate.** The staging gate at `flags.cppm:1522` asks whether the target is
    the MSVC ABI and the toolset has a redistributable directory. Today it asks
    whether the compiler is cl.
- **A row whose toolset has no redistributable directory.** Whether `xim:msvc`
  carries one is yet to be measured. On such a row the default is `host-coupled`,
  and `resolution.json` records it. It is a property of the row, stated once in
  docs/20, not a warning on every build. An explicit `toolchain-coupled` on such a row
  is refused, naming the missing directory. It is never downgraded: an explicit
  statement that cannot be met is an error, as in the pack rule below.
- **`mcpp pack`.** The default mode (`vendored`) carries the staged DLLs.
  - An explicit `--mode system` resolves a *defaulted* contract to `host-coupled`:
    an explicit choice outranks a default.
  - Only an explicit `toolchain-coupled` together with `--mode system` is refused,
    as today.
- **E10.** Every MSVC-ABI row now emits a CRT model, so E10's degrade path applies
  to no row and is removed. A value that a row cannot deliver is refused, as
  stated above. e2e 703 is inverted.
- **Free-form CRT words (D3).** Every MSVC-ABI build now states its CRT, so a CRT
  word in `cxxflags` or `dialect_cxxflags` (`-fms-runtime-lib=*`, `/MD`, `/MT`,
  `/MDd`, `/MTd`) is always a second statement. The engine never lets the last
  word win.
  - A word that agrees with the resolved model is warned as redundant.
    GalTranslPP's `-fms-runtime-lib=dll` therefore keeps building.
  - A word that disagrees is refused. The message names the word, the key, and
    the value that corresponds to the word.
- **Upgrade.**
  - **cl-row projects.** Their compile commands are unchanged; programs gain the
    staged DLLs.
  - **LLVM-row programs.** They move from the static to the dynamic CRT, and
    their std BMI is rebuilt once. A project that links `/MT` prebuilt libraries
    fails with LNK2038 and states `cxx_runtime = "self-contained"`.
  - **Announcement.** The CHANGELOG and docs/20 carry an "Upgrading" note, as
    docs/20 already does for the 2026.8.16 change.
- **Debug CRT.** Deferred, as stated in §7.2.

**Criteria.**

- **Unit property.** It covers every MSVC-ABI row × {undeclared, `self-contained`,
  `toolchain-coupled`, `host-coupled`, `linkage = "static"`}. Each combination
  yields exactly one CRT word, spelled for its driver. The word is equal in the TU,
  the std BMI and the link command.
- **Windows leg: imports and staging.**
  - The default LLVM-row program imports `vcruntime140.dll`, and the file is
    staged beside it.
  - The program runs from the build directory with the Visual Studio directories
    removed from `PATH`.
  - `self-contained` imports none of these DLLs.
- **Windows leg: switching.** Switching between the default and `self-contained`
  rebuilds the std BMI (A, then B, then A).
- **Windows leg: pack.** `mcpp pack` in its default mode includes the DLLs.
  `--mode system` succeeds and records `host-coupled`.

## 8. #722: phase functions

The item is internal, so its home is the engine repository, and it is done in
this round as stage 2 (D7). It follows stage 1, because those fixes touch `manifest.cpp`, `graph.cpp`,
`features.cpp`, `target_side.cpp` and `plan.cpp`. A mechanical split first would
turn each defect fix into a rebase. A split afterwards leaves each fix a small,
reviewable diff, and the features W8 to W11 then land in the smaller functions.

The criteria the report states apply unchanged:

- the golden fixtures stay byte-identical;
- AddressSanitizer runs with `detect_stack_use_after_return=1`;
- no new interface unit enters `mcpp.build.prepare`'s import chain.

The proposed gate on function length is adopted only in a form that parses:
clang-tidy `readability-function-size`, run over a compile database that mcpp
produces for its own LLVM leg. A line-counting script over brace heuristics is a
substring criterion. The gate is part of W7's acceptance, and it fails before
the split, because functions of 1,000 to 2,300 lines exist. If the parsing form
cannot be built, the function limit is removed from the acceptance. It is not kept
as a sentence that nothing checks (rule 3). The file gate stays in either case.

## 9. Two items raised in review

These two items do not come from a report. The reviewer raised them on
2026-09-27, and they are routed by the same rules as the reports.

### 9.1 One progress mechanism for every acquisition (W11)

**The observation.** A toolchain download shows progress. A download of a library
or of an xlings dependency appears to show none.

**Read.** One producer and one renderer already exist, and most paths use them:

- **The producer.** `xlings interface install_packages` streams NDJSON
  `download_progress` events. xlings builds these events from the same
  `DownloadProgressRenderer` that draws its own bars
  (`openxlings/xlings src/core/xim/commands.cpp:811-833`).
- **The renderer.** mcpp parses the events in `InstallProgressHandler` and draws
  them with `ui::DownloadProgress` (`src/fetcher/progress.cppm:208-263`,
  `src/ui.cppm:148-162`, `:623-682`).

The following table lists every path by which mcpp acquires remote content:

| Path | Progress today |
|---|---|
| Toolchain, runtime payload (glibc, openkal), host tools (`resolve_xpkg_path`) | the shared bar |
| Library packages from the index (`graph_load.cpp:848-877`, global and project scope) | the shared bar |
| `[xlings]` and `[feature-xlings]` payloads (`prepare/fetch.cpp:186-471`) | the shared bar |
| Index refresh: automatic (`refresh_policy.cppm:161-164`, `xlings.cppm:2168-2171`), and first use of a custom index (`prepare/xlings.cpp:558-560`) | one static status line, then silence: the bare `xlings update` CLI runs with `quiet = true` |
| Explicit `mcpp index update` | xlings's unstructured text, reprinted line by line |
| `git` dependencies (`fetch.cpp:127-140`, `graph.cpp:1911-1944`) | none: the output is captured whole and shown only on failure |
| The sandbox bootstrap (`xlings.cppm:1584-1738`) | a spinner on a TTY; the direct `xlings install` output is discarded |

**Reading of the observation (inferred, to be measured).** On the paths read,
library and payload installs do draw the shared bar. The silence the reviewer saw
most likely has one of three sources:

- the index refresh that precedes a library's first resolution, which can take
  many seconds and shows one static line;
- a `git` dependency;
- a payload whose install downloads through a channel that emits no
  `download_progress` event.

W11 therefore starts with a measurement: one cold run of every row of the table
above, recording what the terminal shows. The measurement also confirms that the
rows marked as sharing the bar really draw it.

**A defect found on the way (read).** `ProgressBar` writes `\r` and ANSI erase
sequences whether or not the stream is a terminal (`src/ui.cppm:440-475`); only
colour is gated. CI logs therefore collect one line per repaint. The bootstrap
spinner is the one place that gates on `is_tty()` (`xlings.cppm:1621`).

**Checked, not a defect.** `emit build-database` redirects mcpp's stdout to
stderr during planning (`StdoutToStderr`, `src/cli/cmd_build.cppm:377`). A bar
drawn during planning therefore cannot corrupt the document on stdout (SPEC-005
R1.3).

**Design.** The design adds no second UI. Every acquisition becomes a producer of
the one event shape that `ui::DownloadProgress` already renders: an item, a label,
bytes done and bytes total, or a phase percentage when bytes are unknown.

| Path | Producer | Home |
|---|---|---|
| Index refresh | the `update_packages` capability of `xlings interface`, which exists (`src/capabilities.cpp:182`), in place of the bare CLI. Whether it emits `download_progress` for an index sync is to be measured. If it emits none, xlings emits it for the index artifact and the git sync. | mcpp; xlings if the events are missing |
| `git` dependencies | git's own `--progress` phases (`Receiving objects: 45% ...`, with bytes and rate), parsed into the same event shape | mcpp |
| Sandbox bootstrap | the NDJSON path first, and the direct CLI only as the fallback, which is the reverse of today's order | mcpp |
| A payload whose install emits no events | the payload's install uses xlings's downloader, which emits them | ecosystem data (the payload's recipe) or xlings, per case |

Rendering follows one rule per output mode:

| Mode | Rendering |
|---|---|
| Terminal (TTY) | the live bar, as today |
| Not a terminal | one line when an item starts, with its size when known, and one line when it finishes, with its duration; no `\r` and no ANSI. This mode fixes the defect above. |
| `--quiet` | nothing |
| Machine output (`--format json`, `emit`) | nothing on the document stream. Progress remains narration on stderr and is not part of the envelope. |

The end-of-run notices of W12 (§9.2) go through the same reporter, as the run's
closing lines.

**Criteria.**

- **Unit: the git progress parser.** Recorded git stderr is parsed into the
  expected events.
- **Unit: the renderer in non-terminal mode.** It is rendered into a buffer, which
  contains no `\r` or `ESC` byte and exactly one start line and one finish line
  per item.
- **e2e.** A `git` dependency is fetched with stderr redirected to a file. The file
  holds the start and finish lines and no `\r`. This criterion fails on
  2026.9.27.1, which prints nothing for the clone.
- **Measurement.** The measurement matrix above is recorded in this document
  before and after the change.

### 9.2 An index that requires a newer mcpp is reported as an error (W12)

**Measured.** The binary is mcpp 2026.9.18.1, and the index's `min_mcpp` is
2026.9.18.3. `mcpp build` of a project with one index dependency prints, before
anything else:

```
error: index requires mcpp >= 2026.9.18.3 but this is mcpp 2026.9.18.1 [E0006]
  ...
  Details:  mcpp explain E0006   (override for debugging: MCPP_INDEX_FLOOR=ignore)  Upgrade:  'xlings update mcpp' (recommended default installer)
```

The build then resolves its dependency, compiles, finishes, and exits with 0. The
line labelled `error:` described no error of the command. The message also lacks
a line break before the appended upgrade line.

**Read.**

- **The read site.** `read_identity_verified_xpkg_lua`
  (`src/pm/package_fetcher.cppm:698`) calls `check_index_floor` on the first
  descriptor read from each index. On a violation it calls `mcpp::ui::error` and
  returns no descriptor. This is the "start of the run" in the observation.
- **The fact is also recorded.** `check_index_floor`
  (`src/pm/index_contract.cppm:236-262`) records the fact in a process registry.
- **The failure path already explains itself.** A lookup that then fails carries
  the cause through `unusable_index_hint()` (`package_fetcher.cppm:410`,
  `prepare/fetch.cpp:487`). That hint currently says "See the [E0006] error
  above".
- **The refresh guard.** `update_index` (`src/xlings/xlings.cppm:2046-2080`) keeps
  the previous snapshot when a refreshed index requires a newer mcpp. It prints a
  `Kept` status line at that moment, which is mid-run.
- **The two defects.** The guard itself is correct. The label and the timing of
  the notice are not.

**The rule this follows.** The rule is already written down, and this path breaks
it: an index is data and mcpp is a program, and `min_mcpp` is a routing signal,
not a termination signal (the 2026-08-03 records on the index floor). A run that
succeeds has no error to report.

**Change.**

1. **The read site prints nothing.** The floor violation is recorded, as today,
   and the lookup still does not use that tree.
2. **A run that fails** carries the cause in its failure message, as
   `unusable_index_hint()` already does. The hint now states the E0006 text
   itself instead of pointing at an error above it.
3. **A run that succeeds** prints at most one line at its end, and only when this
   run refreshed or attempted to refresh an index and that index requires a newer
   mcpp. The refresh may be `mcpp index update` or an automatic refresh. The line
   has the form `tip: the package index now requires mcpp >= X; this run used the
   previous index. Upgrade: xlings update mcpp`. A run that did not touch the
   index says nothing. The tip moves the guard's mid-run `Kept` status to the end
   of the run.
4. **Under `--format json`**, the fact is a diagnostic of severity `note`. It
   never changes the exit code (SPEC-003).
5. **`mcpp doctor` reports the state. This is new work, not existing
   behaviour.** Today `doctor_report` (`src/doctor.cppm:187-790`) never consults
   the unusable-index registry. `src/doctor.cppm:1337` is the text of
   `mcpp explain E0006`, not a check. W12 adds a doctor check that lists every
   index in `unusable_indexes()` with its floor. Without that check, removing the
   error at the read site would leave the state visible only in a failing run.
6. **The missing line break** in `e0006_message` is added.

**Criteria.** Each is an e2e test with a path index whose `index.toml`
`min_mcpp` is above the binary's version:

- A build that needs no package from that index prints no `error:` line and exits
  with 0.
- The same build with an index refresh in the run prints exactly one `tip:` line,
  and it is the last line of the output.
- A build that needs a package only that index serves fails, and its last error
  names E0006. This is e2e 185's existing assertion, kept.
- `mcpp doctor` lists that index and its floor.

The first and last criteria fail on 2026.9.27.1: the read site is unchanged at
`b439fd97`, and `doctor_report` has no such check.

## 10. What is not done, and why

| Proposal | Source | Why not |
|---|---|---|
| `mcpp::dialect_cxxflag` in build programs | #717 | A target predicate is data; the directive would give programs authority over the whole graph (§6.2) |
| Auto-promote `-fms-runtime-lib` into the dialect flags | #717 | The CRT belongs to `linkage` and `cxx_runtime` (§7.3) |
| New keys `msvc_crt_linkage` and `msvc_crt_variant` | #718 | A second spelling of an existing fact (§7.2) |
| The debug CRT in the dev profile, as CMake, Meson and Visual Studio do | #718 | The dev profile states debug information, not a different ABI; deferred as an opt-in axis (§7.2) |
| Keeping the static CRT as the LLVM row's default | #718 | It is ABI-incompatible with the prebuilt ecosystem and gives each DLL its own CRT state (§7.2) |
| Consumer precedence, action equality, exclusion lists | #723 | §5.2 |
| Run generators under `emit` | #724 | §4.4 |
| A function-length gate written as a text heuristic | #722 | §8 |
| A second progress UI for the paths that show none | review | One renderer exists; the missing paths become its producers (§9.1) |
| Any work on the GCC 16.1 ICE | #721 | upstream; out of scope for this round |

## 11. Order of work

The reviewer decided that #722 is done in the same round (D7). The round is
therefore one release, in three stages. The stages are ordered so that each diff
stays small and reviewable.

| Stage | Step | Content | Specs and docs |
|---|---|---|---|
| 1 | W1 | #725: the workspace context for a rooted workspace; `-p` resolves the package identity | SPEC-004 §9 item 1; docs 07 §5.3 (en, zh); the `-p` help text |
| 1 | W2 | #720: the lib root in the sort | docs 31 (a lib root may import siblings) |
| 1 | W3 | #724 §1: a device source is not a compile unit | SPEC-005 R3.7 |
| 1 | W4 | #724 A: the failed program's diagnostic survives | SPEC-005 R5.2 amended: a check whose premise is a build program's directives does not run for a package whose program failed |
| 1 | W5 | #724 B: no project write under `emit` | SPEC-005 R2.1 (unchanged) |
| 1 | W6 | #723: one destination, one content, checked when staging; one writer per destination | SPEC-007 R4.2 and R4.3 |
| 1 | W12 | the index floor is a closing tip, not an error; a doctor check; W12 introduces the reporter's list of closing notices | docs 09 (and its doctor section), docs 50 (`note` severity) |
| 2 | W7 | #722: split the phase functions | none |
| 3 | W8 | #717: conditional graph-wide dialect flags | SPEC-004 §3.1, §9 item 10 |
| 3 | W9 | #718: the CRT model by ABI; `toolchain-coupled` is the MSVC-ABI default | docs 20 and 04; SPEC-006 (the row's CRT) |
| 3 | W10 | #724 §2: the generated-output record | S1 addition (mcppls), SPEC-005 §3 |
| 3 | W11 | one progress mechanism; the producers per path | docs 09 |

The order has three reasons:

1. **Stage 1 comes first because it fixes defects on the present layout.** W1 is
   first in it, because it blocks existing builds, including mcppls's nightly run.
2. **Stage 2 then splits the phase functions that stage 1 has touched.** Its
   golden fixtures are regenerated after stage 1, so that the criterion of
   byte-identical output compares the split with the unsplit code at one point in
   the history.
3. **Stage 3 lands in the smaller functions,** in an order fixed by three
   dependencies:
   - W8 precedes W9, because W9's rule for free-form CRT words reads the
     `dialect_cxxflags` that W8 makes conditional.
   - W10 waits for the S1 text (D6) and builds on W3.
   - W11 attaches progress rendering to the reporter that W12 introduced.

Every defect and feature step (W1 to W6 and W8 to W12) has a criterion that fails
on 2026.9.27.1 and passes after the change. W7 is a refactor, and its invariants
hold before and after by construction: the golden fixtures stay byte-identical,
and ASan stays clean. Its one criterion that fails before is the function-size
gate (§8).

**Compatibility notes for the CHANGELOG.**

- **W1.** Members reached as path dependencies of a rooted workspace receive
  `[workspace.build]` and `[workspace.package]`. `-p` resolves a package name or
  a qualified name first, and the directory spellings second. A value that is one
  member's package name and another member's directory now selects the package,
  with a warning. A package name shared by two members under different namespaces
  is refused, and the message gives both qualified names. This is the third
  refusal the round adds.
- **W6.** A deploy collision is reported when staging, and identical files no
  longer collide.
- **W9.** LLVM-row programs on the MSVC ABI move to the dynamic CRT, with the
  redistributable staged beside them. cl-row programs keep `/MD` and gain the
  staged DLLs. `cxx_runtime = "self-contained"` restores the static CRT.
  - **Two manifests that build today are refused afterwards**, each with the
    one-line fix in its message:
    - a free-form CRT word that contradicts the resolved model, for example
      `-fms-runtime-lib=static` on the LLVM row;
    - an explicit `toolchain-coupled` on a row whose toolset has no
      redistributable directory. Today E10 downgrades that request with a warning.
- **W12.** E0006 is no longer printed as an error by a run that succeeds.
  `mcpp doctor` lists any index this mcpp cannot read.

**Other repositories.**

- **openxlings/xlings.** It is involved only if the W11 measurement shows that
  `update_packages` emits no `download_progress` for an index sync. The xlings
  release then precedes the mcpp release, and the mcpp release pull request
  carries the xlings pin.
- **mcpp-language-server.**
  - The S1 addition for W10 (D6).
  - After the release, its nightly run is green again (W1), and it may read the
    generated-output record.
- **GalTranslPP.** After the release it needs:
  - no CRT flag at all, because W9's default is its choice;
  - the updater back on an `artifacts` edge (W6);
  - `lib.path` may point at `gpp-build.ixx` (W2).
- **mcpp-plugins.** No change is required. Two changes are optional: splitting
  the lib roots (W2), and the SDK-catalog deploy in rules-qt (W6).

## 12. Decisions

Every decision is settled (revision 3, 2026-09-27).

| | Decision | State |
|---|---|---|
| D1 | #725: the workspace context is set from where the manifest lives. `-p` resolves the qualified name, then the package name, then the directory spellings (§2.3) | accepted. The reviewer pointed out that the option is `--package <NAME>` and asked for the reasonable, specification-conforming form |
| D2 | #718: `toolchain-coupled` (`/MD` with the redistributable staged) is the default for every MSVC-ABI row, cl and clang++ alike | accepted |
| D3 | #718: a free-form CRT word that agrees with the model is warned as redundant; one that disagrees is refused | accepted |
| D4 | #717: `[target.<selector>.build] dialect_cxxflags`, root-only and appended; the directive is declined | accepted |
| D5 | #723: a content check when staging replaces the planning refusal, for every deploy; the merged deploy list is the single authority for a destination | accepted (the one-writer rule was added by the self-review, §5.2) |
| D6 | #724 §2: mcpp proposes the record's shape, and the mcppls maintainers write it into S1 before W10 lands | accepted |
| D7 | #722 in the same round | accepted: one release, with #722 as stage 2 (§11) |
| D8 | W12: no error at the read site; the cause goes into a failure message; a closing tip only when the run refreshed an index; a new `mcpp doctor` check | accepted |
| D9 | W11: measure every acquisition path first, then make the missing paths producers of the one renderer; non-terminal output without `\r` | accepted |

## 13. Self-review of the whole plan

**Method.** The self-review was done in two passes on revision 2:

1. **The author's pass.** The author read the plan against the specifications and
   the code, looking for interactions between steps.
2. **An independent adversarial review.** A second reader checked the steps
   against each other, against SPEC-001 to SPEC-007 and docs 10, 20 and 50, for
   upgrade cliffs, and for criteria that cannot fail. It spot-checked the
   citations behind W1, W3, W6, W9 and W12.

The citations behind those five steps were confirmed. Every finding below changed
the text, in the section named.

### 13.1 Findings, and what changed

| # | Finding | Found by | Change | Where |
|---|---|---|---|---|
| 1 | The plan said `mcpp doctor` already reports an unreadable index. It does not: the cited line is the text of `mcpp explain E0006`. Removing the error at the read site would have left the state visible only in a failing run | reviewer | W12 adds a doctor check | §9.2 |
| 2 | Three mechanisms write into a program's directory, and only declared deploys go through `add_deploy`. R4.3's placement ignores the deploy list, so W9's default staging of `vcruntime140.dll` could race with a plugin directory that ships its own copy | reviewer and author | the merged deploy list is the single authority for a destination; R4.3 skips a name the list places | §5.2 |
| 3 | The first text of W9 staged the redistributable through R4.3, from `linkRuntimeDirs`. On the LLVM row that field holds LLVM's own directories | author | the existing planning-time staging is kept; the toolset's redistributable directory becomes its own field | §7.3 |
| 4 | For a toolset without a redistributable directory, the text left open whether an explicit `toolchain-coupled` would be downgraded, which is E10's shape again | reviewer and author | a defaulted contract resolves to `host-coupled`, recorded once; an explicit one is refused | §7.3 |
| 5 | W4 changes what SPEC-005 R5.2 means for checks that depend on a build program's directives; the plan called this "an example" | author | R5.2 is named as amended | §11 |
| 6 | W1 could have carried root-position keys (`[toolchain]`, `[target.<triple>]`, `[indices]`) to members reached by path | author | the scope is stated, and the criterion checks the member's toolchain and flags | §2.3, §2.4 |
| 7 | A dependency's inert `dialect_cxxflags` enters its fingerprint. W1 would have made more members carry one, and each such member would be rebuilt once for nothing | reviewer | W8: a key enters a fingerprint only where it reaches a command | §6.2 |
| 8 | Refusing an ambiguous `-p` would break a command that works today | reviewer | revised in revision 3 after the reviewer's D1 answer. The option names a package, so the package identity is resolved first; directory spellings remain a fallback, and the one layout where the two disagree selects the package with a warning. A short name shared under two namespaces is refused, since no earlier behaviour existed there to preserve | §2.3 |
| 9 | "Every criterion fails before the change" is false for a refactor. W7 also allowed the function limit to survive as prose | reviewer | W7's failing criterion is the parsed size gate; without the gate, the limit is dropped rather than written down | §8, §11 |
| 10 | The plan cited docs/20 as already scoping the PE shared-library default to the GNU ABI; the table says only "PE (Windows)" | reviewer | stated as a planned amendment | §7.3 |
| 11 | Removing device units from the plan's compile units could break a consumer that pairs them with graph units by position | author | checked. The only index uses are within the list (`plan.cppm:1914`, `prepare/plan.cpp:1790`, `:1832`). The dependency cache collects a package's units by path, so a dependency with device sources changes its artifact set; the implementation verifies this | §4.1 |
| 12 | A progress bar drawn during `emit` could corrupt the document on stdout | author | checked: `emit` redirects stdout to stderr while planning (`cmd_build.cppm:377`) | §9.1 |

### 13.2 Properties the plan keeps, checked across all steps

- **One home for each item, and the engine names no tool.**
  - W6 compares bytes.
  - W10 describes actions without naming their tools.
  - W9 speaks of ABIs and toolset directories.
  - W11 renders events from any producer.

  No knowledge of Qt, vcpkg or CMake enters the engine. The only step outside the
  engine is xlings's progress events, and only if a measurement shows they are
  missing.
- **No slot for exceptions.**
  - W6 replaces a proxy check with the invariant.
  - W9 has one derivation and one word for each driver.
  - W12 removes a false error instead of silencing it.
  - W7's limit is either parsed or dropped.
- **An explicit statement outranks a default wherever a default is introduced or
  changed:**
  - W9's pack mode, and its rows without a redistributable directory;
  - W6's declared deploys over the toolchain's derived staging.
- **Planning stays pure.**
  - W5 removes the one write into the project.
  - W10 describes generated outputs and runs nothing.
  - W3 and W4 change only what is described and reported.
- **An index is data.** After W12 the floor appears in exactly three places:
  - the cause in a failing run;
  - a closing tip in a run that refreshed an index;
  - `mcpp doctor`.

  It is never an error in a run that succeeds.
- **Upgrades are announced.**
  - Every behaviour change is listed in §11's compatibility notes, with its
    one-line remedy.
  - Three refusals are added, and all three are named there:
    - W9: a contradicting CRT word;
    - W9: an explicit `toolchain-coupled` without a redistributable directory;
    - W1: a package name that is ambiguous across namespaces.
  - The only selection that changes is W1's `-p`, where the option's own name
    decides. The change is warned and announced.
- **The order holds.**
  - W12 lands before W11, which reuses its reporter.
  - W8 lands before W9, whose rule reads W8's conditional flags.
  - W10 follows W3 and waits for the S1 text.
  - W7 splits code that stage 1 has already changed.

### 13.3 Owed before the release, not open in the design

- **The Windows leg:** the criteria of §7.3 (W9) and §5.3 (W6's one writer), and
  a `mcpp test` run with no Visual Studio directory on `PATH`.
- **Whether `xim:msvc` carries `VC\Redist`.**
- **The acquisition matrix of W11**, before and after, and whether xlings's
  `update_packages` emits `download_progress` for an index sync.
- **The S1 text for W10**, from the mcppls maintainers (D6).

### 13.4 Second self-review (revision 3)

**Method.** After the reviewer's answers, the author read the whole document again
from beginning to end. The pass checked three things:

1. every section against the settled decisions;
2. every step against its criterion and its specification change;
3. the counts and claims in the summaries against the sections they summarise.

**What changed.**

| # | Finding | Change | Where |
|---|---|---|---|
| 1 | W10 had no criterion | the generated-header record is checked against the path a following `mcpp build` writes | §4.4 |
| 2 | The one-writer rule of W6 had no criterion, and SPEC-007 R4.3 was not listed as changed | a Windows-leg criterion; R4.3 added to §11 | §5.3, §11 |
| 3 | The `mcpp doctor` check of W12 had no criterion | a criterion, which fails on 2026.9.27.1 | §9.2 |
| 4 | The package-first `-p` of D1 adds a refusal: a short name shared under two namespaces. §13.2 said no other refusal was added | the refusal is listed in §11 and counted in §13.2 | §11, §13.2 |
| 5 | Duplicate directory basenames are resolved silently to the first member today. Refusing them would break working commands | the first member stays selected, and a warning names the others | §2.3 |
| 6 | §13.2 listed `-p` under "an explicit statement outranks a default". The resolution order is a question of the option's documented meaning, not of a default | the item moved to the upgrade bullet | §13.2 |
| 7 | §6.1 said `[workspace.build] dialect_cxxflags` reaches only the root member. `inherit_workspace_build` also prepends it into every member pulled in as a path dependency | corrected; the cache-key claim of the fingerprint rule is cited (`cache_key.cppm:323`) | §6.1, §6.2 |
| 8 | E10's removal was stated as "every row delivers every value", which the refusal for a row without a redistributable directory contradicts | reworded | §7.3 |
| 9 | The scope counted eleven items for "#724 carries four" alone. The basis omitted the 2026.9.18.1 binary used for §9.2 | corrected | §0, basis |

**Checked and unchanged.**

- **Decisions against sections.** Every settled decision (§12) matches the
  section it names.
- **Ledger against order.** Each of W1 to W12 appears in both the ledger (§1) and
  the order (§11).
- **Criteria.** Every step W1 to W6 and W8 to W12 now has a criterion that fails
  on 2026.9.27.1. W7's failing criterion is its parsed size gate.
- **Routing.** No step moved between homes in this pass.
