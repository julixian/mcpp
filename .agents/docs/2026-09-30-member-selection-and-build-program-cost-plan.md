---
subject: design
status: active
---

# Member selection, build programs prepared once, a pack over several members, and the output streams of `mcpp run`: the plan for the release after 2026.9.30.2 (#748, #749, #750)

- Status: revision 3, in implementation.
  - Revision 3 settles the open decisions by their recommendations (D3, D6,
    D7), since the review directed that the plan be implemented. It removes
    the stage-specific downstream verification from the plan, adds a review
    from several angles (section 12), the tasks with their ownership and
    dependencies (section 13), and the cross-repository work and its
    verification (section 14). It folds #750, filed after revision 1, into S,
    and records #751 as outside this release (section 8).
  - Revision 1 was reviewed on 2026-10-01. D1, D2 and D5 were accepted.
  - D4 was settled by the reviewer's rule: what the project owns is kept in
    the project, and what comes from the index is kept globally.
  - Status output moves to stderr on every command (R3, option (b)).
  - D3 and D6 were asked about. Revision 2 explains D3 and replaces D6: the
    revision 1 answer would have changed the meaning of a JSON field that
    `docs/50-machine-output.md` §7 fixes.
  - The review also asked for `mcpp run -q` to be aligned with established
    practice. That is section 6, from a measurement.
  - Section 11 is the self-review of revision 2.
- Date: 2026-09-30, revised 2026-10-01.
- Origin:
  1. A question on how to build or test several workspace members at once
     (`mcpp test -p a -p b`), and the follow-up questions on what Cargo does
     and what mcpp's own design admits.
  2. mcpp#748: a workspace's build programs are prepared and compiled one
     after another, although only their runs have an order.
  3. mcpp#749: `mcpp pack` packs one member per invocation, so packing
     several members plans the graph and runs the build programs once per
     member.
  4. Review round 1: whether `mcpp run -q` follows the conventions of
     comparable tools.
  5. mcpp#750, filed independently of this plan: a repeated `-p` keeps only
     the last value (F1).
- Task: one plan for the next release that treats the four as one subject.
  The subject is which members a command acts on, what a command pays once
  per selection rather than once per member, and which stream carries what.

## 0. Summary

| # | Finding | Item |
|---|---|---|
| F1 | `-p` takes one value on every command. A repeated `-p` is accepted, and all but the last are dropped without a word (measured) | S1, S2 |
| F2 | The selection is computed by two functions. `build` and `emit` use `workspace_selection` and plan once per configuration group. `test --workspace` uses `workspace_fanout_members` and plans each member alone (code) | S1, S4 |
| F3 | Each build program compiles the bundled `mcpp` module, and every host module it imports, into its own directory before its own compile. #748 measured about 7.8 s per program on its runner, repeated for every program and every invocation. The attribution comes from code reading and is to be confirmed | B1, B2 |
| F4 | `pack` has one `-p`, no `--workspace`, and one stage directory per invocation (code, #749) | K1 |
| F5 | `mcpp run` writes its status lines to stdout. With `-q` it still writes an empty line to stdout before the program's output. A failed build exits 1, as a program that returns 1 does (measured) | R1, R2, R3 |

The plan has four parts. No manifest key is added.

- **S, the selection.**
  - S1: one selection shared by every command.
  - S2: `-p` is repeatable.
  - S3: `--exclude`.
  - S4: `test` plans a selection once.
- **B, the build programs (#748).**
  - B1: the `mcpp` module and host modules are compiled once per agreeing
    flag set. The output is kept globally when it comes from the engine or the
    index, and in the workspace otherwise.
  - B2: programs are compiled concurrently, after the process launcher is
    made safe for concurrent callers (B2-0).
  - B3: runs in dependency waves, deferred.
- **K, pack over a selection (#749).**
  - K1: `pack --workspace` and a repeated `-p`, with one plan and one stage
    directory per member.
- **R, the output streams.**
  - R1: no empty line under `-q`.
  - R2: a distinct exit status for a `run` whose build failed.
  - R3: status output on stderr, on every command.

Three existing behaviours change:

- A repeated `-p` selects every member it names (F1).
- Status lines leave stdout (R3).
- A `run` whose build failed exits 101 (D7).

## 1. Findings

### F1. A repeated `-p` keeps the last value

`src/cli.cppm` declares `package` with `.takes_value()` and without
`.multiple()`. It does so on `build` (390), `run` (436), `test` (538), `pack`
(660) and `describe` (707).

Measured with mcpp 2026.9.30.1 on a virtual workspace of three members `a`,
`b` and `c`. The option declaration is the same at 8e00a183.

```
$ mcpp build -p a -p b
   Workspace building member 'b'
   ...
   Compiling b v0.1.0 (b)
    Finished dev [unoptimized + debuginfo] in 0.30s
```

`a` is not built, and nothing says so. #750 reports the same defect on
`mcpp test -p a -p b`, which tests `b` alone and exits 0.

### F2. Two selection functions, and `test` plans each member alone

`src/cli/cmd_build.cppm` has two functions:

- `workspace_selection` (79) returns the workspace root and the selected
  members. `build` (324) and `emit build-database` (612) pass the members to
  `workspace_groups`. They then plan once per configuration group through
  `BuildOverrides::workspace_members` (workspace design 2026-09-29, §15).
- `workspace_fanout_members` (50) returns the member list alone. `test` (998)
  loops over it and calls `run_tests` once per member with
  `package_filter = member`. Each call resolves the toolchain, plans that
  member's closure, and runs the build programs of the closure.

The planner already accepts a group together with the test targets of each
member (`BuildOverrides::member_targets`, read in
`src/build/prepare/manifest.cpp:187`). `--configure-only` and
`emit build-database` use it. `test` does not.

This has two consequences, the same ones #749 states for `pack`:

- A shared member's build program runs once per member that reaches it.
- A shared member's active features are those of one closure, not the union
  across the selection.

### F3. The preparation of a build program is repeated per program

When the program's cache is stale, `run_build_program`
(`src/build/build_program.cppm`) performs four steps:

1. It compiles the bundled `mcpp` module and its `mcpp.core` alias
   (`build_mcpp_module`, 1401).
2. It obtains the `std` module, which is already cached globally by
   `stdmod::ensure_built`.
3. It compiles every host module the program imports (`build_host_module`,
   1518).
4. It compiles `build.mcpp` and runs it.

Steps 1 and 3 write into `<member>/target/.build-mcpp`. Neither step checks
whether its output is already current. `program_compiling` is called only at
step 4 (1665), so the reported `ran` covers step 4 alone. Steps 1 to 3 are
counted as `plan` in `Finished`. `step9_member_build_programs`
(`src/build/prepare/target_side.cpp:1945`) runs the programs one after
another.

#748 measured about 7.8 s outside `ran` for each of four programs on a
4-vCPU `windows-2025` runner, and about 8 s again in each `mcpp pack`. The
four programs import the same host module with the same flags.

### F4. `pack` acts on one member

`pack` has one `-p` and no `--workspace` (`src/cli.cppm:660`). The stage
directory is one value per invocation (`BuildOverrides::pack_stage_dir`,
`src/build/prepare.cppm:687`). `step9_member_build_programs` gives that one
value to every program (`bpEnv.packStageDir`, target_side.cpp:1992).

#749 measured two consecutive `mcpp pack -p` invocations on a five-member
workspace at 146 s. Distribution accounts for about 35 s of that. Most of the remainder is
planning and build programs repeated per invocation, plus a 29.2 s gap
between the two processes.

### F5. The output streams of `mcpp run`

The measurements used mcpp 2026.9.30.2 on member `a` of the F1 workspace. The
program prints `OUT` on stdout and `ERR` on stderr.

| Command | stdout | Note |
|---|---|---|
| `mcpp run 2>/dev/null` | the `Workspace`, `Resolving`, `Resolved`, `Target`, `Inferred`, `Finished` and `Running` lines, an empty line, then `OUT` | status is on stdout |
| `mcpp run -q 2>/dev/null` | `\n` `OUT` `\n` (checked with `od -c`) | one empty line before the program's output |
| `mcpp run -q -- x` (the program returns 3) | exit 3 | the program's status passes through |
| `mcpp run -q` with a compile error | exit 1, and the diagnostic is shown | the build's status |
| `mcpp run -q` (the program returns 1) | exit 1 | indistinguishable from the previous row |

Where each behaviour comes from:

- **Status on stdout.** This was decided on purpose by the observability
  design of 2026-05-22: `status`, `info`, `finished` and progress go to
  stdout, and `warning` and `error` go to stderr.
- **The empty line.** `src/build/execute.cppm:2183` prints `std::println("")`
  after the `Running` line, and it does so unconditionally. Under `-q`, only
  the empty line remains.
- **Arguments after `--`.** The `--quiet`/`-q` pre-scan (`src/cli.cppm:161`)
  stops at `--`, so arguments after it reach the program.

## 2. What the design already states, and what follows from it

| # | Principle | Where | Consequence here |
|---|---|---|---|
| P1 | An input mcpp cannot serve, or cannot read unambiguously, is refused by name. mcpp does not guess | `docs/00-what-mcpp-is.md`, the guarantee; `docs/07-workspace.md` §5.3, where an ambiguous `-p` is refused, naming every match | F1 is a defect. A name that matches no member is refused, and so is a selection a command cannot act on |
| P2 | One graph per configuration. The selection only chooses the roots | `docs/07-workspace.md` §5.4; workspace design 2026-09-29 §15 | Several members are one plan, never N invocations. The selection is a set, so argument order does not change the plan |
| P3 | A command over several members continues past a failing member, reports each member, and ends with a summary | `docs/07-workspace.md` §5.3 | A multi-member `-p` inherits this report. There is no fail-fast switch |
| P4 | `-p` names a package, resolved among the members | `docs/07-workspace.md` §5.3 | `-p` does not select a dependency, unlike Cargo |
| P5 | The engine carries general capabilities. A new manifest key is a compatibility cost on engines already released | the engine-and-plugin rule; the `[c-abi]` precedent | No `default-members` key |
| P6 | A BMI is usable only by a compile that agrees with it. The agreement is produced from one set of flags, not checked afterwards | `build_host_module` (`src/build/hostprogram.cppm:918`) | B1 shares a BMI only between compiles whose key, which includes the flags, is equal |
| P7 | Only what comes from the immutable store may enter the global cache, and only when nothing it was built against is local | `src/build/prepare/plan.cpp:1964` | B1 applies the same rule: engine and index output is global, and everything else stays in the workspace |
| P8 | The JSON streams add fields and never remove or redefine one | `docs/50-machine-output.md` §7 | D6 adds records and fields. `build_ms` keeps its meaning |

### Compared with Cargo

| Cargo | mcpp in this plan | Reason |
|---|---|---|
| `-p a -p b` | same | P2 |
| `--workspace --exclude c` | same, and also with the implicit whole selection at a virtual root (D2) | a virtual root without `-p` already means every member |
| `-p` accepts a dependency | refused; members only | P4 |
| `-p 'foo-*'` | not in this release | a fourth resolution form would make a pattern ambiguous between a name and a path |
| `[workspace] default-members` | not in this release | P5 |
| stops at the first failing test binary unless `--no-fail-fast` | continues and reports every member (D1) | P3 |
| status on stderr, and stdout is the program's | the same after R3 | section 6 |
| a failed build exits 101, and a program's status passes through | the same after R2 (D7) | section 6 |

## 3. The selection (S1 to S4)

### S1. One selection, one function

`workspace_fanout_members` and `workspace_selection` are replaced by one
function.

- Input: `(wantAll, packages[], excludes[])`.
- Output: `{root, members}`. `members` is a set, kept in `[workspace]
  members` order (D5). A rooted workspace's own package comes first, as `"."`.

| Input | Members |
|---|---|
| `--workspace` | all |
| a virtual root, no `-p` | all |
| a rooted root, no `-p` | `"."` |
| inside member X, no `-p` | X |
| `-p X -p Y` | {X, Y}, each resolved by the §5.3 order |
| any "all" form above with `--exclude Z` | all minus Z |

The following are refused before any planning:

| Input | Refusal |
|---|---|
| `-p N`, where N matches no member | refused, listing the members |
| N is ambiguous | refused, naming every match, as today |
| `--exclude` together with `-p` | refused |
| `--exclude Z`, where Z matches no member | refused |
| every member excluded | refused |

Two `-p` values that resolve to one member select it once.

### S2. `-p` is repeatable

`.multiple()` is added to `package` on `build`, `test`, `pack` and
`describe`. `BuildOverrides::package_filter` stays a single string. The plan
reads it in 56 places, and a selection of one member still sets it. A
selection of several members goes through `workspace_members`, as
`--workspace` does now.

`run` keeps one member. On `run`, a second `-p` is refused, naming both.

### S3. `--exclude`

The option is added to `build`, `test`, `pack` and `describe`. Its value is
resolved like `-p`, and it may be repeated. It applies to every "all" form
(D2) and is refused with `-p`.

### S4. `test` plans a selection once

A `test` over more than one member proceeds in four steps:

1. It plans once per configuration group, with each member's test targets in
   `member_targets`, as `--configure-only` does.
2. It builds each group's graph once.
3. It runs each member's tests in member order, continuing past failures
   (D1).
4. Test discovery stays scoped to each member.

The report keeps its shape, with one change for the shared build (D6,
section 7). The timeouts are divided between the two phases:

- `--build-timeout` bounds the build.
- `--workspace-timeout` bounds the runs. A member not started before the
  deadline is listed as `not run`, as before.

A member that fails to plan fails alone, by the R5.2 rule `emit` already
uses. The other members of its group are planned without it.

## 4. Build programs (#748)

### B0. The attribution, measured first

Before B1 is written, `mcpp build --workspace` runs with `MCPP_VERBOSE=1` on
a fixture of #748's shape: four members, each with a build program that
imports one member host module, which imports the bundled `mcpp` module. The
`buildmcpp-host` lines are timestamped and summed per step: the `mcpp`
module, `std`, each host module, and `build.mcpp`.

If steps 1 and 3 of F3 are not most of the preparation, B1 is re-scoped. The
measurement is recorded in section 15, which the landing revision adds.
B0 also records the `base` flags of the bundled module's compile (see
section 11, item 10).

### B1. The `mcpp` module and host modules are compiled once per key, and kept by provenance

Each output of `build_mcpp_module` and `build_host_module` moves from the
program's `bdir` to an entry addressed by a key. The key is built from:

- the host compiler's identity (`compilerHash`);
- the standard flag, `base`, and the `use` flags of the compile;
- the SHA-256 of the interface file;
- for the bundled module, the mcpp version, which determines its text;
- for a host module, the providing package's identity (index, name,
  version).

Under P6, a program whose flags differ has a different key. Agreement is
therefore a consequence of the key and is never checked separately.

**Where the entry lives (D4, as decided): by provenance, under P7.**

| What | Where | Why |
|---|---|---|
| The bundled `mcpp` module and `mcpp.core` | the global cache, next to the `std` module | the engine owns its text. It is identical in every project for one mcpp version and one host compiler |
| A host module from an index package whose sources are in the immutable store | the global cache, beside the package's cached objects | the same admission rule as the dependency cache (`plan.cpp:1964`) |
| A host module from a path or git dependency, or from a workspace member | `<workspace>/target/.build-mcpp/host-modules/<key>/` | its sources can change without its name and version changing |

A host module compiled "alone" imports only `std` and `mcpp`
(`build_host_module`: "a rule package is a leaf by construction"). Its local
taint is therefore its own package's alone, so the dependency cache's rule
applies without a closure walk.

The cache mode applies as it does for dependencies:

- `--cache global`: global when admissible, otherwise the workspace.
- `local` and `off`: the workspace. Programs of one invocation still share
  entries.

The global entries reuse `mcpp.bmi_cache`:

- the entry layout and `entry.json`, whose recorded inputs are compared field
  by field on a hit;
- the LRU stamp, so `mcpp cache gc` collects them;
- the write to a temporary name followed by a rename.

On GCC, a consumer stages BMIs into its `gcm.cache`, which `bmi_cache`
already does for cached dependencies. On clang and MSVC, the BMIs are
referenced by path.

Expected effect, which is an estimate, not a measurement:

- On a fresh runner, the first program pays steps 1 and 3 of F3, and every
  later program of the build pays neither. For the #748 workspace this saves
  25 to 30 s.
- A later `mcpp pack` or `mcpp test` in the same checkout pays neither.
- On a developer machine, a second project with the same mcpp and host
  compiler does not compile the bundled module again.

### B2. Programs are compiled concurrently

**B2-0, the launcher is safe under concurrent use.** Two threads that start
children at the same time must not pass one child's pipe to the other.

- `capture_exec` on Linux and macOS creates its pipe with `::pipe`, without
  `O_CLOEXEC` (`modules/platform/src/process.cppm`).
- The bounded launchers on both families create an inheritable write end and
  start the child with handle inheritance
  (`modules/platform/src/windows/bounded_process.cppm`, `CreateProcessA` with
  `bInheritHandles = TRUE`).

A child started by one thread while another thread's pipe is open inherits
that pipe's write end. The other thread's reader then waits for end of file
until the unrelated child exits. The pipes are therefore created close-on-exec
where the platform offers it (`pipe2` with `O_CLOEXEC`; `posix_spawn`'s `dup2`
clears the flag on the child's descriptors). Where the platform does not
offer it (macOS pipes, Windows handle inheritance), the creation of the pipe,
the start of the child and the parent's close of the write end form one
critical section under a process-wide mutex. The registry of children for
signal forwarding (`guard_group_on_signal`) is made safe for concurrent
callers in the same change.

`step9_member_build_programs` is split into two phases:

- **Phase A** compiles every stale program concurrently, up to the build's
  job count, once B1's entries exist. No compile depends on another program's
  run.
- **Phase B** runs the programs in the present serial order and applies
  their directives in that order. The plan and `build.ninja` are therefore
  byte-identical to the serial form's (#748 D).

A compile's output is captured and printed as a whole. When several compiles
fail, the failure reported is the first in the serial order (#748 E).

### B3. Runs in dependency waves: deferred (D3)

B3 would run programs with no dependency between them at the same time. In
the #748 workspace, gpp.core and gpp.updater would run together, then gpp.cli
and gpp.gui.

#748 estimates the saving at 12.9 s to 10.8 s. The cost is paid in
correctness:

- Directives from concurrent runs would have to be buffered and applied in
  the serial order.
- Programs write generated files into their packages. Nothing now states
  that two programs' writes do not interfere, because they have always run
  one at a time.
- Reports, the first reported failure, and program timeouts would each need
  an ordering rule.

B3 is reconsidered only if a measurement after B1 and B2 shows the runs to be
the dominant remaining cost.

## 5. Pack over a selection (#749)

### K1

`mcpp pack --workspace`, `--exclude`, and a repeated `-p` select members
through S1. For `pack`, "all" means every member with a packable target.

1. **Refusals before any compile.**
   - A selected member that does not provide the requested `--format` is
     refused, and the refusal names it.
   - `--output` with more than one member must name a directory.
   - Two members whose staging writes one destination from different sources
     are refused, naming both, by the rule `mcpp stage` applies.
2. **One plan per configuration group, and one build.** Each build program
   runs once.
3. **A stage directory and a format per member.**
   - `BuildOverrides::pack_stage_dir` becomes a map from member to
     directory.
   - `step9_member_build_programs` already visits one package at a time, and
     sets `bpEnv.packStageDir` and `bpEnv.packFormat` from the map.
4. **Distribution per member, in member order.** A failing member is
   reported, and the others continue (P3). The exit status is non-zero if any
   member failed.
5. **`mcpp pack -p X` keeps its meaning.** It plans X's closure alone.

## 6. The output streams (R1 to R3)

### Established practice

The practice below is recalled, not measured here.

- **Cargo.** `cargo run` writes every status line (`Compiling`,
  `Finished`, `Running`) to stderr. The program owns stdout. `-q` suppresses
  cargo's own messages, not errors. A failed build exits 101, and a program's
  exit status passes through.
- **`go run` and `zig run`.** Both print nothing of their own on success and
  write diagnostics to stderr.
- **The common rule.** stdout carries a command's result: the program's
  output for `run`, and the document for a command that emits one. Progress
  goes to stderr, where a redirection or a pipe does not capture it.

### R1. No empty line under `-q`

The separator after the `Running` line is written only when the `Running`
line was written. It goes to the same stream as the `Running` line, which is
stderr after R3.

Criterion R-A: `mcpp run -q 2>/dev/null` produces exactly the program's
stdout, compared byte for byte.

### R2. A `run` whose build failed exits with a status of its own (D7)

A `run` whose planning or build fails exits **101**. This is Cargo's value
and an established convention. It is also rare among programs' own exit
statuses. The program's own status continues to pass through unchanged.
`build`, `test` and `pack` keep their exit statuses; `test`'s 0, 1 and 2 are
a documented contract.

Criterion R-B: with a compile error, `mcpp run` exits 101. With a program
that returns 1, it exits 1.

### R3. Status output on stderr, on every command (option (b), decided)

Every line the 2026-05-22 design sends to stdout moves to stderr: `status`,
`info`, `finished`, `line`, the progress bar, and the live region. This
covers every command, so no two commands differ. Four changes follow:

- **The live region's terminal test** follows stderr instead of stdout.
  Output revision 3 (2026-09-30) draws a frame in one write, and that is
  unaffected.
- **stdout keeps only a command's result.** This means the program's output
  for `run`, the JSON streams, and the documents that `emit`, `describe` and
  `--list-runners` print.
- **JSON modes.** stdout would now stay clean without `set_quiet`. The
  existing `set_quiet` calls are kept, so that a JSON run also leaves stderr
  free of human status, as it does today.
- **Records.** This plan supersedes the stream table of the 2026-05-22
  design, and that record is not edited (the record rule). The user-facing
  docs that describe streams are updated in the same pull request:
  `docs/50-machine-output.md` and `docs/09-commands-by-scenario.md`.

**R3-0, a census first.** 46 e2e scripts mention `Compiling`, `Finished` or
`Running`. Most capture `2>&1`, which is indifferent to the change. The
scripts that capture stdout alone and assert a status line are counted
before the change, and each is corrected in the same pull request.

**User-facing change.** `mcpp build | tee log` no longer records the status
lines, and becomes `mcpp build 2>&1 | tee log`. CI logs, which capture both
streams, are unaffected. The CHANGELOG states this under a breaking-change
heading.

Criterion R-C: on a warning-free project, `mcpp build >/dev/null` still
shows `Compiling` and `Finished`, and `mcpp build 2>/dev/null` writes
nothing.

## 7. Decisions

| # | Question | Outcome |
|---|---|---|
| D1 | Should a multi-member `test` continue past a failing member? | **Accepted**: yes, and there is no switch |
| D2 | May `--exclude` be used with every "all" form? | **Accepted**: yes. It is refused with `-p` |
| D3 | Should B3, runs in dependency waves, be deferred? | **Settled by the recommendation**: deferred (§4 B3) |
| D4 | Where does B1's output live? | **Decided by the reviewer's rule**: engine and index output is global, and project-owned output stays in the workspace (§4 B1) |
| D5 | Are reports and packs in manifest order or command-line order? | **Accepted**: manifest order |
| D6 | How is a shared build reported in `test`? | **Settled by the recommendation**: revised form below |
| D7 | Should a `run` whose build failed exit 101? | **Settled by the recommendation**: yes (§6 R2) |
| D8 | Should status move to stderr on one command or on every command? | **Decided**: every command, option (b) (§6 R3) |

**D6, revised.** Revision 1 proposed reporting each member's `elapsed_ms` as
its run time alone. That redefines a field, which P8 forbids. Revision 2
adds and does not redefine:

- **A group record** comes before the group's first test record:
  `{"group_build": {"group": 0, "members": [...], "build_ms": N}}`.
- **Each member's `build_ms`** is its group's build wall time. That is still
  true to the field's definition: the wall time this member's tests waited
  for their build. A new field, `build_group`, names the group.
  - A consumer that sums `build_ms` over members deduplicates by
    `build_group`.
  - A consumer that does not sum is unaffected.
- **Each test's `duration_ms`** keeps the meaning the code gave it: the run of
  a test that ran, the build of a `compile_fail`. (The documentation's
  "build+run" did not describe the measured value; it is corrected.) The test
  binary's own build time, the sum of its edges in `.ninja_log`, is the added
  field `build_ms`.
- **The human report** prints one line per group: members, build time, and
  the slowest edges, for example `slowest: libs/jsc link 88s`. That keeps the
  signal the per-member split existed for: a member whose link, not its
  tests, is slow. Each member's line then states its run time.

## 8. What this release does not do

| Item | Reason |
|---|---|
| `-p` selecting a dependency | P4 |
| glob patterns in `-p` and `--exclude` | a fourth resolution form, and no demand |
| `[workspace] default-members` | P5 |
| `--fail-fast` / `--no-fail-fast` | P3; `--workspace-timeout` bounds the fan-out |
| several members as N internal invocations | P2 |
| a multi-member `run` | an artifact to execute is one program |
| B3 | D3 |
| a global home for project-owned host modules | P7 |
| #751, a shared member recompiled when another member compiles a module of the same name | see below |

**#751 is outside this release.** W10 of 2026.9.30.2 moves two providers of
one module name below their packages' directories only when the plan holds
both (`src/build/plan.cppm`, the block after the product directories). A
plan of `-p app` holds one provider and a plan of `--workspace` holds two,
so the shared member's command lines differ between the two selections. A
command line independent of the selection needs one of two things:

- the set of module names of the whole workspace, including the closures of
  members that are not selected, at every plan; or
- every module placed below its provider and named explicitly to every
  importer. On GCC this means a module mapper file on every compile, which is
  the default path of every GCC build.

Both change the planning of every workspace build, and neither is a
consequence of this plan's items. #751 remains open for its own design. The
criteria S-B and S-G below are stated on fixtures in which every module name
has one provider.

## 9. Delivery

One pull request in mcpp carries every item of this plan, the documentation,
and the version. The items share `src/cli/cmd_build.cppm`, `src/cli.cppm`
and the e2e suite, and R3 changes what every e2e script reads, so separate
pull requests would correct the same scripts more than once. Section 13
divides the work into tasks and states their order; section 14 states what
the other repositories do.

## 10. Acceptance criteria

Every new e2e fixture writes its paths through named `*_HOST` variables and
passes the `00` path lint.

**Selection**
- S-A. `mcpp build -p a -p b` in a workspace of `a`, `b` and `c` builds `a`
  and `b`. `c`'s object directory stays absent.
- S-B. `mcpp build -p a -p b`, followed by `mcpp build -p b -p a`, adds no
  compile edge to `.ninja_log`.
- S-C. `mcpp build -p nosuch` exits non-zero before planning. Its message
  names `nosuch` and lists the members.
- S-D. `mcpp run -p a -p b` is refused, naming both.
- S-E. `mcpp build --workspace --exclude c` builds `a` and `b`.
  `--exclude nosuch` and `-p a --exclude b` are refused.
- S-F. `a` and `b` share `core`, whose build program appends a line to a
  file under `OUT_DIR` on each run. After `mcpp test --workspace`, the file
  has one line. Today it has two.
- S-G. After `mcpp build --workspace`, `mcpp test --workspace` adds no
  compile edge for `core`'s sources. The fixture has no dev-dependency, so
  the test build does not change `core`'s features.
- S-H. The JSON stream of S-F has one `group_build` record, and both
  members' summaries carry its `build_group` and its `build_ms`.
- S-I. #750's reproduction: `mcpp test -p a -p b` runs the tests of `a` and
  of `b`, and the workspace summary counts both members.

**Build programs (#748)**
- B-A. Two members' programs import one host module from a path dependency
  with identical flags. The module's compile appears once in the
  `buildmcpp-host` verbose log, and its entry is under the workspace's
  `target/`.
- B-B. A program whose standard differs gets its own entry.
- B-C. After one build, a second project with the same mcpp and host
  compiler does not compile the bundled `mcpp` module. There is no
  `mcpp module precompile` or `compile` line in its verbose log.
- B-D. A host module from a path dependency is never written under the
  global cache root, with `--cache global` set.
- B-E. Two independent programs record their compile start and end. The two
  intervals overlap when `-j` is 2 or more. The test compares timestamps, not
  wall time.
- B-F. `build.ninja` and the applied directives are byte-identical to those
  of a build at concurrency 1, and the `ran` lines are in the same order.
- B-G. When two programs fail to compile, the reported failure is the first
  in the serial order.
- B-H. Two children started at once from two threads, one of which exits at
  once and one of which sleeps: the reader of the first returns when the
  first exits, not when the second does (B2-0).

**Pack (#749)**
- K-A. #749's criteria A to E. The stage directories are read through
  `pack_stage_dir()` and written to `OUT_DIR`.

**Output streams**
- R-A, R-B and R-C (section 6).

## 11. Self-review of revision 2

1. **The D4 rule versus the #748 measurement.** The rule sends the measured
   workspace's host module to the workspace store, because that module is a
   workspace member and its sources are local. It is still compiled once per
   build, which is #748's saving. The global part adds the bundled module and
   index rules such as `mcpp.plugins`. No gain claimed for #748 depends on the
   global part.
2. **Taint of an index host module.** P7's dependency cache also requires
   that nothing a package was built against is local. A host module is
   compiled alone, against `std` and `mcpp` only. `std` is already global,
   and the bundled module is global under B1. The rule therefore holds
   without a walk. If host modules ever gain imports of other packages, which
   `build_host_module` states they cannot, this must be re-derived. B-D
   guards the local side.
3. **Upgrade debris.** Every mcpp release leaves one stale entry for the
   bundled module per host compiler. It is collected by `mcpp cache gc`, as a
   stale `std` entry is. There is no new eviction mechanism.
4. **R3 and the live region.** When stdout is a pipe and stderr is a
   terminal, as in `mcpp run | less` or `mcpp emit ... > file`, the region is
   now drawn. Today it is not drawn in that case. That is the intended
   effect, and it is also Cargo's behaviour. The reverse case, stderr
   redirected and stdout a terminal, loses the region, which is also
   intended.
5. **R3 and `mcpp test`.** Test programs' stdout is captured into the JSON
   stream or printed on failure; its routing is not changed by R3. Only
   mcpp's own status lines move. R-C checks `build` only. A test-side
   criterion is not added, because the human test report
   (`test result ok. …`) is itself a result and stays on stdout. **This
   boundary, the report as result or as status, is the one open point of R3,
   and PR 4 settles it by the census, following Cargo, which prints
   `test result` on stdout.**
6. **S4 and `--workspace-timeout`.** Today the timeout can stop the fan-out
   between member builds. After S4, the group build is one step and cannot
   be interrupted by it. A workspace whose build alone exceeds the deadline
   used to stop early and now overruns until `--build-timeout`. This is
   stated in `docs/07-workspace.md` §5.3 by PR 1.
7. **S-G under dev-dependencies.** Revision 1 stated S-G without condition.
   A dev-dependency can activate a feature of a shared package and
   legitimately recompile it, as in Cargo. Revision 2 states the fixture's
   condition.
8. **The exit status 101 and `run` under a runner.** With
   `[target.<triple>].runner` or `--runner`, the status that passes through
   is the runner's. A runner that itself returns 101 would read as a failed
   build. This is accepted, as in Cargo, and documented.
9. **Numbers are estimates.** The 25 to 30 s of B1 and #749's 30 to 45 s are
   estimates. B0 replaces the first by a measurement in the landing
   revision.
10. **Cross-project reuse depends on `base`.** The key includes `base`, the
   flags the bundled module is compiled with. By code reading, `base` is
   `host_base_flags(tc, macosDeploymentTarget)`
   (`src/build/build_program.cppm:534`). It is built from the host toolchain
   and the macOS deployment target only, and so carries no project path.
   Cross-project reuse therefore holds. B0 records `base` to confirm this;
   if a project path appears, the bundled module stays in the workspace until
   that is resolved.

## 12. Review from several angles (revision 3)

**Architecture.**
- One selection function replaces two (S1). The selection is data, a set of
  members, and the planner already accepts a set; no planning mode is added.
- B1 is one keyed store with two homes, chosen by provenance (P7). The global
  home reuses `mcpp.bmi_cache`'s entry layout, its recorded inputs and its
  LRU stamp.
- B2 splits `run_build_program` into a compile phase and a run phase. Only
  `step9_member_build_programs` schedules the two apart. The root package's
  program and the dependencies' programs keep the single call.
- K1 divides `build_and_pack` into a build, a stage per member, and a
  dispatch. A single member is the same code with one member.
- R3 is one decision in `mcpp.ui`: the stream that narrates. Every narrating
  function reads it.

**Stability.**
- Directives are applied in the serial order, so the plan of a concurrent
  build is byte-identical to the serial plan (B-F).
- Concurrency is confined to compiles. Runs stay serial (B3 deferred).
- The launcher is made safe for concurrent callers before any concurrent
  caller exists (B2-0).
- A store entry is written under a temporary name and renamed into place.
  Its `entry.json` is written last, so a partial entry is never read.
- S4 reuses the group planning that `emit build-database` and
  `--configure-only` already exercise.
- A member that fails to plan fails alone.

**Simplicity.**
- No manifest key is added.
- The options added are a repeatable `-p` and `--exclude`.
- There is no switch for failing fast, for the stream of status output, or
  for the store's location.

**User experience.**
- A repeated `-p` does what it states. A misspelt member is refused, and the
  refusal lists the members.
- `mcpp run -q > file` writes exactly the program's output. A failed build is
  distinguishable from a failing program (101).
- `test` and `pack` over several members plan once.
- A pipe or a redirection receives a command's result, and progress stays on
  the terminal.

**Compatibility.**
- A single `-p` keeps its meaning. A repeated `-p`, which acted on the last
  member alone, now acts on every member it names; the old behaviour was the
  defect #750 reports.
- The JSON streams gain a record and a field and lose nothing (P8).
- Status lines move from stdout to stderr. This is the one change a script
  can observe. It is stated in the CHANGELOG with its migration (`2>&1`).
  Machine consumers read the JSON forms, which do not move. Section 14
  records that no ecosystem consumer reads status lines from stdout.
- The exit status 101 applies to `run` alone. `build`, `test` and `pack` keep
  theirs.
- Nothing is written to a manifest, so an older engine reads every project
  this release reads.

**Cross-platform.**
- B1 covers the three BMI families. GCC finds BMIs under the compile's
  `gcm.cache`, so a store entry is staged there, as `bmi_cache` stages a
  cached dependency. clang names a BMI with `-fmodule-file=<name>=<path>`, and
  MSVC with `/reference <name>=<path>`.
- Store keys use `u8string()`. A path given to a tool is narrowed with
  `try_narrow`, by the path narrowing rule of the contributing guide.
- B2-0 differs by platform: `pipe2` on Linux, a mutex on macOS, and a mutex or
  an explicit handle list on Windows.
- R3's live region follows the stream it is drawn on. The Windows console
  test `can_move_cursor` is asked of stderr.

**Consistency.**
- `build`, `test`, `pack`, `describe` and `emit build-database` accept the
  same selection. `run` refuses a second member.
- Every report is in manifest order (D5).
- Every command narrates on stderr.

**Upgrade.**
- No project needs an edit, and no cache needs a migration.
- The first build after the upgrade compiles the bundled module once per host
  compiler, because the key holds the mcpp version.
- A script that read status lines from stdout adds `2>&1`.

**Test coverage.**
- Each criterion of section 10 has an e2e script. The selection function and
  the store key have unit tests.
- The census of R3-0 corrects the existing scripts that read status from
  stdout. Each correction captures both streams.
- B1's clang and MSVC paths run in the macOS and Windows e2e shards. B2-0 has
  a test that starts two children at once and requires each reader to finish
  when its own child exits.

## 13. Tasks, ownership and dependencies

| Task | Items | Files owned | Depends on |
|---|---|---|---|
| T1 | S1 to S4, D6, #750 | `src/cli.cppm` (the `package` and `exclude` options of `build`, `run`, `test`, `describe`, `emit build-database`), `src/cli/cmd_build.cppm`, a new `src/cli/selection.cppm` (module `mcpp.cli.selection`), `src/build/execute.cppm` (`run_tests` and its summary only), `src/build/prepare/manifest.cpp` and `src/build/prepare.cppm` (test targets of a group), `src/project.cppm` | none |
| T2 | B0, B2-0, B1, B2 | `src/build/build_program.cppm`, `src/build/hostprogram.cppm`, `src/build/prepare/target_side.cpp` (`step9_member_build_programs`), `src/bmi_cache.cppm`, `modules/platform/src/process.cppm`, `modules/platform/src/unix/*`, `modules/platform/src/windows/*`, `src/build/progress.cppm` (program lines) | none |
| T3 | R1, R2, R3 | `src/ui.cppm`, `modules/platform/src/terminal.cppm`, `src/build/execute.cppm` (the `run` path only), the e2e scripts of the R3-0 census | none |
| T4 | K1 | `src/pack/pipeline.cppm`, `src/cli/cmd_publish.cppm`, `src/cli.cppm` (the options of `pack`), `src/build/prepare.cppm` (the stage directory per member), one hunk in `step9_member_build_programs` and in `src/build/prepare/features.cpp` (the stage directory a program receives) | T1 (the selection module), T2 (the final form of `step9`) |
| T5 | integration | `mcpp.toml` and `modules/versioning/src/version.cppm` (the version), `CHANGELOG.md`, `docs/` and `docs/zh/`, this record | T1 to T4 |

- T1, T2 and T3 proceed at the same time, each on its own branch from the
  integration branch.
- They are merged in the order T2, T1, T3, so that the census of T3 is taken
  against the scripts T1 and T2 add. New scripts capture both streams when
  they assert a status line, so that they hold before and after T3.
- T4 starts on the integration branch once T1 and T2 are merged.
- T5 takes the full e2e suite on the merged branch, locally in shards, then
  opens the pull request.

## 14. Cross-repository work and verification

| Repository | Change | When |
|---|---|---|
| mcpp-community/mcpp | the pull request of section 9, then a release by tag | first |
| xlings-res/mcpp (GitHub and GitCode) | release assets mirrored by `publish-ecosystem`. The GitCode assets are uploaded with a local `gtc` as soon as each appears on the GitHub release | during the release |
| openxlings/xim-pkgindex | the bump pull request that `publish-ecosystem` opens, merged by a maintainer | after the mirror is verified by GET |
| mcpp-community/mcpp-index | `MCPP_VERSION` and `latest_mcpp` move to the new version, and a full scan runs by `workflow_dispatch` | after the index entry is live |
| openxlings/xlings | none. `kXlingsVersion` 2026.9.30.1 is xlings' latest release. xlings' CI reads mcpp's exit status and not its status lines | none |
| mcpp-language-server | none. It reads the `emit build-database --format json` envelope from stdout and the exit status | none |

The release canaries (xlings, mcppls) build with the tagged mcpp before the
platform builds start.

**Verification of the released artifact.** In an xlings subos sandbox, with
the released mcpp addressed at its store path and the CN mirror set inside
the sandbox (`mcpp self config --mirror CN`), a script runs:

1. `mcpp test -p a -p b` on a three-member workspace: both members tested,
   the third untouched (#750).
2. `--exclude`, and the refusal of an unknown member.
3. A workspace of four members whose programs import one host module: the
   host module compiled once (#748).
4. `mcpp pack --workspace` over two members that share a member with a build
   program: each program run once, and one package per member (#749).
5. `mcpp run -q` with stdout redirected: exactly the program's output, and
   exit 101 for a compile error.

Each section reports ok, failed or not run separately. The script's result is
posted on #748, #749 and #750, which are then closed.
