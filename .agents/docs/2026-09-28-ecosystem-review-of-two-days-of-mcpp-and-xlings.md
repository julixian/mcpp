---
subject: review
status: active
---

# Two days of mcpp and xlings: a review of what merged, what is known, and what is open

**Status:** active, revision 1 (2026-09-28). Findings only; nothing described
here is implemented. Written for review before the next round is planned.

## 0. Scope and method

**Window.** Pull requests merged from 2026-09-26 to 2026-09-28, and non-upstream
issues open and active in the same window.

| Repository | Merged in the window |
|---|---|
| mcpp | #727 (2026.9.28.1), #719 (2026.9.27.1), #706 (docs), #702 (2026.9.26.2) |
| xlings | #625 (2026.9.28.1), #622 (2026.9.27.1), #616 (2026.9.26.3), #623, #618, #619 (docs) |

| Repository | Open, non-upstream, active in the window |
|---|---|
| mcpp | #718 (awaiting this review), #728, #729, and the trackers #677 and #397; #726 was closed during the review with its Windows reading |
| xlings | #617, #624 |

#721 (GCC internal compiler error) and #669 (ld64.lld cannot read the xcode-27 SDK)
are upstream and are cited only where they shape a signal.

**Evidence.** Each finding below cites code at `origin/main` (mcpp `acb9f52b`,
xlings `d53162a`) or a measured output: CI runs on `main`, two xlings sandboxes
against the published releases, the validation project Windows build on
mcpp 2026.9.28.1, and the output the maintainer reported from a real
terminal. A finding without such evidence is marked as a hypothesis.

**Severity.** P0 is a wrong result or a lost correctness guarantee on a default
path. P1 is a wrong result off the default path, or a default-path defect whose
effect is visible and recoverable. P2 is noise, wording or cost.

## 1. State at the time of writing

- **Releases.** mcpp 2026.9.28.1 and xlings 2026.9.28.1 are published.
  - All eight archives (four per product) are on GitCode, with sizes equal to GitHub's.
  - The linux-x86_64 archive of each is also byte-identical by sha256.
  - xim-pkgindex points `latest` at both (#895, #896), and the index artifact is published.
- **CI on `main`.** mcpp is green except the xcode-27 jobs of `ci-macos`,
  `ci-macos-e2e` and `ci-fresh-install` (#669). xlings is green.
- **Sandbox verification.** In an xlings SubOS sandbox with the CN mirror for both
  tools, the published pair reads `13 ok, 0 failed`. The same script against
  mcpp and xlings 2026.9.27.1 reads `5 ok, 8 failed`.
- **Real-world build.** the validation project (the validation project's pull request) is a 22-port
  vcpkg and Qt workspace with a project `.xlings.json`.
  - On mcpp 2026.9.27.1, its Windows build failed with vcpkg's compiler detection (#726).
  - With the pin moved to 2026.9.28.1, every step passes: `build --workspace`,
    `run -p cli`, `pack --format release`, the CLI starting from the release
    layout, and `emit build-database`. That is run 36346122142.
  - Its output is the source of §2.1 and §2.3.
- **mcpp-index.** A full sweep with the 2026.9.28.1 pin runs on the upstream branch
  `mcpp/2026.9.28.1`. Its baseline on `main` is already red on one member
  (§3.6).

## 2. Defects introduced in the window

### 2.1 P0 · mcpp · The toolset's CRT yields to a CRT copy found in a dependency's directory

**Observed (the validation project, Windows, llvm@22.1.8, MSVC 14.51).** For every program
that links Qt, ten warnings of the form:

```
warning: cxx_runtime: toolchain-coupled would stage '<VS>\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT\vcruntime140.dll'
beside the artifact, but this project already deploys '<registry>\xim-x-qt-base\6.11.1\bin\vcruntime140.dll' there; keeping the project's file
```

**Mechanism.**

- The plan-time scan of runtime search directories (`src/build/plan.cppm`, the loop
  over `linkIntent.runtimeSearchDirs`) adds every DLL it finds to the deploy list.
- `xim:qt-base`'s `bin/` is such a directory, and Qt ships the MSVC CRT in it.
- The toolchain-coupled staging (`src/build/flags.cppm:1584-1600`) then finds these
  names already present. It treats them as the project's declarations, keeps them,
  and warns.

**Why it is wrong.**

- **The message misattributes the file.** The project declared nothing; the
  files were derived from a directory.
- **The precedence is inverted.** `toolchain-coupled` means that the CRT of the
  toolset the program was compiled with travels with it. A CRT that happens to
  sit in a dependency's directory is of whatever toolset built that dependency.
- **An older copy can break the program.** The MSVC redistributable must be at
  least as new as the newest toolset that built any image in the process. The
  build does not compare versions, so the selected copy can be the older one.
- **The design intended a different order.** It is: a declared deploy, then the
  toolchain's runtime, then a derived file. That is SPEC-007 R4.3 read together
  with the "explicit wins" comment at the staging site. The round implemented the
  first yield (derived to declared), not the second (derived to the toolchain's
  runtime).

**Direction.**

- The plan-time scan skips a DLL that the toolchain-coupled staging provides, as
  it already skips a declared name.
- The staging's clash check then fires only for a declared deploy, where its
  message is true.
- Criterion: a Windows fixture (e2e 814) and its cross-compiled Linux form (818)
  whose runtime search directory holds a same-named CRT DLL. The program's
  directory must receive the toolset's copy, and no clash warning may appear.

### 2.2 P1 · xlings · An index download redraws nothing and prints a frame per chunk

**Observed (maintainer's terminal, `xlings update`).** For each index artifact,
dozens of two-line blocks (`↓ xim 96.7%` / `▸ ███ 96.7% …`), one per received
chunk.

**Mechanism.**

- #625 made `cmd_update` report the index artifact's bytes as a `download_progress`
  data event (`src/core/xim/commands.cpp:2726-2752`), with `payload["prevLines"] = 0`
  on every event.
- The CLI renderer moves the cursor up only when `prevLines > 0`
  (`src/cli.cpp:304`, `src/ui/progress.cpp:366`). Every frame is therefore appended.
- The install path keeps this count itself (the renderer returns
  `files + 2`, `commands.cpp:951-971`) and renders at the downloader's cadence. The
  index path does neither.
- Off a terminal, the renderer never rewrites, so the missing throttle alone
  fills a CI log.

**Direction.**

- The index-bytes callback keeps the previous frame's line count per label,
  starting again at 0 when the label changes.
- It throttles to the install path's interval.
- Criterion: a pseudo-terminal e2e for `xlings update` asserts a bounded number of
  frames per index, and the non-terminal form asserts one start and one finish line.

### 2.3 P2 · mcpp · One fact, many warnings

- **The redundant CRT word, once per member.**
  - The validation project writes `dialect_cxxflags = ["-fms-runtime-lib=dll"]` at workspace
    level, and every member inherits it.
  - A `--workspace` build plans each member as a root, so the redundancy warning
    appears once per member: five times.
  - The warning names `[build] dialect_cxxflags`, while the statement is the
    workspace's.
- **The staging clash, once per link.** §2.1 appears ten times per linked
  program: for cli, the updater host tool and gui.
- **Two older warnings follow the same per-member pattern.**
  `lib target without conventional lib root` and `emits no GNU depfile` are
  repeated for each member as well.

**Direction.**

- A warning with the same code and text is printed once per process.
- An inherited statement is named at its source (`[workspace.build]`).
- The envelope keeps every occurrence for machine readers, with its member.

### 2.4 P2 · mcpp · The post-link DLL difference is visible only under `-v`

- `place-dlls` reports a difference between a declared DLL and a search directory's
  copy on its own stderr. mcpp discards an edge's output when the build succeeds
  (`ninja_backend.cppm`, `execute.cppm`: the output is printed only when
  `verbose`).
- #727 added a planning-time warning for the common case. It does not cover a
  directory a `prepare` action fills during the build, nor a declared source that
  an action generates. The post-link edge is the only place that sees those.
- **The same shape recurs.** It is the "success with something to say" channel
  that build programs already have (`tag`) and ninja edges lack. A general
  mechanism: an edge writes advisories to a sidecar beside its stamp, and mcpp
  prints the sidecars of the edges that ran. That would serve both.

## 3. Defects that predate the window, surfaced by it

### 3.1 P0 · mcpp · No header dependency tracking on the default Windows row

- `ninja_backend.cppm:1446-1480` computes `posixDepfile = !msvcDeps && !is_windows`.
- The stated reason is that GCC's depfile needs an awk filter, which Windows lacks.
- **The gate is conflated.** Clang's depfile needs no filter: `needsGnuModuleFilter`
  is true only for GCC, and the comment above measures Clang's plain rule.
- **The consequence.** Every clang++ build on Windows gets no `-MMD`, and a header
  edit does not rebuild the objects and BMIs that include it. Since #718 the LLVM
  row is the default Windows row, so this is the default experience. The validation project
  prints the degradation once per member.
- **Direction.**
  - The depfile is emitted for Clang on every host; only GCC's filtered form is
    host-gated.
  - Criterion: a Windows e2e edits a header included in a module purview and
    asserts a rebuild.
- **Hypothesis to measure.** Ninja's `deps = gcc` parser accepts clang's Windows
  depfile paths (drive colons, backslashes). The escaping clang writes is designed
  for it.

### 3.2 P1 · CI · The only Linux clang self-build never completes, and reports success (#729)

- `ci-linux.yml`'s "toolchain: musl + llvm" job builds mcpp with llvm@20.1.7.
- The build fails at `xlings.m.o`: libc++ 20's `std` module does not expose
  `directory_iterator`'s comparison.
- The step pipes the build into `tee` and greps only the resolution line.
  It has been green on `main` while failing.
- **Consequences.** No CI job builds mcpp with clang on Linux. The #722
  function-size gate, which needs a clang compile database, runs by hand.
- **The docs disagree with what resolves.** `docs/01` and `docs/20` name llvm@20.1.7
  as the macOS and Windows default, while Windows CI and the validation project resolve
  llvm@22.1.8. The documentation should state what the resolver picks. The
  selection code was not traced in this review.

### 3.3 P2 · xlings · `update` rebuilds the catalog twice

- `cmd_update` obtains the catalog through `get_catalog()`, whose first call
  rebuilds it (`commands.cpp:159-161`). Two lines later it forces a second rebuild
  (`commands.cpp:2760`).
- Every index repository's `pkgindex-build.lua` therefore runs twice, and its
  `[i/n]` lines print twice. The maintainer's output shows `awesome`, `d2x` and
  `scode` twice.
- **Direction.** `update` performs only the forced rebuild.

### 3.4 P2 · xlings · "is already installed" followed by "upgraded"

- When the target version's payload already exists in the store, `update <name>`
  prints `xim:mcpp@2026.9.28.1 is already installed`, then
  `upgraded xim:mcpp: 2026.9.27.1 -> 2026.9.28.1`.
- **Both statements are true.** The payload was present, because the sandbox
  verification installed it into the shared `~/.xlings/data`, and the active
  version changed.
- **Read together, they contradict.** The wording should say the payload is in
  the store and is now active.

### 3.5 P1 · xlings · Which home owns a path is answered by structure (#617, #624)

- **#617.** `is_home_root` recognises a home by `.xlings.json`, `bin/xlings` and a
  `subos/` directory. Every SubOS now has an empty `subos/`, so a SubOS reads as a
  home, and the shim handoff and shim dispatch stop one level too early.
- **#624.** A home nested under another home's `subos/<name>/` has its package
  script paths re-rooted by the outer `subos/` segment. `gcc` then installs and
  registers no program.
- **The common root.** Two predicates infer "home" from directory shape, and the
  shape stopped being unique. A home should carry an explicit identity: a marker
  that names the home, written by `self init`. One resolver should read that
  marker, and a nested home should either work or be refused at initialisation,
  with a reason.

### 3.6 P1 · ecosystem · mcpp-index's full sweep has been red on `main` since the last pin

- The two `workflow_dispatch` full sweeps on `main` after #470 (2026.9.26.2) failed.
  - The first failed its `mirror-cn-reachable` job.
  - The second failed one member, `pangocairo` on linux (`FAILED. 1 passed; 1 failed`).
  - No issue records either.
- A red weekly net is a net that nobody reads.
- **Direction.**
  - The failure gets an issue and an owner, or the member is marked.
  - Criterion: the next full sweep either passes, or fails only on members
    listed in an open issue.

### 3.7 P2 · SPEC-004 · Conditional tables merge in selector text order (#728)

- **Behaviour.** Matching `[target.<selector>]` tables merge in the lexical order of
  their selector text. The specification says "manifest order", which a TOML
  reader cannot observe.
- **Status.** SPEC-004 §3.1.1 is marked partially implemented.
- **Needed.** A decision: specify lexical order, or specify a precedence by
  selector specificity.

## 4. Process findings

1. **A trailer the user disabled reached `main`.**
   - `Co-authored-by: Claude` appears in `acb9f52b`. A subagent wrote it into one
     commit message (`a114160a`), and GitHub's squash merge aggregates every
     co-author trailer of the branch. The same happened once before (`cfe46967`).
   - Remedy, now recorded:
     - subagent prompts forbid attribution lines;
     - `git log --grep` checks the branch before merging;
     - squash merges carry an explicit subject and body.
2. **A differential run cannot see a test that is red on both sides.**
   - The round judged regressions by running the fresh and the released binary in
     one environment. e2e 205 and 807 failed there on both, for reasons of the
     environment, and read as unchanged. CI caught both.
   - A both-red test must get its reading from an environment in which it passes.
3. **A step's name is not its assertion.**
   - #729 repeats a shape found before: a CI step that asserts something other
     than what its name says.
   - A lint over the workflows would find the pattern: `| tee` followed by a `grep`
     that is not about success.
4. **A pinned canary needs a person to move its pin.**
   - The validation project pins mcpp in `.xlings.json`. It measured #726 only after its pin
     was moved by hand.
   - A small set of real projects, rebuilt against each release candidate with
     the pin overridden, would turn this into a release gate.
5. **Regressions of 2026.9.27.1 had one shape.**
   - #725: a new gate made a latent gap loud.
   - #723: a new feature (`artifacts` edges) met an older invariant (one source per
     destination).
   - e2e 797 in the round itself: a command line derived from a state that differs
     between two plans.
   - Each is a new rule crossing an old one, with no test at the intersection. The
     release checklist should ask, for each new gate or feature: which existing
     invariant does it cross, and which test sits at the crossing?

## 5. Open issues, with their home and priority

| Issue | Home | Priority | Next step |
|---|---|---|---|
| mcpp #718 | engine | done; reading passes | close with the validation project reading and e2e 814, and file §2.1 as its own issue |
| mcpp #726 | engine | closed | closed during this review, with the validation project reading |
| mcpp #729 | CI | P1 | fail the step on the build's status; build with llvm@22.1.8; wire the size gate after it |
| mcpp #728 | specification | P2 | decide the order rule; then implement or restate |
| mcpp #677, #397 | trackers | P2 | re-verify their open items against `acb9f52b` in the next sweep |
| xlings #617 | xlings | P1 | one home-identity resolver with an explicit marker |
| xlings #624 | xlings | P1 | same resolver; nested home works or is refused at init |

## 6. Proposed next round

**Packaging.** One PR per repository, each with its own patch version.

**xlings 2026.9.28.2:**
- §2.2: the index-progress frame count and throttle.
- §3.3: a single rebuild in `update`.
- §3.4: the wording.
- #617 and #624: the home-identity marker and resolver. These could be a separate
  version if their review needs more time.

**mcpp 2026.9.28.2:**
- §2.1: toolset CRT precedence.
- §3.1: depfiles for Clang on Windows.
- §2.3: warning deduplication and naming of the source.
- #729: the CI step asserts the build; the size gate is wired after it.
- The xlings pin moves to 2026.9.28.2.

**Deferred with a decision.**
- §2.4, a general edge-advisory channel: a design, not a patch.
- #728: a specification decision.
- §3.6: an index issue and owner.
- Process items 3 and 4: a workflow lint and a canary gate.

**Order and verification.** xlings first, then the mcpp pin, then the index. The
same sandbox script, extended by one assertion per item, is run against the new
and the previous versions. The validation project is built on Windows as the real-world
reading for §2.1, §2.3 and §3.1.
