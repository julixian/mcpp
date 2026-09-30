---
subject: design
status: landed
---

# The build's wall time, its progress count, a hang after the build, and #732 and #744: measurements and a remediation plan

- Status: implemented, revision 4, released as 2026.9.30.2. Section 9 splits
  the work into tasks across repositories; section 10 records what was built,
  what was measured, and where the implementation departs from sections 4 and
  5 (W4 is deferred on its own gate). Revisions 1 and 2 were reviewed on
  2026-09-30.
  - Round 1 settled D1 to D5 (section 6), narrowed W6 and W7, and asked
    whether #732 is a usage problem. Revision 2 applied those answers and a
    self-review, which changed W2, W4, W8 and W10.
  - Round 2 asked for the root cause of #732 and for a fundamental solution
    that is elegant, stable and compatible. Revision 3 answers with
    measurements on GCC 16 and clang 22 (F7), and replaces revision 2's
    rule with W10.
- Date: 2026-09-30
- Origin: five points reported on 2026.9.30.1 while building xlings.
  1. Once, `mcpp build` did not exit after the animated status row stopped.
  2. A build with the animated row appears a few seconds slower than
     2026.9.28.2. The report asks whether the old timer was inaccurate or
     whether the time before the build can be reduced.
  3. The status row's count includes the packages served from the cache.
     The `Cached` lines are correct; the count should state only the work the
     build performs.
  4. mcpp#744 and mcpp#732.
  5. The build is slow in general.
- Task: separate what is not mcpp's, and plan what is mcpp's or what mcpp's
  own design requires, as one pull request.

## 0. Summary

| # | Report | Finding | Whose | Item |
|---|---|---|---|---|
| 1 | Hang after the animation | `Stack::update` does not terminate; the ticker holds the line lock, and `close_region()` joins it forever (F1) | mcpp, new in 2026.9.30.1 | W1 |
| 2 | Slower than 2026.9.28.2 | Wall time is equal within the run-to-run spread; 2026.9.28.2's `Finished` omitted about 3.9 s of work before ninja (F2) | no regression | none; the 4 s is addressed by W3, W4, W8 |
| 3 | Count includes cached packages | 1195 counted steps: 503 cache placements, 460 dependency scans, 232 compiles and links (F3) | mcpp design (revision 3, §7.2) | W2 |
| 4a | #744 | Confirmed; the probe also costs 0.35 s on every planned command (F6) | mcpp | W3 |
| 4b | #732 | The language and the ABI make a module name unique per program. mcpp makes it unique per build configuration, which holds several programs. GCC and clang can bind imports per importer, measured (F7) | within one program: a usage error, still refused. Across programs: mcpp's identity model | W10 |
| 5 | Slow in general | Clean build: 30 of 35 s is the project's import chain (F4). Edit of one file: 3.05 s of planning precedes the one compile, and the planner walks each dependency's tree about 380 times (F5) | chain: project and compiler; planning and probes: mcpp | W4, W8, W9 |

**One pull request carries W1, W2, W3, W8, W9 and W10** (section 5). W4 was
deferred on its own gate after W8 was measured (section 10.3).

| Item | Status |
|---|---|
| W5, the split schedule | stays opt-in |
| W6, a faster linker | recorded and not done (D3) |
| W7 | dropped (D4) |
| W11 | its Windows reading is taken from this pull request's run; the change waits for that reading |

## 1. Method

- **Machine.** 32 cores, Linux 6.8.0. `perf` is unavailable to the user
  (`perf_event_paranoid = 4`) and `ptrace_scope = 1`, so the in-process time
  is resolved to phases by log timestamps and system-call traces, not to
  functions.
- **Project.** xlings at c4b6cef: a workspace of eight members, thirteen
  index dependencies served from the global cache, 230 translation units of
  its own. The tree was copied twice into a scratch directory (without
  `tests/` and `target/`), one copy per mcpp version, so that each version's
  lock file and build directory stay its own.
- **Versions.** The released 2026.9.28.2 and 2026.9.30.1 binaries from the
  xlings store, run by path, with the same home (`~/.mcpp`), gcc 16.1.0,
  ninja 1.12.1 and binutils 2.42.
- **Runs.** Clean builds (`mcpp clean && mcpp build`), one warm-up per
  version, then three rounds alternating the versions under a pty (the
  animated row on) and three through a pipe. Wall time is measured outside
  mcpp.
- **Instruments.**
  - `.ninja_log`, split into passes and deduplicated per step;
  - the dyndep files (`*.dd`), which give the module edges, for the critical
    path;
  - `strace -f --seccomp-bpf -e trace=execve` for process launches, and
    `-y -e trace=openat` for the paths the planner opens;
  - `MCPP_LOG_LEVEL=info` timestamps for the planning phases;
  - a harness that compiles the `Stack` class verbatim and drives it;
  - the final link command replayed with `ld.bfd` and with `ld.lld`;
  - `MCPP_BMI_SCHEDULE=on` against the default.

## 2. Findings

### F1. The hang: `Stack::update` does not terminate

- **The loop.** `src/ui/dots_screen/stack.cppm:22`:
  `while (cells_.size() + 4 * flying_.size() + 16 <= target) lock(spawn());`
  terminates only if every `lock(spawn())` adds cells.
  - `landing()` (`stack.cppm:65-72`) starts at `x = kWidth` and tests only
    `fits(x - 1)`.
  - When column 47 is occupied in the rows a piece spans, the piece lands at
    `x = 48`, outside the 48-column screen, on cells that an earlier such
    piece already holds.
  - `cells_` is a `std::map`, so its size stops growing. `target` does not
    fall, and the loop never ends.
  - The screen holds 192 dots and the target at the end of a build is 168
    (`(kWidth - 6) * kHeight`). A stack whose holes leave it at 152 cells or
    fewer when it reaches the right edge therefore keeps the condition true
    for ever.
- **Measured.** The class compiled verbatim in a harness, with 2000 seeded
  runs of a 240-step build that finishes in bursts at ten frames per second:
  66 runs (3.3%) never return from `update()`. They stall at 146 to 152 cells
  against a target of 162 to 168.
- **Why the process hangs.**
  - The ticker thread takes `line_mutex()` (`src/ui.cppm:599`) and draws:
    `redraw_locked` → `current_rows_locked` → the model's `frame()` →
    `r.animation->update(in)` (`src/build/progress.cppm:1080`).
  - After ninja, `close_region()` (`src/ui.cppm:1029`) requests a stop and
    joins the ticker. The ticker never returns to its stop check, so the join
    blocks and `Finished` is never written.
  - No frame completes after the loop starts, so the row stops moving: this
    is what the report describes as the animation having ended.
- **How often.** `stack` is one of four animations chosen at random when
  `MCPP_PROGRESS` is unset (`dots_screen.cppm:42`, `progress.cppm:1120-1134`).
  The hang needs that one-in-four choice and a stack shape that reaches the
  right edge before the end: about 1% of interactive builds, and more of the
  long ones.
- **Ruled out, with evidence.**
  - The stream reader: it reads non-blockingly and drains once after
    `waitpid` (`modules/platform/src/unix/bounded_process.cppm:311-345`), so a
    grandchild holding the pipe cannot block mcpp.
  - Lock order: no holder of the model's lock calls into `mcpp.ui`; lines
    are written after the lock is released.
  - The keyboard reader of `--play-game`: it is non-blocking and runs inside
    `poll` on the ticker thread.
  - The post-build steps: only the freestanding size report and hooks run
    after the region closes.
  - The other animations and the three games: every loop in them is bounded
    (checked: `snake`, `ions`, `chomp`, `stack_game`, `snake_game`,
    `runner_game`).

### F2. "Slower than 2026.9.28.2" is the timer

| Version | Wall, mean (runs) | `Finished`, mean | ninja main pass, mean | Outside ninja |
|---|---|---|---|---|
| 2026.9.28.2 | 35.88 s (6: 34.11 to 37.82) | 31.98 s | 31.89 s | 3.99 s |
| 2026.9.30.1 | 35.39 s (5: 35.02 to 35.91) | 35.37 s | 31.20 s | 4.19 s |

- **2026.9.28.2 under-reports by 3.90 s.** Its clock starts near ninja and
  excludes resolution, the build programs and planning. Since 2026.9.29.5,
  `Finished` counts from the start of the command and equals the wall time
  within 0.02 s.
- **The wall times are equal within the spread.** The pty and pipe runs do
  not differ measurably, so the animated row has no measurable cost.
- **One outlier.** One 2026.9.30.1 run took 43.06 s. Its ninja pass alone
  took 38.65 s against about 31 s in the others, which is compile-time
  variance on a shared machine. It is excluded from the mean.
- **No change to the timer is proposed.** The four seconds outside ninja are
  real and are addressed by W3, W4 and W8.

### F3. The count: 1195 steps, of which 232 compile or link

The status row's `f/t` in a clean build of xlings (2026.9.30.1):

| Steps | Count | Time |
|---|---|---|
| Cache placements (`stage_file`, the cache pass) | 503 | 80 ms of ninja time |
| Dependency scans (`.ddi`) | 230 | all 460 scans and collations end 0.21 to 0.24 s into the main pass |
| Collations (`.dd`) | 230 | (included above) |
| Compiles | 231 | about 30 s of the main pass |
| Link | 1 | 1.5 to 2.1 s |

- **The recorded row.** It read `Building … 967/1195 · 0:04` when the first
  compiles began: 81% of the count was reached at the start of the work. It
  then took 25 s to go from 81% to 100%. The animations take their fraction
  from the same numbers (`progress.cppm:1066-1076`).
- **Cause, first half: the design.** Revision 3 states "The cache pass
  counts in `Building f/t` like the main pass"
  (`2026-09-30-build-output-refinement-design.md:1152`), and
  `Build::pass_begin` carries every pass's counts forward
  (`progress.cppm:1397-1404`).
- **Cause, second half: ninja's `%t`.** It counts the scans and collations,
  which are as numerous as the compiles and finish in the first quarter
  second. Excluding the cache pass alone would still leave 460 of the
  remaining 692 counted steps as scans.

### F4. A clean build: 30 of 35 s is the project's import chain

- **The critical path.** The module edges from the dyndep files and the
  measured step durations (2026.9.28.2, final run) give a chain of 18
  compiles summing to 30.13 s, in a main pass of 32.48 s.
  - It starts at `modules/json` (4.64 s), runs through `core/config`, `xim`,
    `xself` and `subos` to `cli.m`, and ends in `cli.o` (6.01 s).
  - The link (1.5 to 2.1 s) follows.
  - Busy cores are between 14 and 32 for the first 12 s, 10 or fewer after
    17 s, and 1 for the last 5 s.
- **What bounds the build.** ninja's scheduling and `-j` are not the bound.
  The bound is the depth of the import chain and GCC's time per module
  interface, which belong to the project and the compiler.
- **Levers inside mcpp's design, measured.**
  - *The split schedule.* `MCPP_BMI_SCHEDULE=on` releases importers when GCC
    publishes the BMI, at about 22% of the compile
    (`src/build/schedule/policy.cppm:215-218`). On xlings it measured
    34.04 s against 35.55 s (2 runs each; main pass 29.94 s against
    31.42 s), a gain of 1.5 s, or 4%.
  - *The linker.* The final link replayed from `build.ninja` takes 1.49 to
    1.59 s with binutils `ld.bfd` and 0.19 to 0.20 s with `ld.lld` from the
    llvm payload, three runs each. Both binaries run.
- **Outside ninja (about 4 s, both versions).**

  | Cost | Time | Note |
  |---|---|---|
  | `xlings --version` | 0.35 s | F6 |
  | Recompiling a dependency's build program after `mcpp clean` | 0.79 s | its artifacts live in the consuming project's `target/.build-mcpp`, `build_program.cppm:586-595` |
  | Planning | about 2.7 s | F5 |

### F5. An edit: 3.05 s of planning precede the one compile

A `touch` of `src/core/xself/doctor.cpp` and `mcpp build` (2026.9.30.1,
10.47 s; 2026.9.28.2 took 11.57 s), from the exec trace and the phase log:

| From (s) | To (s) | Span | What |
|---|---|---|---|
| 0.00 | 0.02 | 0.02 s | start |
| 0.02 | 0.37 | 0.35 s | `xlings --version` (F6) |
| 0.37 | 0.38 | 0.01 s | three compiler probes |
| 0.38 | 1.04 | 0.66 s | graph load up to the build program's cache hit |
| 1.04 | 1.69 | 0.65 s | features, host tools, target side |
| 1.69 | 2.60 | 0.91 s | module scan of every package that compiles here |
| 2.60 | 3.05 | 0.45 s | plan, emit, records |
| 3.05 | 8.60 | 5.55 s | compile `doctor.cpp` |
| 8.60 | about 10.4 | about 1.8 s | link (`ld.bfd`) |

- **Why every edit plans.** The fast path declines when any source is newer
  than `build.ninja` (`src/build/execute.cppm:1654-1656`, the rule of
  mcpp#225). A body edit that changes no module declaration, no import and
  no file set cannot change `build.ninja`; planning it again is waste.
  Revision 3 recorded this in §12 and deferred it to a design of its own.
- **A second reason, found in the self-review.** Even with fresh sources,
  the fast path declines whenever ninja relinks an artifact ("ninja relinked
  an artifact, whose closure the full path validates"). The post-link runtime
  validation reads four facts from the plan:
  - the runtime binding;
  - the target triple;
  - the library search directories;
  - whether host libraries are allowed
    (`src/build/runtime_validation.cppm:640-776`).

  The fast-path record carries only the runtime binding. A body edit always
  relinks, so a fast path for edits must carry the rest.
- **Where the planning goes: the planner walks the dependencies' trees
  hundreds of times.**
  - Between the start and the first ninja, mcpp opens 30,967 paths. 30,372
    of them are directories inside installed index packages, and none is a
    file read:

    | Tree | Directory opens | Directories | Walks |
    |---|---|---|---|
    | compat.libarchive 3.8.7 | 13,406 | 35 | about 383 |
    | compat.xz 5.8.3 | 11,551 | 50 | about 231 |
    | compat.zlib, zstd, lua, mbedtls, ftxui and others | 5,415 | | |

  - The cause is in `expand_glob_one` (`src/modgraph/scanner.cppm:563`). It
    walks recursively from a pattern's literal prefix and calls
    `fs::canonical` on every directory.
  - The libarchive descriptor lists 127 sources of the form
    `*/libarchive/archive_acl.c`, whose literal prefix is empty, so each
    pattern walks the whole tree. The expansion runs about three times per
    plan. This is inferred from 35 × 127 × 3 = 13,335 against 13,406
    measured.
  - The same walk code timed in isolation costs 0.39 s for libarchive's 381
    walks and 0.13 s for xz's 222, with a warm page cache. That is before the
    per-file glob match, which the harness reduces to a suffix comparison.
  - The walks run across the whole planning window, at 2,000 to 4,900
    directory opens per quarter second.
- **Scanning.** The module scan reads the sources of every package that
  compiles here, including the thirteen packages whose units the cache then
  serves. The cache decision is made after the scan
  (`src/build/prepare/plan.cpp:2080`).

### F6. #744, and the probe costs 0.35 s per planned command

- **Confirmed as reported.**
  - `candidate_source_version` takes the first source that exists, not the
    newest (`src/fallback/xlings_binary.cppm:262`).
  - `load_or_init` is not memoised, and it runs the check on every load
    (`src/config.cppm:816`).
- **The probe's cost.** `vendored_xlings_version` spawns `xlings --version`
  (`xlings_binary.cppm:196`) on every configuration load, and that spawn
  takes 0.35 to 0.49 s on this machine.
  - In `mcpp build` it runs once, and it is the first 0.35 s of every
    command that does not take the fast path.
  - The fast path does not load the configuration: a no-op build takes
    0.05 s.
- **Whose.** That `xlings --version` needs 0.35 s is xlings's matter
  (section 3). That mcpp asks the question on every command is mcpp's.

### F7. #732: a module name identifies a module within a program, and mcpp made it an identity of the whole build configuration

**What the language and the ABI require (measured).**

- The ABI makes the program the boundary. GCC and clang mangle a
  module-attached entity with its module's name, and a module has one
  initializer, named after it. The objects of both test modules define
  `_ZW6common5valuev` (`value@common()`).
- Linking two different `common` modules into one program therefore fails:
  `multiple definition of value@common()` and of
  `initializer for module common` (GCC 16.1.0, binutils 2.42).
- Two programs do not share symbols, so each may have its own `common`. A
  module name is unique within one program, by the standard and by the ABI,
  and not beyond it.

**What mcpp does: the name is an identity of the whole configuration, at
four levels.**

| Level | What mcpp does | Where |
|---|---|---|
| Resolution | Four places each keep a map from module name to one provider: the scanner's refuses a second provider, the plan's keeps the last one, and the backend's keeps the first, so the two would disagree | `scanner.cppm:1427-1436` (read by `pack/interface.cppm:123-158`); `prepare/scan.cpp:882-924`; `plan.cppm:2110-2113`; `ninja_backend.cppm:2237-2250` |
| Location | A BMI's path is `<bmiDir>/<name><ext>`. The staging of cached BMIs uses the same form | `bmi_path`, `ninja_backend.cppm:2324`; `configure.cppm:72` |
| Compiler lookup | Every compiler finds a BMI by name in one directory: GCC by its default mapper, which also chooses where the BMI is written (`gcm.cache/<name>.gcm`); clang by `-fprebuilt-module-path`; MSVC by `/ifcSearchDir` | `modules/toolchain-model/src/model.cppm:641-693`; `flags.cppm:1176-1222` |
| Collation | `mcpp dyndep --bmi-dir --bmi-ext` derives an import's BMI path from its name | `ninja_backend.cppm:1487-1491` |

A build configuration holds several programs: a package and the programs it
ships through `artifacts`, a workspace's members, and test binaries.
mcpp's boundary is therefore wider than the one the language and the ABI
draw.

**What the compilers can do instead (measured on the same two modules and a
third module `util`).**

- **GCC 16.1.0.**
  - With `-fmodule-mapper=<file>` (lines of the form `name path`), the
    provider writes its BMI to the mapped path and the importer reads it
    from there.
  - Two `common` modules lived in one build directory
    (`gcm.cache/a/common.gcm`, `gcm.cache/b/common.gcm`), and the two
    programs returned 41 and 42, as intended.
  - A mapper file has no fallback. A module it does not list fails with
    `unknown compiled module interface: no such module`, so a map must list
    every module the unit can import.
- **clang 22.1.8.**
  - The provider takes `-fmodule-output=<path>`, as mcpp already passes it.
  - `-fmodule-file=common=<path>` overrides `-fprebuilt-module-path` for that
    one name, while other names are still found in the directory.
  - The programs returned 41 and 42.
- **MSVC.** `/ifcOutput` and `/reference name=path` are the documented
  equivalents. They were not measured on this machine.

**The cases of #732, read against this.**

- **Two providers inside one program.** Invalid, by the ABI, and mcpp
  rightly refuses it. This is a usage error.
- **Case B, the minimal example: two different `common` modules in two
  independent programs.** Valid C++, and not a usage problem. mcpp refuses
  it only because of its configuration-wide identity. The same holds for two
  workspace members that do not share a program; the workspace design
  records that refusal (`2026-09-29-workspace-build-graph-design.md:532`).
- **Case A, the reported project.** `gpp.core` (in the GUI program) and
  `gpp.updater` (in the updater program) both compile
  `3rdParty/3rdModule/boost.ixx`. Each program still contains one `boost`,
  so the build is valid. The layout compiles one file twice where a package
  that both depend on would compile it once, but it violates no rule.
- **A second defect in case A.** The scanner's "one file is reached as two
  packages" hint compares paths lexically (`first.path == u.path`,
  `scanner.cppm:1440`). `GalTranslPP/../3rdParty/…` and
  `Updater/../3rdParty/…` differ lexically, so the hint was not printed.

**Root cause.** The language keys a module by (program, name). mcpp keys it
by (configuration, name) and derives its BMI path, its lookup and its
resolution from the name alone. What identifies a BMI is its provider, the
package whose unit declares the module. What decides which provider an
import means is the importer's dependency closure.

## 3. Excluded: not mcpp's

- **E1. The depth of xlings's import chain and GCC's time per module
  interface.** These account for 30.1 of 35.4 s. Observations for xlings
  (not mcpp work):
  - `modules/json` wraps a large header, is imported by almost everything,
    and costs 4.6 s at the root of the chain;
  - `cli.o` (6.0 s), `core/config.o` (8.6 s) and `xself/doctor.o` (7.8 s)
    are the longest single compiles.
- **E2. `xlings --version` takes 0.35 s.** A version query should not load
  more than the binary. This belongs to xlings, as its own issue; W3 removes
  mcpp's dependence on it.
- **E3. The 43 s run.** It is variance in the compile steps themselves.
- **E4. 2026.9.28.2's `Finished`.** It is already superseded (F2).

## 4. The plan

Each item states the change, its criterion, and the expected gain on the
measured xlings builds. Items W1 to W4, W8, W9 and W10 form the pull request
of section 5.

**W1. Every animation and game terminates in every frame (F1).**

- **The change.**
  - `landing()` tests the landing column itself.
  - `spawn()` returns `std::nullopt` when no rotation and offset lands
    inside the screen.
  - Both fill loops in `update()` stop when `spawn()` returns nothing or a
    locked piece adds no cell. Every iteration then either grows `cells_` or
    ends the loop, so termination follows from the structure of the loop,
    not from the shape of the stack.
  - When the stack can hold no more pieces, it stays full until the build
    ends. The display is decorative, and the counts beside it carry the
    facts.
- **Criterion.** A property test over every name in `names()` and
  `game_names()`, 2000 seeds each (the count at which the harness found 66
  hangs), with inputs that ramp to 1 in bursts and a failure input. Each
  seed runs under a watchdog, and a timeout fails the test process. The test fails on 2026.9.30.1: the
  harness reproduced 66 hangs in 2000 runs. This follows the repository's
  rule that an invariant is stated as a property test, not as an example.
- **Alternative rejected.** Drawing the frame outside `line_mutex()`, or
  joining the ticker with a timeout, only moves the hang: the ticker is a
  static `jthread` whose destructor joins again at exit, and a spinning
  thread still occupies a core.

**W2. The count states the work the build performs (F3).**

- **The cache pass is reported but not counted.**
  - Its `Cached` lines are unchanged.
  - The row keeps its phase, `Planning`, during the cache pass, which took
    80 ms in the measured build (D1).
- **Scans that wait on no action run in a pass of their own, before the
  main pass.**
  - The pass builds a goal `_mcpp_scanned`, made of the `.dd` files of every
    unit whose package has no action preceding compilation. It is emitted as
    `_mcpp_staged_cache` is.
  - The row shows the pass as `Scanning f/t` (D1).
  - The restriction is required. A scan waits on its package's `prepare`
    and `check` actions (`order_only_for`, `ninja_backend.cppm:2505-2511`).
    A pass over every scan would hold every compile of every package behind
    the longest such action: 12 min 49 s of CMake in the validation project.
    That was the defect of revision 1's version of this item (section 7).
  - Scans that wait on an action stay in the main pass, where they are
    counted. There are none in xlings.
- **The main pass counts work.**
  - After the scan pass, the main pass's `%t` counts compiles, links,
    archives and actions, plus those remaining scans and the few runtime
    placements beside a program.
  - The measured clean build reads `Building 0/232` at its first compile. A
    body edit reads `Building 0/2`.
  - The animations take their fraction from the counted passes only.
  - Every dyndep file of a pre-scanned unit is current when the main pass
    starts, so ninja loads them at start-up. This is the order the cache
    pass already relies on (ninja-build/ninja#2662). The growth of `%t` that
    the #742 design measured (12 to 13 when `std.pcm` appeared) should
    disappear, and the criterion checks it.
- **Both paths run the same passes.** The fast path runs ninja through
  `run_ninja_reporting` too (`execute.cppm:1228-1300`). The passes live in
  one function that both paths call.
  - The fast path does not run the cache pass: it replays only a graph whose
    staged files are current.
  - A build with explicit goals (`mcpp test`, a named target) scans only the
    `.dd` files of the units its goals compile, derived from the link units
    the goals name when the goal phony is written.
- **Cost, stated.**
  - A clean build: at most about 0.25 s. All scans and collations currently
    end 0.21 to 0.24 s into the main pass; the root of the critical chain
    already waits for its own scan; one more ninja load takes about 15 ms
    (measured: the no-op cache pass took 15 ms).
  - A no-op build: one more ninja load, from 0.05 s to about 0.07 s.
  - A project of thousands of units on a small machine: compiles wait for
    the last quick scan instead of overlapping it. The delay is bounded by
    the scan pass's duration, which the pull request's timers (W9) state.
- **Alternative rejected: subtracting scans from `%t` with a dry run of the
  scan goal.** It keeps one pass, but it is not exact. The collation after
  an unchanged scan is pruned by `restat`, which lowers `%t` in a way mcpp
  cannot attribute to a scan or to a compile.
- **Criterion.**
  - An e2e test with a cache-served dependency. The first `Building` line
    reads `0/N`, where N is the number of compile, link, archive and action
    steps of the packages the cache does not serve, counted from
    `steps.tsv`. The last reads `N/N`, and `%t` does not change in between.
    The `Cached` lines are unchanged.
  - An e2e test with a `prepare` action. Scans of the other packages do not
    wait for it, and its package's compiles still start after it.
  - The measured clean build of xlings grows by no more than 0.3 s.
- **Compatibility.** The e2e tests that read revision 3's counts (842, 843
  and those listed in #742 and #743) change. Revision 3's §7.2 statement
  on the cache pass is superseded.

**W3. #744, and no process spawned to learn a version already known (F6).**

- **As the issue proposes.**
  - One function selects the source: `MCPP_VENDORED_XLINGS` when set,
    otherwise the newer of the released copy and the `PATH` copy, with the
    released copy on a tie.
  - `Updating` and `Note` are each stated at most once per process.
  - Only a strictly newer source replaces the vendored binary.
- **The version memo.** The version of a binary is memoised per process. It
  is also stored under the home, keyed by path, size and modification time,
  and by inode where the platform provides one.
  - An update of xlings writes a new file, which invalidates the entry.
  - A stale entry can at worst delay an update until the file changes,
    because a replacement still requires a newer candidate.
- **Criterion.**
  - The issue's four e2e cases.
  - An exec trace of a second planned `mcpp build` contains no
    `xlings --version`.
- **Gain.** 0.35 s on every planned command.

**W4. A plan is reused when an edit cannot change it (F5; D2 settled).**

- **The principle.** `build.ninja` is a function of the manifests, the lock
  file, the toolchain, the overrides, the set of source files, and each
  unit's module interface: its module declaration, its partition, its
  imports and its header units. A body edit changes none of these. A header
  cannot supply a module declaration, and an `import` in an included header
  is already invisible to the planner's scanner today, so a header edit
  cannot change the plan either.
- **The change.**
  - The fast-path record stores, per scanned unit of the project's own
    packages, the interface the scanner extracted (D2), and the file set of
    each glob.
  - When a source is newer than `build.ninja`, the fast path rescans only
    the newer files, re-expands the project's globs, and compares. If the
    signatures and the file sets are equal, it runs the passes of W2 on the
    recorded `build.ninja`.
  - The checks the fast path already makes stay: the inputs of the build
    programs, the resources, the `path` dependencies, the runtime manifest
    and the request tag.
- **The post-link checks run from a record, on both paths.**
  - Every check the full path runs after ninja on a relinked artifact moves
    into one function: the runtime closure validation, and the check of the
    surface the artifact walk cannot reach.
  - That function takes a record, not the plan. The record holds the
    runtime binding, the target triple, the library search directories and
    whether host libraries are allowed.
  - Both paths call it. Without it, every body edit would relink and the
    fast path would decline (F5), so W4 would save nothing.
- **Scope.** The same decision serves `mcpp run`'s fast path and a
  workspace's per-group records. A project with active `[hooks]` keeps
  declining the fast path, as it does today.
- **Compatibility.**
  - A record written before this pull request lacks the new fields and
    declines once, the existing pattern for new record fields
    (`depSourceRootsRecorded`).
  - The new fields form an optional block that an older mcpp ignores.
- **Risk.** A missed input skips a plan silently. The matrix below is the
  guard, and it is run with the comparison disabled to prove that it
  detects the loss.
- **Criterion.** An e2e matrix.
  - A body edit takes the fast path, and an exec trace shows no compiler
    probe and no `xlings`.
  - Each of the following takes the full path and builds correctly:
    - adding an import;
    - removing an import;
    - renaming a module;
    - adding a partition;
    - turning a module unit into a non-module unit;
    - adding a file;
    - removing a file;
    - editing `build.mcpp` or a declared input;
    - editing `mcpp.toml`.
  - A relinked artifact is validated on the fast path. A fixture whose
    runtime closure is broken fails on the fast path as it does on the full
    path.
  - A revert probe: with the signature comparison disabled, the matrix
    fails.
- **Gain.** The measured edit falls from 10.47 s to about 7.4 s: the compile
  (5.5 s) and the `ld.bfd` link (1.8 s) remain.

**W8. One walk per tree per plan (F5).**

- **The change.**
  - `expand_glob` takes a package's pattern list and groups the patterns by
    literal prefix.
  - It walks each distinct start once, and matches every entry against the
    group's patterns.
  - The expansions are memoised for the process by root and pattern list,
    so the planner's three expansions of one package share one walk.
- **Scope.** Memoising across processes is not part of this pull request.
  - An installed index tree is not strictly immutable at its version:
    payload revisions (2026.9.27.1) reinstall a version in place.
  - A cross-process key would need a tree identity that mcpp does not
    record today.
  - Skipping the scan of cache-served packages depends on such a key, and is
    left to a later measurement with W9.
- **Criterion.**
  - The directory opens in the planning window of the measured edit fall
    from 30,372 to below 1,000.
  - The file lists are unchanged, checked by the glob unit tests and by a
    byte comparison of `build.ninja` before and after on xlings and on the
    e2e fixtures.
- **Gain.** At least 0.5 s per planned build (measured in isolation); the
  full figure comes from W9.

**W9. Planning states its phases (observability).**

- Each phase of `prepare_build` (`src/build/prepare/driver.cpp:61-73`) logs
  its duration under `build/stage`, as `ninja_backend`'s `stage()` already
  does for its own steps.
- W2's scan pass is logged the same way.
- Behaviour does not change. W8's and W2's criteria read these lines.

**W10. #732 at its cause: a module is identified by its provider, and an
import is resolved in the importer's closure (F7).**

- **The rule, in one sentence.** An import is resolved within the
  dependency closure of the importing package, and each package's closure
  provides a module name at most once.
  - *The closure* is the package and every package it reaches through
    code and workspace-member edges. For the package's test units it
    also includes the dev edges. It excludes `artifacts` and `tools`
    edges, whose programs are separate, and build dependencies, which
    build in a sub-build of their own.
  - *Uniqueness per closure* is the ABI's program rule stated at the package
    level: every program's objects are the closure of its root package.
  - Closures nest: a dependency's closure is contained in its consumer's. A
    name that is unique in a consumer's closure therefore resolves to the
    same provider for the consumer and for all of its dependencies, so no
    BMI is ever read against a different module than the one it was built
    against.
- **One resolver.** `mcpp.modgraph` gains the only answer to "which
  provider": `providers(name)`, `resolve(importerPackage, name)` and
  `bmi_path(unit)`. The four maps of F7 become calls to it. The plan's
  last-wins map and the backend's first-wins map disappear, and with them
  the chance that the two disagree.
  - When the configuration has one provider of a name, `resolve` returns
    it, whether or not it lies in the importer's closure. That is today's
    behaviour, kept so that no existing import is newly refused.
  - Otherwise `resolve` returns the one provider in the importer's closure.
    It refuses when there is none, naming the providers and the closure.
  - The scanner's check becomes per closure. The message states the
    consequence the ABI gives it: the program would define `value@common()`
    twice. The "one file reached as two packages" hint compares file
    identity (`std::filesystem::equivalent`) instead of spelling.
- **Location: disambiguate only on collision.**
  - `bmi_path(unit)` stays `<bmiDir>/<name><ext>` when the configuration has
    one provider of the name.
  - It becomes `<bmiDir>/<provider package>/<name><ext>` when it has more
    than one.
  - This is the rule mcpp#233 already applies to object paths: flat, unless
    two files would collide.
  - The staging of a cached BMI (`configure.cppm:72`) uses the same
    function, and the global cache's entries do not change.
- **Binding the compilers, only in affected packages.** An affected package
  is one whose closure contains a name the configuration provides more than
  once.
  - **GCC.** One mapper file per affected package (`modmap/<package>.map`,
    GCC's `name path` format), used by all of that package's units through
    `-fmodule-mapper=`.
    - It lists every named module of the closure, `std` and `std.compat`
      included, because GCC has no fallback for an unlisted name (F7). The
      list is derived from the resolver over the closure, not from what
      the units happen to import, so it is complete by construction.
    - Header units are refused by the scanner (`scanner.cppm:1046`), so
      named modules are all a map must hold.
    - Providers write their BMI through the same file. The split schedule's
      `bmi-compile --bmi` reads the same `bmi_path`.
  - **clang.** An affected unit gets `-fmodule-file=<name>=<path>` for each
    collided name of its closure. Other names are still found through
    `-fprebuilt-module-path` (F7). Providers already take
    `-fmodule-output=`.
  - **MSVC.** `/reference <name>=<path>` for the same names. Providers
    already take `/ifcOutput`.
  - **Collation.** `mcpp dyndep` gains `--module-map <file>`: a mapped name
    resolves to its path, and any other name to `--bmi-dir` and `--bmi-ext`,
    as today. The GCC-format map file serves it on every compiler.
  - The map files are written only when their content changes, and they are
    inputs of the edges that read them, as `placements.list` is (#734 E4).
- **What does not change.**
  - When no name has two providers, no map is written, no flag is added and
    no path moves. `build.ninja`, `compile_commands.json`, the BMI cache and
    the fast-path record are byte-identical.
  - Every existing project that builds today is in that case, and so are
    all 172 packages of the index.
- **What the change allows.**
  - Case B: an `artifacts` program, or a workspace member, with its own
    `common`.
  - Case A as it stands: each program compiles `boost.ixx` in its own
    package. A note states that one file is compiled as two packages, and
    that a package both depend on would compile it once.
- **What stays refused.** Two providers of one name in one closure,
  including one file reached twice within one closure: the ABI cannot link
  that program.
- **Known limit.** clangd keys the providers of a compilation database by
  module name. In a project that uses a collided name, the editor may
  therefore resolve an importer to the other program's module; the build
  stays correct. The limitation is stated in the docs, next to the rule.
- **Criterion.** The probes of F7 as e2e fixtures, on each compiler family
  of the CI matrix (GCC, clang, and MSVC on Windows, where `/reference`
  precedence is measured for the first time):
  - an app and its `artifacts` updater, each with a different `common`;
  - two independent workspace members, each with a different `common`;
  - case A with the `..` spellings.

  Each program returns its own module's value. The criterion also covers:
  - the refusal of two providers in one closure, and of one file reached
    twice in one closure;
  - an affected GCC unit that imports `std`, a unique module and the
    collided module, which shows that the map is complete;
  - an edit of one `common`, which rebuilds only its own closure's
    importers;
  - a byte comparison of `build.ninja` and `compile_commands.json` before
    and after the change, for every e2e fixture without a collision and for
    xlings.
- **Alternatives rejected.**
  - *Revision 2's rule* (refuse a name twice per configuration). It leaves
    the cause in place and refuses valid programs.
  - *A sub-build per program.* It compiles shared dependencies twice, which
    contradicts #711's "nothing built twice" and the workspace's one graph
    per configuration.
  - *Explicit maps for every unit*, as CMake writes them. The approach is
    uniform, but it changes every `build.ninja`, every database entry and
    every BMI path, with no gain for the projects that have no collision.

### Recorded and not in this pull request

- **W5. The split schedule stays opt-in.**
  - Measured gain on xlings: 1.5 s of 35.5 s (4%).
  - Its policy requires verification on every platform before it becomes a
    default, because a wrong schedule is wrong silently
    (`policy.cppm:196-201`).
  - A gain of 4% does not justify adding that risk to a pull request that
    already changes the pass structure (W2) and the fast path (W4).
- **W6. A faster linker (D3: recorded, not done).**
  - `ld.lld` links xlings in 0.19 s against 1.54 s for `ld.bfd`.
  - A toolchain is not composed from another package's payload: the lld
    inside the llvm payload is not used for a GNU toolchain. The option
    exists only once the ecosystem has an independent `lld` package, and
    it would then be an opt-in key resolved to that package.
- **W7. Dropped (D4).**
  - A build program stays in the project's build directory, a dependency's
    in the consuming project's (`build_program.cppm:586-595`).
  - `mcpp clean` removes it, as it does today.
  - The 0.79 s rebuild of xlings's one dependency build program after
    `mcpp clean` is the accepted cost of that rule.
- **W11. Cache placement on Windows.**
  - The cache pass launches one `mcpp stage` per file: 504 processes in the
    measured build, 80 ms on Linux.
  - On Windows, #734 E4 measured 4.5 s for 1270 per-file placements against
    0.5 s for one process, and moved runtime placements to `stage_list`.
  - This pull request's Windows validation run records the cache pass's
    duration from the log line mcpp already writes
    (`build/stage: ninja-staged-cache`). The change waits for that reading.

## 5. The pull request

One pull request, in this order of commits. Each commit builds and passes
the unit tests, so a bisect over the pull request stays meaningful.

| Order | Commit | Why here |
|---|---|---|
| 1 | W9, planning phase timers | Every later commit is measured with them |
| 2 | W1, animation termination, with the property test | Independent, and the most urgent |
| 3 | W3, #744 and the version memo | Independent |
| 4 | W8, one walk per tree | Changes planning cost only; `build.ninja` is byte-identical |
| 5 | W10, module identity by provider and resolution by closure | `build.ninja` is byte-identical without a collision; before W2 and W4 so that their records carry the resolver's paths |
| 6 | W2, the pass structure and the count | Needed by W4's fast path |
| 7 | W4, plan reuse and post-link checks from a record | Last, measured after W8; deferred on that measurement (section 10.3) |

Verification before merge, following the repository's practice:

- the unit tests and the e2e suite on Linux, macOS and Windows CI;
- the sandbox ecosystem run;
- the validation project's cross-verification from the pull request's
  branch. It checks W2 against a project with 12-minute `prepare` actions
  and supplies W11's Windows reading.

The pull request's description states the before-and-after readings for
the three builds of section 5.1.

### 5.1 Expected figures for xlings on this machine

| Build | Today | After the pull request |
|---|---|---|
| Clean | 35.4 s | about 34.8 s: −0.35 s (W3), −0.5 s or more (W8), up to +0.25 s (W2) |
| Edit of one leaf `.cpp` | 10.5 s | about 7.4 s: planning removed (W3, W4); the compile and the link remain. Measured with W3 and W8 and without W4: 8.0 s (section 10.2) |
| No-op | 0.05 s | about 0.07 s: one more ninja load (W2) |

The clean build stays near 35 s because of the import chain (E1). The edit
stays above 7 s because of the one compile (5.5 s) and `ld.bfd` (1.8 s).

## 6. Decisions

Settled in review round 1:

- **D1.** The row keeps its phase, `Planning`, without a count during the
  cache pass, and shows `Scanning f/t` during the scan pass.
- **D2.** W4's signature is the interface the scanner extracts.
- **D3.** W6 is recorded and not done. A toolchain is not composed from
  another package's payload; only an independent `lld` package would make
  the option possible.
- **D4.** `mcpp clean` removes build programs. W7 is dropped, and build
  programs stay per project.
- **D5.** One pull request.

Open for review round 2:

- **D6.** #732 (F7, W10):
  - an import is resolved in the importer's closure, and a name is unique
    per closure;
  - BMI paths and compiler bindings change only for a collided name;
  - four maps become one resolver;
  - clangd's per-name view of a collided name is a stated limit.
- **D7.** W5 stays opt-in and outside this pull request.
- **D8.** W2's scan pass, with its stated costs: up to 0.25 s on the clean
  build, about 15 ms on a no-op, and a bounded delay for very large projects.

## 7. Self-review

Revisions 1 and 2 were checked against the code, against the measurements
and across items. Five statements were wrong or incomplete; each is
corrected above.

| # | Earlier text | What the check found | Now |
|---|---|---|---|
| 1 | W2: scans run in a pass over every `.dd` | A scan waits on its package's preceding actions (`ninja_backend.cppm:2505-2511`). The pass would hold every compile behind the longest `prepare` action (12 min 49 s in the validation project) | The scan pass covers only scans that wait on no action |
| 2 | W4: rescan and compare, then replay ninja | The fast path declines after any relink, because the post-link validation reads the plan (`runtime_validation.cppm:640-776`). A body edit always relinks, so W4 as written would have saved nothing and run ninja twice | The post-link checks run from a record, on both paths |
| 3 | W8: memoise index trees across processes as immutable | Payload revisions reinstall a version in place | Per-process memo only. The cross-process part waits for a tree identity |
| 4 | Revision 1's W10: per-program scopes for every graph. Revision 2's W10: refuse a name twice per configuration | Revision 1 moved every BMI path and database entry. Revision 2 refused valid programs and left four disagreeing maps in place. The measurements of F7 show that GCC needs a complete map and clang an explicit flag, only for the collided names, and that the ABI draws the boundary per program | Revision 3's W10: resolution by closure, one resolver, and disambiguation only on collision; byte-identical when no name collides |
| 5 | W7: move build programs to the store | Contradicts D4, and the measured 0.79 s belongs to a dependency's build program, which is already per project by design | Dropped |

The checks that found nothing to change:

- **Measurements.**
  - F2's means use the same six-run sets for both versions. The one outlier
    is explained by its own ninja pass.
  - F4's critical path uses the dyndep edges the build itself used.
  - F3's composition sums to the logged total: 503 + 230 + 230 + 231 + 1 =
    1195, which equals `progress: 1195 steps` in the log.
- **Platforms.**
  - W1, W3 and W8 have no platform branch.
  - W10 binds each compiler family in its own spelling. GCC 16 and clang 22
    were measured; MSVC's `/reference` precedence over `/ifcSearchDir` is
    measured for the first time by W10's fixtures on the Windows leg of CI.
  - W3's key uses the inode only where the platform provides one.
  - W2 adds one process launch per build, which costs more on Windows but is
    bounded to one.
  - W4 uses the fast path that exists on every platform since 2026.9.28.3.
- **Workspaces.**
  - W2 runs per configuration graph.
  - W4 extends the per-group records of the workspace fast path.
  - W10 allows two members with one module name when they share no
    program, and still refuses the case where they do.
- **Interactions between items.**
  - W2 and W4 share the pass function.
  - W8 speeds W4's re-expansion of the project's globs.
  - W9 is the instrument for W2's and W8's criteria.
  - W3's memo is read before any plan, so W4's fast path does not depend on
    it.
- **Compatibility.**
  - `build.ninja` is byte-identical after W8, and after W10 for every graph
    without a collided name.
  - W2 changes the counts that e2e tests read. They are updated in the same
    commit.
  - W4's record gains an optional block. An old record declines the fast
    path once.
  - No manifest key and no index format changes.
- **W10 in particular.**
  - *Consistency.* Closures nest. A name that is unique in a consumer's
    closure resolves to the same provider for all of its dependencies, so
    no unit reads a BMI built against another module of the same name.
  - *GCC's missing fallback.* The map lists every named module of the
    closure, `std` and `std.compat` included. It is derived from the
    resolver, not from the imports a scan found.
  - *Header units.* The scanner refuses them (`scanner.cppm:1046`), so a map
    holds named modules only.
  - *The scan step.* A scan wraps its unit's compile command
    (`ninja_backend.cppm:2148-2150`), so the mapper flag reaches the scan as
    well.
  - *The split schedule.* `bmi-compile --bmi` takes its path from
    `bmi_path`, the same function the mapper file is written from.
  - *The BMI cache.* Its entries do not change. Staging already moves a BMI
    from the cache to the build directory, so a BMI does not depend on its
    location; only the destination comes from `bmi_path`.
  - *W4.* A changed module declaration changes the unit's signature, so W4
    plans again, and the map files are regenerated by that plan.
  - *The one limit found.* clangd's view of a collided name (W10, known
    limit).
- **Documentation and prose.**
  - W2's counts and phases, W10's rule and its clangd limit are stated in
    `docs/` and `docs/zh/`.
  - The CHANGELOG entry and the commit messages are English.

## 9. Tasks, their dependencies, and the repositories

| Task | Repository | Depends on | Criterion |
|---|---|---|---|
| T1 W9, phase timers (and the finish steps) | mcpp | none | `build/stage` lines in the log of a planned build |
| T2 W1, animation termination | mcpp | none | property test; fails on 2026.9.30.1 |
| T3 W3, #744 and the version memo | mcpp | none | unit tests, e2e 846; 846 fails on 2026.9.30.1 |
| T4 W8, one walk per tree | mcpp | T1 | directory opens of a planned edit; modgraph tests |
| T5 W10, #732 | mcpp | none | e2e 847 (GCC, clang), 848 (MSVC); byte comparison without a collision |
| T6 W2, pass kinds and the count | mcpp | T5 (the scan goal reads the scopes' owners) | e2e 842, 843; unit test |
| T7 W4, plan reuse | mcpp | T4, T6 | deferred (section 10) |
| T8 documentation, CHANGELOG, version | mcpp | T2 to T6 | docs in both languages; version pins |
| T9 one pull request, CI on every platform | mcpp | T8 | every required check |
| T10 release and the GitCode mirror | mcpp, xlings-res | T9 | four archives, GET on both hosts |
| T11 the index entry | openxlings/xim-pkgindex | T10 | the bot's bump merged; `latest` read back |
| T12 the index's CI pin | mcpp-community/mcpp-index | T11 | `validate.yml` and `latest_mcpp` on 2026.9.30.2 |
| T13 ecosystem verification in a sandbox | local | T11 | `xlings subos use <n> --sandbox` with CN mirrors |
| T14 issues | mcpp, xlings | T13 | #744 and #732 closed with the evidence; xlings#638 opened for E2 |
| T15 acceptance on the validation project | Sunrisepeak/GalTranslPP | T11 | the pull request's CI with the released mcpp |

xlings needs no change in this round: its pin is already the latest release
(2026.9.30.1). The start-up cost of `xlings --version` (E2) is xlings's, and is
stated as openxlings/xlings#638; W3 removes mcpp's dependence on it.

## 10. Implementation record

### 10.1 What was built

- **W9.** Every phase of `prepare_build`, and every step of its last phase,
  logs `plan <phase>: <ms>` under `build/stage` when the log is at info level
  or `--verbose` is on; the backend's own stage lines follow the same gate.
- **W1.** `landing()` answers nothing for a piece that would rest outside the
  screen, `spawn()` answers nothing when no candidate lands inside, and both
  fill loops end when a spawn fails or a locked piece adds no cell. The
  property test drives the four animations over 2000 seeds and the three games
  over 500 (not 10,000: 2000 is the count at which the harness found 66 hangs,
  and the test runs in 4 s). It fails on 2026.9.30.1 (`an animation 'stack'
  did not return within 120 s`).
- **W3.** `choose_xlings_source` is the pure choice and `select_xlings_source`
  gathers the candidates; the first acquisition and the replacement both use
  it, and the replacement copies the chosen file. The memo is
  `<home>/cache/vendored-xlings.versions`, one `<path> <size> <mtime>
  <version>` line per binary. A home settled in a process is not examined
  again.
- **W8.** The walk is kept per (root, start) and matched by the text after the
  pattern's last `*` before the matcher runs. The same kept walk serves the
  scanner, features, graph loading and every other caller of `expand_glob`,
  which is why the graph and feature phases shrank as well as the scan.
- **W10.** As in section 4, with three findings from the implementation:
  - mcpp's naming rule refuses `common` as a top-level module name (it is one
    of `core`, `util`, `common`, `std`, `detail`, `internal`, `base`), so the
    minimal example of #732 is refused by that rule before this one; the
    fixtures use `boost`, the reporter's own module.
  - The reported layout (case A) builds without a note: it violates no rule,
    and a note on every build of a valid layout would be noise.
  - A package that provides a collided name is not placed in the global cache,
    whose entries name BMIs by module; it compiles in the project.
- **W2.** `PassKind` (`Placement`, `Scan`, `Work`) on every pass. Two
  corrections from the tests: a failed scan pass ends the build with its own
  output (the main pass would report the failed step twice), and a scan pass
  names no package at its end (it named every package at once, out of order;
  e2e 842). A build of named goals scans in its main pass.

### 10.2 Measured (xlings, this machine)

| Build | 2026.9.30.1 | 2026.9.30.2 |
|---|---|---|
| Clean, wall time (mean of 3 or more) | 35.4 s | 34.4 s |
| Clean, ninja starts at | about 4.5 s | about 1.3 s |
| Edit of `doctor.cpp` | 10.5 s | 8.0 s |
| No-op | 0.05 s | 0.05 s |
| Planning of that edit | 3.05 s | 0.43 s |
| Directory opens before ninja, that edit | 30,372 | 1,749 |

The planning that remains: make plan 112 ms, the dependency cache 176 ms, the
other phases under 30 ms each. After ninja: runtime validation 78 ms, loader
tags 74 ms, symbol provision 136 ms. The status row of the clean build reads
`Scanning 46/460`, then `Building 20/232` at 0:02, rising steadily to
`231/232` at 0:34.

### 10.3 W4, deferred on its own gate

Section 5 placed W4 last, to be measured after W8. W8 and W3 removed 2.6 s of
the 3.05 s W4 was to save; what W4 could still save is about 0.5 s of an
8.0 s edit (planning and emission), against a new class of silent staleness
and a record-based post-link check on both paths. It is not in this pull
request. The next measured target, if planning becomes material again, is the
dependency cache step (176 ms), whose keys could be kept per tree identity
without a new staleness class.

### 10.4 Verification

- Unit tests of the touched subsystems: dots screen, xlings version,
  modgraph, pack interface, dyndep, build progress.
- e2e 687, 842, 843, 845, 846, 847 (under GCC 16 and clang 22), 801, 805,
  806, 19, 172, 196 and 114 on this machine. 212 fails on this machine with
  2026.9.30.1 as well: its criterion reads GCC's `gcm.cache`, and this home's
  default toolchain is llvm.
- The byte comparison of section 4 (W10): `build.ninja` (3,798 lines) and
  `compile_commands.json` of the xlings workspace, 2026.9.30.1 against the
  branch, with the binary's own path normalised: identical.
- Revert probes: the W1 property test, e2e 846 and e2e 847 each fail on
  2026.9.30.1.

## 8. Appendix: readings

- **Clean builds, wall time (s).**
  - 2026.9.28.2, pty: 37.82, 35.08, 34.11; pipe: 37.31, 34.53, 36.40;
    warm-up: 38.09.
  - 2026.9.30.1, pty: 35.91, 35.35, 35.04; pipe: 35.62, 35.02, 43.06
    (ninja 38.65); warm-up: 36.94.
- **`Finished` (s).**
  - 2026.9.28.2: 33.63, 31.41, 30.37, 33.08, 30.78, 32.61.
  - 2026.9.30.1: equal to the wall time within 0.02 s.
- **Split schedule (2026.9.30.1, clean, s).**
  - Default: 35.23, 35.86 (ninja 31.00, 31.83).
  - `MCPP_BMI_SCHEDULE=on`: 33.38, 34.70 (ninja 29.40, 30.47).
- **Link of `bin/xlings` (s).** `ld.bfd`: 1.59, 1.54, 1.49. `ld.lld` 22.1.8:
  0.19, 0.20, 0.19.
- **Incremental builds (s).**

  | Build | 2026.9.30.1 | 2026.9.28.2 |
  |---|---|---|
  | No-op | 0.05 | 0.03 |
  | Edit of `doctor.cpp` | 10.47 | 11.57 |
  | `touch` of `cancellation.cppm` | 5.30 | 6.49 |
  | Second no-op | 0.05 | 3.56 (not the fast path) |
- **The hang harness.** The body of `class Stack` from `stack.cppm`, with its
  first fill loop capped at 100,000 iterations and the cap reported, driven
  by 2000 seeds over a 240-step build at ten frames per second: 66 runs
  reached the cap.
- **Exec counts in the clean build.**

  | Process | Count |
  |---|---|
  | `sh` | 1199 |
  | `mcpp stage` | 504 |
  | `g++` | 469 |
  | `cc1plus` | 463 |
  | `rm` | 356 |
  | `as` | 233 |
  | `mcpp dyndep` | 230 |
  | `awk` | 230 |
  | `ninja` | 2 |
  | `xlings --version` | 1 |
