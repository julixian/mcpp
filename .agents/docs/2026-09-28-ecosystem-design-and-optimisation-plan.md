---
subject: design
status: landed
---

# An ecosystem design for mcpp and xlings: one authority per fact, and the work that follows from it

**Status:** landed, revision 4 (2026-09-28). The design is settled; §7 divides it
into tasks, and §8 records their implementation: mcpp 2026.9.28.2 and xlings
2026.9.28.2 are released and indexed.

- **Revision 1** proposed the design and seven decisions.
- **Revision 2** records the reviewer's answers: D1 to D7 are settled as
  recommended, and D3 states its reasons (§5). It also adds a self-review of the
  whole plan (§6), whose findings changed WS1, WS3, WS5, WS7, WS8, WS9, WS10 and
  the order.
- **Revision 3** divides the workstreams into tasks per repository, with the
  dependencies between them and the criterion of each (§7). Facts found while
  dividing them, which change no decision, are recorded in §7.4.
- **Revision 4** records the implementation (§8): where it departed from §7,
  nine findings, the before and after reading of each task, and a self-review.

**Input.** The review `2026-09-28-ecosystem-review-of-two-days-of-mcpp-and-xlings.md`
(cited below as "the review §n"), the #717-#726 round's records, and the
specifications SPEC-004 to SPEC-007 and S1.

## 0. The question asked first: is the current CRT placement right?

The review §2.1 found that on a Windows program linking Qt:

- the CRT DLLs in `xim:qt-base`'s `bin/` are placed beside the program;
- the MSVC toolset's own copies are dropped;
- a warning attributes Qt's files to "this project".

### 0.1 Against the industry norm

**What the norm says.**

1. **Version.** Microsoft states the rule for mixing binaries built by different
   toolsets of the v14 line: the redistributable must be at least as new as the
   newest toolset used by any component of the application. A newer CRT serves
   older binaries; an older CRT does not serve newer ones.
2. **Deployment models.** Two models exist.
   - Central: the redistributable installer. Microsoft recommends it, because
     Windows Update then services the CRT.
   - Local: the DLLs from `VC\Redist\MSVC\<version>\<arch>\Microsoft.VC14x.CRT`
     copied beside the program.

   In local deployment, the copy beside the program takes precedence over the
   installed one, because the loader searches the application directory first
   and these DLLs are not KnownDLLs. So a local copy that is older than required
   breaks the program even on a machine that has a newer CRT installed.
3. **What established tools copy.**
   - CMake's `InstallRequiredSystemLibraries` copies the CRT of the compiler in
     use (`MSVC_REDIST_DIR`).
   - `windeployqt` takes the runtime from the toolset's installation
     (`VCINSTALLDIR`), not from Qt's `bin/`.
   - vcpkg's app-local deployment copies only the DLLs of its installed tree.

   None of them takes the CRT from a third-party library's directory.

**Verdict.** The current behaviour does not meet the norm.

- It chooses the CRT by where a file happened to be found, not by version.
- It can place a CRT older than the toolset that compiled the program. MSVC
  14.51 compiled this program, while Qt 6.11.1 was built by an earlier toolset.

### 0.2 Against mcpp's own design

1. **The contract names the toolset.** SPEC-006 §3.7 defines `toolchain-coupled` on
   the MSVC ABI as: the dynamic CRT, with "the selected toolset's own
   `vcruntime140.dll`/`msvcp140.dll` placed beside the artifact". The recorded
   contract says toolchain-coupled; the files placed are Qt's. The build prints a
   promise and ships something else, which is the shape `pack.cppm` names as a
   contract with no executor.
2. **One destination, one writer.** SPEC-007 R4.3 makes the deploy list, "R4.2
   merged with the toolchain-coupled runtime DLLs", the single authority.
   - The plan-time scan of runtime search directories writes into that same
     list, so a derived file sits in the authority's own table.
   - The staging then cannot distinguish a declared file from a derived one.
3. **Explicit outranks derived.** The staging site's comment says a declared
   `[runtime] deploy_files` wins "because a human wrote that one down; this list
   is derived". The same principle ranks the toolset's runtime above a file found
   in a dependency's directory.

**Verdict.** The current behaviour contradicts SPEC-006 §3.7 and the intent of
SPEC-007 R4.3.

### 0.3 The fix, checked against both

**The proposed order:** a declared file, then the toolset's runtime, then a
derived file.

- It meets mcpp's design as written.
- Against the norm it is right in the common case, where the program's toolset
  is the newest. Here that is 14.51 against Qt's older build.
- It is not complete in the other case: a prebuilt dependency built by a *newer*
  toolset than the program's. Then the norm asks for the dependency's newer CRT,
  or for a newer toolset.

**The complete rule** (§2.1 below):

- **The CRT is one versioned set.** Never mix `vcruntime140.dll` of one version with
  `msvcp140.dll` of another.
- **The set is chosen as the newest complete set among its sources.** Ties go to
  the toolset.
- **A declared file wins over the choice, and is checked against the requirement.**
  A file older than the newest toolset that built any image of the program is
  refused, with both versions named. The refusal applies once the floor's reading
  is measured reliable, and is a warning until then (§2.1, D2).
- **A dependency that ships the CRT is a packaging fault**, reported once, never a
  silent source (§2.9).

## 1. Principles

The review's findings reduce to seven principles. Each workstream below is one of
them applied.

| # | Principle | Violated by (the review) |
|---|---|---|
| P1 | **One authority per fact.** A fact that several stages need is decided once, by one function, and the stages read the decision; they do not re-derive it. | CRT placement decided in four places (§2.1); home identity inferred by two walks (§3.5) |
| P2 | **A declaration outranks a toolchain fact, which outranks a derived observation.** A derived answer never sits in the table of declared ones. | §2.1, §2.4 |
| P3 | **The producer sends data; the consumer owns its rendering state.** No wire field carries a renderer's state. | `prevLines` in xlings's `download_progress` (§2.2) |
| P4 | **One statement per fact per run, attributed to its source.** | §2.3 |
| P5 | **Identity is declared, not inferred from shape.** | §3.5 |
| P6 | **A check asserts what its name says, or it does not exist.** | #729, and the earlier false greens |
| P7 | **A release is gated by its consumers, at the intersections its changes create.** | the regressions of 2026.9.27.1 (review §4.5); the pinned canary (§4.4) |

## 2. Workstreams

Each workstream states its design, its tasks, and a criterion that fails before
the change and passes after it.

### 2.1 WS1 · mcpp · One resolver for a Windows program's runtime files (P1, P2)

**Design.** One function, the runtime placement resolver, answers for each name
beside a PE program where its bytes come from. The build's staging edges, the
post-link `place-dlls` edge and `mcpp pack` all consume its answer.

- **The input: candidate sources, by kind.**
  - `declared`: R4.2 `deploy`, `[runtime] deploy_files`.
  - `toolchain`: the selected toolset's `Microsoft.VC*.CRT` set.
  - `derived`: a DLL found in a runtime search directory.
- **The CRT is a set.** The names in the toolset's CRT directory form one set.
  - Candidates for a CRT name are compared by the file version in their PE
    `VERSIONINFO`, and the whole set moves together.
  - A derived CRT file never enters the plan unless its set is complete and
    strictly newer than the toolset's. It is then reported once as "the program's
    CRT comes from <package> <version>, newer than the toolset's <version>".
- **Order.**
  - A declared name wins, subject to the version floor (refused when older than
    the newest toolset among the program's images).
  - Then the chosen CRT set.
  - Then the derived names.
- **One record.** `resolution.json` records the choice for each name, so that
  `mcpp why runtime` and `mcpp pack` read it rather than repeat it.
- **Timing.** The post-link edge re-evaluates only the names whose candidates did
  not exist at planning, which are directories a `prepare` action fills. It uses
  the same resolver, linked into `place-dlls`.
- **The contract governs the kind, not only the order** (self-review §6.1).
  - Under `host-coupled`, no CRT name is placed from any source: the system's CRT
    serves the program. A derived CRT file is dropped, as a declared one would be
    refused.
  - Under `self-contained`, the program imports no CRT DLL, so no CRT name
    arises.
  - Today the derived scan places a dependency's CRT even under `host-coupled`,
    because it does not read the contract.
- **The version floor, measured before it is enforced** (self-review §6.2).
  - The floor is "the newest toolset that built any image of the program". A PE
    image records its linker's version in the optional header
    (`MajorLinkerVersion.MinorLinkerVersion`, for example 14.44).
  - Before D2 refuses anything, the resolver's reading of that field is compared
    with the known toolsets of the program, of Qt 6.11.1 and of two vcpkg ports.
  - Until the reading is shown to be reliable, a declared file below the floor is
    a warning naming both versions, not a refusal.
- **A reader that cannot answer does not decide** (self-review §6.3). When a
  candidate's `VERSIONINFO` cannot be read (a stripped resource section, an
  unknown layout), the resolver falls back to the kind order and says so once. It
  never compares a missing version as older or newer.
- **Scope: the MSVC ABI first** (self-review §6.4). MinGW's runtime set
  (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) has the same
  shape: it takes the kind order and no version rule, because those DLLs carry no
  reliable `VERSIONINFO`.

**Tasks.**
- Extract the resolver.
- Make the plan-time scan, the staging in `flags.cppm`, `place_runtime_dlls` and
  `mcpp pack` consume it.
- Add a PE `VERSIONINFO` reader (the PE import reader already exists).
- Record the choice in `resolution.json`.

**Criteria.**
- e2e 814 on Windows, with a search directory holding an *older* same-named CRT:
  the toolset's set is placed and no clash warning appears.
- The same with a *newer* complete set: that set is placed, with one note.
- e2e 818 (the cross form) and a unit property test of the resolver over every
  combination of kinds and versions.
- `mcpp pack` places the same files as the build.

**Specification.** SPEC-006 §3.7 and SPEC-007 R4.3 gain the order and the set rule.

### 2.2 WS2 · mcpp · Header dependency tracking on every row (P1)

**Design.** Whether a compile unit emits a GNU depfile is a property of the
compiler, not of the host. The awk filter is a property of GCC's module depfile
only.

- **Clang, on every host:** `-MMD -MF $out.d` with `deps = gcc`.
- **GCC on POSIX:** keeps its filtered form.
- **GCC on Windows (MinGW):** gets a filter that needs no awk, written in the
  existing `mcpp` subcommand family (`mcpp depfile-filter`), and loses its
  degradation too.

**Criterion.** A Windows e2e edits a header included in a module purview and
asserts that the importing object is rebuilt. It must fail on 2026.9.28.1, and
the Windows CI default row runs it.

### 2.3 WS3 · mcpp · The diagnostics model (P4)

**Design.**

- **Once per run.** Every diagnostic carries a code, a text and a source
  (manifest path and table, or package). The terminal prints each (code, text)
  once per process. The envelope keeps every occurrence, with its member.
- **The source is where the value was written.**
  - An inherited value names its source table: `[workspace.build]` when a member
    inherited it.
  - The manifest loader records the source of each inherited key, as it already
    records the workspace root.
- **Edge advisories.**
  - A build edge that has something to say on success (`place-dlls`,
    `mcpp stage`) writes `<stamp>.advice`.
  - After a successful build, mcpp prints the advisories of the edges that ran
    this time, then deletes them.
  - This is the build-program `tag` channel given to ninja edges. It replaces
    ad hoc planning-time duplicates such as the one #727 added for R4.3.
  - One function reads and prints the advisories, and both the full path and the
    fast path (`run_ninja_fast`) call it (self-review §6.5). Two paths that
    report the same thing in two places is the shape `execute.cppm` already warns
    about.

**Criteria.**
- A workspace of five members with one inherited redundant word prints one
  warning naming `[workspace.build]`.
- A `prepare`-filled search directory with a differing DLL prints the advisory
  once without `-v`.

### 2.4 WS4 · xlings and mcpp · Progress: data on the wire, state in the renderer (P3)

**Design.**

- **Protocol 1.3.** `download_progress` carries data only: files, bytes, elapsed
  time, and a stream id (the label for an index sync, the install batch for an
  install). `prevLines` is deprecated: still accepted from older producers, and
  ignored by 1.3 consumers.
- **The CLI renderer owns its frames.** xlings's renderer keeps the frame state
  per stream id, as mcpp's `DownloadProgress` already does. Producers stop
  computing it.
- **One throttle at the consumer.** Frames are drawn at most every 100 ms on a
  terminal. Off a terminal, one line starts an item and one line finishes it.
  Every producer then gets both properties for free.
- **Coalescing at the producer** (self-review §6.10). The producer emits at most
  ten `download_progress` events per second per stream, and always the final one.
  This bounds the data on the wire; it carries no rendering state.

**Tasks.**
- xlings: the renderer change, protocol 1.3, and the index path's stream id.
- mcpp: none required. It already owns its state; it accepts 1.3 unchanged.

**Criteria.**
- A pseudo-terminal e2e of `xlings update`: the number of frames per index is
  bounded by elapsed time over 100 ms.
- The non-terminal form: exactly two lines per index.
- The same two assertions for `xlings install`, which must not regress.

### 2.5 WS5 · xlings · Declared home identity (P5)

**Design.**

- **A marker.** `self init` writes `<home>/.xlings-home` containing the home's
  identity (a random id and its path at creation). A SubOS never has one.
- **One resolver.** `resolve_owner_home` walks up to the nearest marker. The
  structural `is_home_root` is removed.
- **A nested home is supported:** the nearest marker wins, and no `subos/<name>/`
  segment re-roots a path that belongs to an inner home.
- **Migration.** A home without a marker is recognised by the old signature once,
  and the marker is written then.
- **A read-only home keeps working** (self-review §6.6). A home on read-only
  storage (a CI cache restored read-only, a mounted image) cannot receive the
  marker. The failed write is not an error: the old signature answers for that
  run, and a `self doctor` note says the marker is missing.

**Criteria.**
- #617's shim handoff targets `<home>/bin/xlings` from a SubOS.
- #624's nested home installs `gcc` with its programs registered.
- A home created by 2026.9.28.1 gains its marker on the first command of the new
  version.

### 2.6 WS6 · xlings · `update` does what it says, once

**Design.**

- **One rebuild.** `update` takes the catalog without its implicit first rebuild
  and performs only the forced one.
- **Messages for switching an existing payload.** When the target version is
  already in the store, the messages are "xim:mcpp@2026.9.28.1 is in the store"
  and then "active: 2026.9.27.1 -> 2026.9.28.1".

**Criterion.** `xlings update` runs each index's build script once, counted by its
`[n/n]` lines. The messages are asserted verbatim in the e2e.

### 2.7 WS7 · CI integrity (P6)

**#729.**
- The LLVM step fails on the build's own exit status (`set -o pipefail`).
- The step builds mcpp with llvm@22.1.8, the row that Windows CI resolves and
  that builds mcpp today.
- The function-size gate runs after it, with `xim:llvm-tools@22.1.8`.

**A workflow lint.**
- `.github/tools/check_workflow_assertions.py` flags a `run:` block that pipes a
  command into `tee` or `grep` without `pipefail`.
- It flags a step whose only assertion is a text match unrelated to its name's
  verb ("build", "test", "install").
- It runs in the static-checks job.

**Known red is machine-readable.**
- A job that is red for a tracked external reason (the xcode-27 row, #669) is
  marked `continue-on-error: true` with the issue number in the job name. Its
  failure then does not fail the workflow, while its own result and log stay
  visible.
- The merge rule reads "green" literally again: a workflow is green or it is not.
- A job leaves the list when its issue closes; the lint checks that every marked
  job names an open issue.

### 2.8 WS8 · Defaults stated once (P1)

- The default toolchain for each host is answered by one function.
- The answer is exposed through the existing machine surface: a
  `default_toolchain` field of `mcpp self env --format json`. No new command is
  added (self-review §6.7).
- The tables in `docs/01` and `docs/20` (en and zh) are checked against it by a
  script in the docs job. The review found them stating llvm@20.1.7 while Windows
  resolves llvm@22.1.8.

### 2.9 WS9 · Ecosystem data (P2, P7)

**xim-pkgindex payload lint.**
- A payload whose runtime directory contains a CRT name from the MSVC set is
  flagged.
- **D3 is settled: a library payload does not carry the compiler's runtime.** The
  recipe removes the files; `bundles_crt` is not introduced. The reasons are in
  §5, under D3.
- **`xim:qt-base` 6.11.1 is the first case.**
  - Its recipe states `revision = 1`, and its payload is rebuilt without the CRT.
    xlings's packaging revision (2026.9.27.1, #622) then replaces every installed
    copy on its next use, instead of leaving machines on the old payload.
- **The build-time tools still need a CRT** (self-review §6.8).
  - Qt's `bin/` holds the libraries a program loads and also the host tools the
    build runs (`moc.exe`, `rcc.exe`, `uic.exe`). rules-qt finds those tools in
    `bin/` or `libexec/`, and they need a CRT to start.
  - The engine therefore puts the toolset's CRT directory on the `PATH` of every
    action it runs for a Windows target. It already does so for `mcpp run` and
    `mcpp test`.
  - The build then has one CRT, the toolset's, for the program and for the tools
    that build it. That is WS1's authority applied to build time.
- **Measured before the payload changes.** On the "bare Windows, no Visual
  Studio" CI row and on a runner with Visual Studio, Qt's `moc.exe` must start
  from a payload without the CRT, with only the action `PATH` supplying it.
  - If either row fails, the fallback keeps D3's rule for the directory
    consumers search: the recipe moves the host tools, a copy of the Qt DLLs they
    load, and a tool-private CRT into `libexec/`, where rules-qt already looks.
  - `bin/` then still carries no CRT.

**mcpp-index red baseline.**
- The two failing full sweeps on `main` (review §3.6) get an issue and an owner:
  `mirror-cn-reachable` and `pangocairo` on linux.
- The index's CI summary lists a failing member together with its issue, and a
  failing member without an issue fails the summary job.

### 2.10 WS10 · The release as a gate (P7)

**In-repository verification.**
- The sandbox verification script of this round becomes
  `tests/release/verify-published.sh` in mcpp.
- It is extended by one assertion per released item. It is run against the new
  and the previous version in two fresh SubOS environments, and both readings go
  into the release record.

**Canary projects.**
- `.github/release-canaries.toml` lists real projects with their build commands:
  GalTranslPP, mcppls, the xlings self-build.
- A release-candidate workflow builds each with the candidate mcpp, rewriting the
  project's pin in its own checkout, never by a commit to the project
  (self-review §6.9). No new override mechanism is added to xlings for this.
- A canary failure blocks the tag.

**Intersection checklist.** The release PR template asks, for each new rule or
feature, which existing invariant it crosses and which test sits at the crossing.
The review §4.5 names three crossings in 2026.9.27.1 that had none.

**Attribution.**
- Every squash merge carries an explicit subject and body.
- A branch is checked for attribution trailers before merging.
- Subagent prompts forbid them. The last of these is already recorded in working
  memory.

## 3. Order, repositories and versions

```
xlings  (next patch)   WS4 (renderer, protocol 1.3, producer coalescing), WS6,
                       WS5 (marker + resolver; its own version if D6 applies)
        |
mcpp    (next patch)   WS1 (resolver, contract rule, VERSIONINFO, floor as warning),
                       WS1/D3 action PATH, WS2, WS3, WS7, WS8, D7 (SPEC-004),
                       xlings pin -> the xlings patch
        |
measure                moc.exe from a CRT-less qt-base on two Windows rows
        |
xim-pkgindex           WS9 payload lint; qt-base 6.11.1 revision 1 without the CRT
mcpp-index             CI pin -> the mcpp patch; red-baseline issues; summary rule
        |
release                WS10 verification script and canaries, from this release on
```

- **One PR per repository.** Versions are named by the release date when each is
  cut (`YYYY.M.D.N`).
- **xlings first.** Protocol 1.3 is additive, and mcpp's pin moves after it.
- **WS1 before WS9.** WS1 lands in mcpp before the qt-base recipe changes, so the
  mcpp fix is observed on the unchanged payload first: GalTranslPP on Windows
  must place the toolset's CRT with no warning.
- **The measurement gates the qt-base payload.** The payload changes only after
  `moc.exe` is shown to start from a CRT-less payload through the action `PATH`,
  on both Windows rows (§2.9).

## 4. Compatibility and upgrade

- **WS1 changes which files sit beside a program.** A Windows program that links
  a dependency shipping an older CRT now receives the toolset's CRT.
  - This is the documented contract, and the CHANGELOG states it under
    Compatibility.
  - A project that relied on the dependency's copy declares it and gets the
    version floor check, as a warning until §2.1's measurement.
  - `mcpp pack` output changes the same way.
  - Under `host-coupled`, a dependency's CRT is no longer placed at all.
- **WS1/D3 puts the toolset's CRT directory on Windows actions' `PATH`.** An
  action that relied on a different CRT on `PATH` now finds the toolset's first.
  That is the intended single CRT of the build.
- **WS2 adds depfiles on Windows.** The first build after the upgrade is a full
  one on that row, because the compile commands change.
- **WS4 keeps `prevLines` accepted.** An older xlings with a newer mcpp, and the
  reverse, both render correctly.
- **WS5 migrates in place.** A home without a marker keeps working and gains the
  marker. A read-only home keeps the old signature. A nested home that failed
  before now works.
- **WS7's known-red marking.** Those jobs stop failing their workflow; their
  results and logs stay, and the lint keeps them tied to an open issue.
- **D7 can reorder list values.** A manifest whose several matching conditional
  tables append to one list receives them in specificity order instead of
  lexical order. The CHANGELOG names the case; a scalar key's winner changes only
  where two matching tables set it.
- **The qt-base revision reinstalls the payload once** on every machine that uses
  it, through #622's mechanism.

## 5. Decisions (settled 2026-09-28)

The reviewer accepted every recommendation of revision 1.

| # | Decision | Settled |
|---|---|---|
| D1 | CRT choice when a complete dependency set is newer than the toolset's | the newer set is taken, with one note (§2.1) |
| D2 | A declared CRT file older than the toolset | refused, naming both versions; enforced once the floor's reading is measured reliable, a warning until then (§2.1, §6.2) |
| D3 | xim:qt-base's bundled CRT | removed from the payload; not declared (reasons below) |
| D4 | Edge advisories (WS3) | in this round |
| D5 | Known-red CI jobs | `continue-on-error`, the issue in the job name, and the lint (WS7) |
| D6 | WS5's version | its own xlings version if its migration review is not complete when the rest is |
| D7 | #728, the order of conditional tables | precedence by selector specificity (a triple over an OS over a family), lexical order only as the tie-break |

**Why D3 removes the CRT instead of declaring it.**

1. **The runtime belongs to the application's deployment, not to a library.**
   Microsoft's rule ties the redistributable to the newest toolset of the
   *application*, and CMake and `windeployqt` take it from the toolset in use. A
   library that ships the CRT pre-decides that version for every program that
   links it.
2. **A bundled copy is frozen and serviced by no one.** It stays at the version of
   the toolset that built the library. Placed beside a program, it shadows a newer
   system CRT, because the loader searches the application directory first.
3. **Declaring it keeps the conflict and adds a mechanism.** With
   `bundles_crt = "<version>"`, every Qt program would still see two candidates for
   ten names, and every build would run the comparison. Removal leaves one
   candidate in the common case. WS1's comparison then remains only where the case
   is real: a closed prebuilt that genuinely needs a newer CRT, which D1 serves
   without any new key.
4. **Removal reaches existing machines by itself.** The recipe's `revision = 1`
   (#622) replaces installed payloads on their next use, so no one has to clean up
   by hand.
5. **The need the files served is met by the build's own CRT.** Qt's build-time
   tools need a CRT to start. The action `PATH` supplies the toolset's (§2.9),
   which is the same CRT the program receives. Removal does not move the problem
   to build time; the measurement in §2.9 confirms it before the payload changes,
   and a fallback that keeps `bin/` free of the CRT is stated.

## 6. Self-review of the plan (revision 2)

Each finding below changed the plan; the section it changed is named.

| # | Finding | Change |
|---|---|---|
| 6.1 | WS1 ordered the sources but did not read the contract. Under `host-coupled` the derived scan places a dependency's CRT today, which contradicts "no file is placed" in SPEC-006 §3.7 | the contract governs the kind: no CRT name under `host-coupled` from any source (§2.1) |
| 6.2 | D2's floor ("the newest toolset of any image") needs each image's toolset, and the PE linker-version field has not been shown to give it | measured against known images first; a warning until then, a refusal after (§2.1, §5) |
| 6.3 | A `VERSIONINFO` that cannot be read would have compared as some version | an unreadable version never decides; the kind order applies and says so once (§2.1) |
| 6.4 | The CRT-set rule is MSVC-specific, and MinGW's runtime has the same placement shape without reliable versions | MSVC first; MinGW takes the kind order with no version rule (§2.1) |
| 6.5 | Edge advisories printed from the full build path only would repeat #727's two-path shape: the fast path skips `prepare` | one printing function, called by both paths (§2.3) |
| 6.6 | Writing the home marker can fail on read-only storage, and a failed write must not fail a command | the old signature answers that run; `self doctor` notes it (§2.5) |
| 6.7 | A new `mcpp toolchain default --print` command would add a surface for one fact that the machine output already has a place for | a field of `self env --format json` (§2.8) |
| 6.8 | D3's removal would stop Qt's build-time tools on a machine with no system CRT: they live in the same `bin/` | the toolset's CRT on Windows actions' `PATH`, measured on two rows before the payload changes, with a stated fallback (§2.9) |
| 6.9 | "Overriding the pin through the environment" presupposed an xlings feature that does not exist | the canary rewrites the pin in its own checkout (§2.10) |
| 6.10 | Throttling only at the consumer leaves the wire carrying one NDJSON line per received chunk, which a consumer parses in full | the producer coalesces data events to at most ten per second per stream, plus the final one; this bounds data, it carries no rendering state (WS4, §3) |
| 6.11 | The versions were written as 2026.9.28.2, a date the releases may not fall on | versions are named by the release date when cut (§3) |
| 6.12 | D7 changes the order of appended list values in a manifest with several matching tables | stated under Compatibility (§4); SPEC-004 §3.1.1 is restated with the rule and a test of mixed specificities |

**Checked and unchanged.**

- **Routing.** WS1 to WS3, WS7 and WS8 are engine defects or engine consistency;
  WS9's qt-base change is ecosystem data; the action `PATH` is a general engine
  rule that names no package. This follows the routing rule (a defect belongs to
  the engine; a package's content belongs to its recipe).
- **Specifications to amend.**
  - SPEC-006 §3.7: the set rule and the contract's kinds.
  - SPEC-007 R4.3: the order and the edge advisory.
  - SPEC-004 §3.1.1: D7.
  - The xlings interface specification: protocol 1.3 and coalescing.
  - The xim recipe specification: the payload lint rule.
  - docs/20 and docs/50, in both languages.
- **Criteria.** Every workstream has one that fails before its change and passes
  after it.
  - WS1's run on Windows CI, with its Linux cross form in e2e 818.
  - The `VERSIONINFO` reader is unit-tested on every host with small synthesized
    PE files.
- **Nothing depends on a later step.** Each step's inputs exist by the time it
  runs: the measurement needs WS1/D3's action `PATH` from mcpp, and the qt-base
  change needs the measurement.

## 7. Tasks, dependencies and criteria (revision 3)

The workstreams of §2 are divided below into tasks, one pull request per
repository. Each task names the criterion that fails before it and passes after
it. A task that depends on another names it in the last column.

### 7.1 xlings (one pull request, one release)

| Task | Workstream | Change | Criterion | Depends on |
|---|---|---|---|---|
| X1 | WS6 | `update` takes the catalog without its implicit first build and performs only the forced one | an offline e2e counts the fixture index's `[i/n]` lines: one pass, not two | — |
| X2 | WS6 | a target already in the store reads "`<pkg>@<v>` is in the store", then "active: `<a>` -> `<b>`" | the same e2e asserts both lines verbatim | X1 |
| X3 | WS4 | protocol 1.3: `download_progress` carries a `stream` id and no `prevLines`; producers coalesce to ten events per second per stream, the final one always sent | a unit test of the coalescer; an interface e2e counts the events of one stream | — |
| X4 | WS4 | the CLI renderer keeps each stream's frame state, draws at most every 100 ms on a terminal, and off a terminal prints one line when an item starts and one when it finishes | a pseudo-terminal e2e of `update` bounds the frames by the elapsed time; the non-terminal form prints exactly two lines per index; the same two for `install` | X3 |
| X5 | WS5 | `self init` writes `<home>/.xlings-home`; one predicate `is_home` (the marker, else the old signature excluding a SubOS) answers for `resolve_owner_home`; `normalize_subos_paths` leaves a path owned by an inner home unchanged; a home without a marker gains it on its first command, and a read-only home keeps the old signature for that run | #617: a stale tool shim in a SubOS hands off to `<home>/bin/xlings`; #624: a home nested under another home's SubOS installs a package whose hook path lies under the inner home; a marker-less home gains the marker; a read-only home still runs | — |
| X6 | — | the interface specification (1.3, coalescing), the home-identity note, the release version | the documents state what the code does; `check` scripts pass | X1-X5 |

### 7.2 mcpp (one pull request, one release)

| Task | Workstream | Change | Criterion | Depends on |
|---|---|---|---|---|
| M1 | WS1 | one resolver for the names beside a PE program: kinds (declared, toolchain, derived), the CRT as one versioned set, the contract governing the kind, the version floor as a warning, an unreadable version never deciding, MinGW by kind only; a PE `VERSIONINFO` reader and the optional header's linker version; the plan scan, the `flags.cppm` staging, `place-dlls` and `mcpp pack` read its answer; `resolution.json` records it | a unit property test over every combination of kinds, versions and contracts; the reader tested on synthesized PE files on every host; e2e 814 (Windows) and 818 (cross) with an older and a newer same-named set; `mcpp pack` places what the build placed | — |
| M2 | WS1/D3 | every action of a build for a Windows target has the toolset's CRT directory first on `PATH` | a Windows e2e whose action reports its `PATH`; the measurement job of §2.9 on two Windows rows | M1 |
| M3 | WS2 | a GNU depfile for clang on every host; MinGW GCC filtered by `mcpp depfile-filter`, which runs the compile and needs no shell | a Windows e2e edits a header included in a module purview and asserts the importer is rebuilt; it fails on 2026.9.28.1 | — |
| M4 | WS3 | each diagnostic printed once per process by (code, text); an inherited value named at `[workspace.build]`; edge advisories written beside a stamp and printed after a successful build by one function that both the full and the fast path call | a five-member workspace prints one warning naming `[workspace.build]`; a `prepare`-filled search directory's differing DLL is reported once without `-v` | M1 |
| M5 | WS7 | #729: `pipefail`, llvm@22.1.8 and the function-size gate after it; `check_workflow_assertions.py` in the static checks; known-red jobs marked with their open issue | the lint's fixture tests; the lint fails on the workflows of `origin/main` and passes after the change | — |
| M6 | WS8 | one function answers each host's default toolchain; `mcpp self env --format json` reports it as `defaultToolchain`; a script checks the tables of docs/01 and docs/20 (en, zh) against it on each host's CI row | the script fails on a table that states another version | — |
| M7 | D7 | matching conditional tables apply in order of selector specificity (triple, then OS, then family), lexical order breaking ties | unit tests over mixed specificities fail under lexical order | — |
| M8 | WS10 | `tests/release/verify-published.sh`; `.github/release-canaries.toml` and a candidate workflow that release.yml runs before the tag; the pull-request template's intersection checklist | the script run against the new and the previous release in two fresh SubOS; the candidate workflow run on the pull request | M1-M7 |
| M9 | — | SPEC-004 §3.1.1, SPEC-006 §3.7, SPEC-007 R4.3; docs/01, docs/20, docs/50 in both languages; CHANGELOG; the version; the xlings pin moves to the release of §7.1 | `check_docs_style.sh`, `check_docs_structure.sh`, `check_version_pins.sh` | X6, M1-M8 |

### 7.3 Ecosystem data (one pull request each)

| Task | Repository | Change | Criterion | Depends on |
|---|---|---|---|---|
| I1 | xim-pkgindex | a static test: no recipe places a file of the MSVC CRT set into a payload | it fails on `origin/main` (`qt`, `qt-base`) and passes after I2 | — |
| I2 | xim-pkgindex | `qt-base` and `qt` 6.11.1 without the `vcruntime` module, `revision = 1` | I1; the Windows install test of both recipes | M2's measurement |
| I3 | xim-pkgindex | the recipe rule in the index's documentation | — | I1 |
| N1 | mcpp-index | the CI pin and `latest_mcpp` move to the release of §7.2 | the pull request's validation | M9 released |
| N2 | mcpp-index | the summary lists a failing member with its issue; a failing member without one fails the summary job | a failing member without an issue turns the summary red in a fixture run | — |
| N3 | mcpp-index | an issue records the two red sweeps of 2026-09-26 and the sweep of 2026-09-27 that passed | — | — |

### 7.4 Facts found while dividing the work

- **The CRT in `xim:qt-base` is the recipe's, not Qt's.** Qt's archives carry no
  CRT. The recipe adds a `vcruntime` module, Microsoft's 14.44 redistributable,
  picked into `bin/` on windows-x86_64 only. `xim:qt` carries the same module. D3's
  removal is therefore the removal of one list entry from each recipe, and I1 flags
  both.
- **The system directory precedes `PATH` in the DLL search order.** On a machine
  with the VC++ redistributable installed, a Qt tool loads the system's CRT
  whatever `PATH` holds; `PATH` supplies it only where the system has none. Every
  GitHub Windows image has one, so the measurement of §2.9 must hide the system's
  copy for its first leg to be able to fail.
- **The mcpp-index baseline recovered on its own.** The scheduled sweep of
  2026-09-27 on `main` passed every member. N3 records the two red sweeps and
  this reading; N2 is the rule that keeps the next one from going unread.
- **The false green of #729 has three siblings.** The musl-gcc step and the GCC
  cold rebuild in `ci-linux.yml` and the LLVM step in `ci-windows.yml` pipe the
  build into `tee` and assert only the resolution line. M5's lint names all four.
- **The docs' `llvm@20.1.7` is what the first-run default picks.** The tables of
  docs/01 and docs/20 agree with `native_first_run_spec` on macOS and on Windows
  with MSVC. The `llvm@22.1.8` readings of the review came from project pins
  (mcpp's own `[toolchain] macos`, GalTranslPP's manifest). M6 makes the function
  the one authority and the tables its checked copies.
- **mcpp reads no `prevLines`.** Its renderer already owns its frames, so X3 can
  drop the field from the producers without a change in mcpp.

### 7.5 Order

```
X1-X6  xlings pull request -> CI -> merge -> release (latest moved by the bot PR)
M1-M8  developed in parallel with X's CI
M9     pins X's release -> CI on three hosts, with M2's measurement -> merge -> release
I1-I3  after M2's measurement is green -> merge -> index artifact
N1-N3  after the mcpp release is indexed
verify verify-published.sh against the new and the previous pair in fresh SubOS
       sandboxes with the CN mirror; GalTranslPP on Windows with the new mcpp and
       qt-base revision 1; then the issues are closed with their readings
```

## 8. Implementation record (revision 4)

The tasks of §7 were implemented on 2026-09-28 in one pull request per
repository: openxlings/xlings#628 (X1 to X6), mcpp-community/mcpp#730 (M1 to
M9), an xim-pkgindex pull request (I1 to I3) and an mcpp-index pull request (N1,
N2), with the issue mcpplibs/mcpp-index#482 (N3). This section states what
landed where the implementation departed from §7, what was found while
implementing it, and the readings.

### 8.1 Departures from §7

- **X3.** `prevLines` is kept, deprecated and always 0, rather than dropped: a
  minor protocol version only adds (interface specification 1.3).
- **M4.** The edge-advice channel (SPEC-007 R4.5) is a rule for every build
  edge, not a mechanism of `place-dlls`: an action writes
  `.mcpp-advice/<its output>.advice` the same way. e2e 821 reads it through an
  action on both build paths, because on Linux no engine edge writes it.
- **M5.** `check_workflow_assertions.py` also accepts a `PIPESTATUS` read on the
  line after the pipe (rule W2), which the workflows use.
- **M8.** `verify-published.sh` takes `M` and `XS`, binaries to verify in place
  of the published ones, so that it can be rehearsed before a release; a run
  that uses either says so at its start and its end.
- **I2.** `installed()` does not assert that the runtime files are absent. The
  packaging revision is what replaces an installed payload (xlings
  2026.9.27.1), and a line naming the runtime files would be the one the static
  test of I1 refuses.

### 8.2 Found while implementing

- **F1. A Windows test renamed a directory that a scanner still held.** E2E-01
  (`bootstrap_home_test.ps1`) renamed the portable home 0.2 s after `self init`
  and failed with "You do not have sufficient access rights"; the same failure
  had occurred on 2026-09-14 on a branch that did not touch init, and passed on
  that branch's next run. The test now renames with `[IO.Directory]::Move`,
  which either renames or leaves the tree intact, retries for at most 10 s, and
  on failure names the processes running from the tree.
- **F2. A second producer of terminal frames.** Off a terminal, the sub-index
  build scripts (`xim-pkgindex-awesome`, `-scode`, `-d2x`) still write
  `\r[i/n] <ns>::<file>\033[K`; they run in-process and write to stdout
  directly. It is the class of #626 in a producer X4 did not cover, and it is
  present in 2026.9.28.1 (openxlings/xlings#629, open). `verify-published.sh`
  asserts the download lines and reports these frames as a reading.
- **F3. The index's sweep alert could not open its issue.** The job checks
  nothing out, so `gh` could not infer the repository ("not a git
  repository"), and it watched only the `workspace` job. The two red sweeps of
  2026-09-26 therefore opened nothing (mcpplibs/mcpp-index#482). N2 sets
  `GH_REPO` and also runs the alert when `mirror-cn-reachable` fails.
- **F4. A filtered unit run was reported as the unit suite.** A local run of
  the mcpp unit binaries under a `GTEST_FILTER` naming the new suites printed
  "134 passed", which counts binaries; the full suite had one stale expectation
  (the `place-dlls` command now carries `--crt`), found by the macOS self-host
  row. The unfiltered run passes.
- **F5. The toolset on the Visual Studio runner is newer than the Qt payload's
  runtime.** The measurement recorded MSVC 14.51.36231 (Visual Studio 2026) as
  the toolset whose runtime directory the action `PATH` receives, against the
  14.44 copy `xim:qt-base` carried: the case D1 and D2 describe is the ordinary
  state of a current CI image.
- **F6. The action PATH crossed the invariant that an action's command line
  survives an upgrade.** An action that declares neither `env` nor `cwd` kept
  the positional `__action-stamp` spelling so that its command line, and
  ninja's command hash, stayed the one an earlier engine wrote. D3 gives every
  action of an MSVC-ABI build the named wrapper with `--path-prepend`, so each
  such action re-runs once on the first build after the upgrade. The crossing
  had no test; e2e 780, which recognised a check that ran by the old spelling,
  failed on the Windows row and found it. The detector accepts both spellings,
  the comment in `cli.cppm` states the exception, and the CHANGELOG lists the
  one re-run.
- **F7. §7.4 named the wrong sibling of #729.** It listed the LLVM step of
  `ci-windows.yml` among the builds piped into `tee` without `pipefail`; that
  step runs under `shell: bash`, which GitHub starts with `-eo pipefail`, and
  is not affected. The lint's reading of the workflows before the change is
  seven W1 problems: in `ci-linux.yml` the LLVM, musl-gcc and GCC cold-rebuild
  steps and two example steps, and in `ci-fresh-install.yml` two template
  steps. A list written from reading the files is the claim; the lint's output
  is the reading.
- **F8. The release gate's first reading was of its own harness.** The first
  release run of 2026.9.28.2 (36363585412) failed the GalTranslPP canary before
  it built anything: `release_canaries.py` started `bash` by name, and a Windows
  program that does so gets `System32\bash.exe`, the WSL launcher, because the
  loader searches the system directory before `PATH`. The gate held and no tag
  was created. The workflow now names the step's bash (`CANARY_BASH`), the
  runner has tests, and the canaries were dispatched on the fix's branch before
  the release was dispatched again (#731).
- **F9. A packaging revision reaches a consumer when its index does.** The
  GalTranslPP canary, dispatched on #731's branch after `qt-base` revision 1
  was published, built, ran and packed the project with the candidate, and
  stated once that `xim-x-qt-base\6.11.1\bin` ships the MSVC C++ runtime:
  the resolver placed the toolset's set instead, as WS1 specifies. The payload
  was revision 0 because the job restores the whole mcpp home from CI's cache,
  its index copy included, and that copy predates the revision; by that index
  the installed payload is current. A consumer receives revision 1 on the
  first use after its index refreshes (GalTranslPP's own CI caches the
  payloads and not the index; its reading follows the release).

### 8.3 Readings

**Before and after, per task.** Each criterion of §7 was read against the
released 2026.9.28.1 (xlings 2026.9.28.1) and against this round's versions.

| Task | Criterion | 2026.9.28.1 | 2026.9.28.2 |
|---|---|---|---|
| X1 | the index build script runs once per `update` (E2E-91 S6; sandbox) | 2 runs | 1 run |
| X2 | a store hit reads "is in the store", then "active: a -> b" | "is already installed", "upgraded" | as specified |
| X3 | every `download_progress` event names its stream (E2E-123 P3; sandbox) | no `stream` | named, bounded by the elapsed time |
| X4 | off a terminal, the xim index download prints two lines (E2E-123 P1; sandbox, CN mirror) | 162 lines (769 in a CI log) | 2 lines |
| X5 | the marker; #617's handoff; #624's nested home (E2E-120 S6, E2E-124; sandbox) | no marker; "package file not found" | as specified |
| M1 | e2e 819 L1 to L6; e2e 820 W1 to W5 on the Windows row (VS 2026, MSVC 14.51.36231) | `--crt` unknown | pass |
| M2 | e2e 820 W6; e2e 799 D; the measurement (run 36361809507) | the tools do not start as actions; `--path-prepend` unknown | the tools start as actions on the Visual Studio row and on the masked row (managed msvc@14.44.35207), and do not start directly |
| M3 | e2e 118 on the Windows row | asserted only the missing depfile | the rebuild is asserted and passes |
| M4 | e2e 818 criteria 5 and 6; e2e 820 W7; e2e 821 | 0 statements (818); A1 fails (821) | once; once at `[workspace.build]`; once per run of the edge |
| M5 | the lint on the workflows before and after the change | 7 W1 problems | 0 |
| M6 | the docs check on the Linux, macOS, Windows and bare Windows rows | no `defaultToolchain` | gcc@16.1.0, llvm@20.1.7, llvm@20.1.7 and gcc@16.1.0: 0 problems on each row |
| M7 | unit `ConditionalOrder.*`; the sandbox section #728 | lexical order | specificity |
| M8 | the canaries; `verify-published.sh` | not present | see F8 and the sandbox table below |
| I1, I2 | the static test; the Windows install tests of `qt-base` and `qt` (openxlings/xim-pkgindex#898) | the test fails at `qt` and `qt-base` | both recipes install and pass their checks with revision 1 |
| N2 | `red_members.py selftest` (seven cases) | not present | pass |

**CI.** mcpp#730's last head (`feb5743f`): every workflow concluded success;
the two jobs of the xcode-27 image fail as known red (#669). The two earlier runs
on the branch found F4, F6, the fast path that e2e 821 assumed on every host
(it serves only ELF products, #400), and the measurement's three harness
defects; each was fixed on the branch.

**Sandbox readings.** `tests/release/verify-published.sh` in fresh SubOS
sandboxes (`xlings subos use <n> --sandbox`), with the CN mirror set for xlings
and for mcpp, against what the index publishes:

| mcpp | xlings | ok | failed | not run | The failures |
|---|---|---|---|---|---|
| 2026.9.28.1 | 2026.9.28.1 | 15 | 12 | 2 | every section this round adds, each with the reading of §8.3's second column |
| 2026.9.28.1 | 2026.9.28.2 | 20 | 7 | 2 | the seven mcpp sections: mcpp's registry runs its pinned xlings 2026.9.28.1 and carries no marker (two); no `defaultToolchain`; the lexical order; the three `place-dlls` legs (`--crt` unknown) |
| 2026.9.28.2 | 2026.9.28.2 | 27 | 0 | 2 | none |

The two sections not run are Windows behaviour, read on the Windows CI rows
(e2e 820, e2e 118, the measurement). Every section kept from 2026.9.28.1
(#717, #720, #723, #724, #725, the progress of an index refresh) passes in
all three runs.

**GalTranslPP on Windows.** The real project the review started from, read
three ways with mcpp 2026.9.28.2, each with the LLVM row (llvm@22.1.8) over the
Visual Studio 2026 toolset (MSVC 14.51.36231) on `windows-2025`:

| Reading | Build, run, pack | The runtime beside the program |
|---|---|---|
| the release's canary (the candidate, CI's cached mcpp home) | 4 of 4 commands held; `GalTransl++ CLI v3.1.1` | `qt-base` revision 0 from the cached home: its copy is stated once as a packaging fault, and the toolset's set is placed (F9) |
| the project's CI, pin 2026.9.28.2 (Sunrisepeak/GalTranslPP#3, `0681f59`) | success in 41 minutes | no runtime copy in `qt-base`'s `bin`: nothing is stated; the package carries the set the build placed |
| the same, without the #718 workaround (`229f0d1`, run 36378870254) | success in 35 minutes; no statement that a CRT word is redundant | the model alone chooses the dynamic CRT: the packages carry `MSVCP140*` and `VCRUNTIME140*` beside GPPCLI and GPPGUI |

### 8.4 Self-review

**Architecture.** Each fact that §1 set out to give one authority now has one:
the files beside a PE program (`mcpp.build.runtime_placement`, read by the
plan, `place-dlls` and `mcpp pack`), the order of conditional tables
(`mcpp.manifest.cfg_selector`, read by both manifest readers), the host's
default toolchain (`pins::host_default_toolchain`, read by the first run,
`self env` and the docs check), and a home (`home_identity::is_home`, read by
every xlings reader). Two statements remain second copies by construction:
`verify-published.sh` embeds the PE synthesiser of `tests/e2e/_synth_pe.py`,
because a sandbox sees no checkout, and the index keeps its own list of red
members, because the owner of a failure is a fact of the index.

**Crossings.** The pull-request template's table was filled before CI and
still missed one crossing (F6: the action `PATH` against the stability of an
action's command line). The table asks which invariants a change crosses; a
change to a generated command line crosses every test and comment that reads
that command, and those are found by searching for the old spelling, not by
thinking of invariants.

**Stability.** The release gate works and costs time: the GalTranslPP canary
builds its vcpkg dependencies without a cache on `main`, about fifty minutes,
before any archive is built. Its cache is saved per run, so later releases
from `main` restore it. The measurement workflow renames system files on a
disposable runner and restores them from bash in an `always()` step.

**User experience.** A project that wrote the #718 workaround
(`dialect_cxxflags = ["-fms-runtime-lib=dll"]`) is told once that the word is
redundant; GalTranslPP carries it. Off a terminal, xlings still passes the
sub-index build scripts' frames through (openxlings/xlings#629).

**Compatibility.** On Windows the first build after the upgrade re-runs each
action once and, for GNU-dialect compiles, rebuilds once; a program whose
search directories carry an older runtime copy receives the toolset's. No
index descriptor changes meaning under D7 (§8.3).

**Open after this round.** openxlings/xlings#629 (the frames), mcpp #669 (the
xcode-27 image's SDK), and the reach of a packaging revision, which is the
reach of the consumer's index (F9) and is not a defect.

