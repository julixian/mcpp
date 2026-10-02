---
subject: design
status: active
---

# PR CI acceleration and the toolchain specification (#756, #757, #669)

- Date: 2026-10-02. Status: proposed for review; revision 2. Nothing in this record is implemented.
- Base: `main` at `4d81d062` (2026.10.1.3).
- Inputs:
  - CI jobs 110514498085, 110514498447 and 110514498760 on `main`.
  - Issues #756, #757 and #669, with #669's 2026-10-01 measurement.
  - A census of every place mcpp states a toolchain version.
  - The packaging pipeline of the `gcc` and `llvm` payloads, and the upstream state of GCC 16.2 and
    LLVM 23.
  - The GitHub Actions record of seven head commits: four pull-request heads (#758, #754, #752, #745) and
    three pushes to `main` (`4d81d062`, `68e49981`, `23c9590b`). Every job, step, queue time, cache lookup
    and ninja count of these runs was measured.

**Revision 2.** Revision 1 proposed moving the default toolchains to GCC 16.2 and LLVM 23. Review on
2026-10-02 settled the toolchain work as a specification only: SPEC-009 is written, and no version moves
in this work (Part V keeps the analysis for the move that will follow it). The subject of this record
therefore became the time a pull request waits for CI. That time is dominated by work the CI repeats,
and the triage of the three red jobs that started this record was the first evidence of it.

Reading order:
- Part I triages the inputs.
- Part II is the main design: where PR CI time goes, the rules that remove the repetition, and an optional
  external lane.
- Part III states the defects the next release repairs.
- Part IV is the toolchain specification, as approved.
- Part V keeps the toolchain candidates for later.
- Part VI orders the work, Part VII lists what is not yet measured, and Part VIII asks the open questions.

## 0. Decisions

### 0.1 Settled in review (2026-10-02)

| # | Decision |
|---|---|
| T1 | The toolchain specification (Part IV) is published as a new specification, SPEC-009; SPEC-006 keeps identity, selection and the payload contract, and its §7 becomes a reference to SPEC-009 |
| T2 | No default toolchain moves in this work. GCC 16.2 and LLVM 23 are candidates (Part V), each subject to the gate of TS-10.5 (G1 to G6, tolerance 10 percent) |
| T3 | LLVM moves only to 23.1.3, once it passes the gate; 23.1.2 is measured as an early reading; `23.1.2-r1` only on explicit request |
| T4 | A default that an earlier mcpp recorded on a first run is announced once when a newer default exists, and the user chooses to move or keep it (TS-11.2); a fresh install announces nothing |
| T5 | When LLVM moves, the bare-metal and iOS rows lag with the reason recorded |
| T6 | GCC 16.2 is a candidate only if Bug 126577 does not reproduce in mcpp's own build and suite; otherwise the GCC line waits for 16.3 |
| T7 | The GCC recipe and builder live in `xlings-res/gcc`, as `musl-gcc`'s do |
| T8 | The macOS e2e step limit rises now; Part II replaces it with measured shards |

### 0.2 Requested now

| # | Decision | Recommendation |
|---|---|---|
| D1 | Each commit builds mcpp once per host, and every other job consumes that build (§2.4.1) | Yes |
| D2 | One top-level workflow orchestrates the stages; per-area work moves into reusable workflows (§2.4.2) | Yes |
| D3 | A pull request that changes only documentation runs the documentation checks and nothing else (§2.4.3) | Yes |
| D4 | Only pushes to `main` write caches; one job writes each key; the `target/` caches are removed (§2.4.4) | Yes |
| D5 | The e2e suite is sharded by measured duration, and a shard's step limit follows from its budget (§2.4.5) | Yes |
| D6 | Every e2e test runs on some host or carries a recorded reason, checked in CI; the `llvm` capability is granted (§2.4.6) | Yes |
| D7 | Legs that are known red run on `main` and on dispatch, not on pull requests (§2.4.7) | Yes |
| D8 | An external lane under `speak-agent` is designed now and built only if the rule of §2.5.5 holds after D1 to D7 | Yes |
| D9 | The next release repairs #757 and #756 (Part III) | Yes |
| D10 | The single line table of TS-3 waits for the first toolchain move | Yes |

## Part I. Triage

### 1.1 The three CI jobs

| Job | Leg | Failing step | Cause | Class |
|---|---|---|---|---|
| 110514498085 | `ci-macos`, `xcode-27` (known red #669) | Test: non-module C++23 compilation | `ld64.lld` 22.1.8 cannot load `libc++.tbd` and `libSystem.tbd` of the Xcode 27 SDK (`unknown architecture`, `arm64e.x1`); every undefined symbol after it is a consequence | external, tracked by #669 |
| 110514498447 | `ci-macos-e2e`, `xcode-27` (known red #669) | Build mcpp from source | the same signature while linking `bin/mcpp` | external, tracked by #669 |
| 110514498760 | `ci-macos-e2e`, `macos-15` | E2E suite | the step reached its 25-minute limit at test 867 of 877; every earlier test passed, and test 867 was still printing readings when it was stopped | a time budget in this repository |

The third job is not a hang. The suite started at 18:30:52 and was stopped at 18:55:54. The last green
run of the same suite (36895492498, the head of #755) took 21 min 01 s, so the budget left 19 percent
for runner variance, and this runner used more. The workflow's header still states that the suite
takes about 3.5 minutes on macOS and is therefore not sharded (`ci-macos-e2e.yml:6-7`); that was true
when it was written and is now false by a factor of six.

None of the three is a usage error or a non-conforming project.

### 1.2 #756: a provider's `.ixx` host module is invisible to the fast path

**Conformance.** The reproducer conforms to the manifest specification. `.ixx` is opt-in through
`[build] module_extensions` (the built-in table is `.cppm` only, `modules/source-kind/src/source_kind.cppm:121`);
`host-module = true` is the Cargo-inherited kebab key and `module_extensions` the mcpp-owned snake key
(SPEC-004, naming table); `[lib] path` and `mcpp::define` (`src/build/hostprogram.cppm:48`) exist. The
workaround the report describes, declaring `.ixx` in the consumer, is excluded: the consumer has no
`.ixx` source, and the dead-entry warning it then receives is correct.

**Mechanism.** `fast_path_identity` builds one extension table from the consumer's effective manifest
(`src/build/execute.cppm:1418-1437`). `dep_sources_newer_than` (`execute.cppm:1172-1215`) classifies
every file under every path-dependency root with that table. A `.ixx` file that only its own package
declares is classified as not affecting the graph's shape and is skipped. The host module is compiled
into the consumer's build program, which no edge of `build.ninja` names, so ninja cannot see the edit
either, and the fast path replays the old graph with the old build program.

Classification is a property of the package that owns the file; the sweep asks a different package.
The mechanism does not depend on the platform, although the report was made on Windows.

**Verdict.** An engine defect. Repair in §3.2.

### 1.3 #757: an upgraded engine replays a graph that names the removed one

**Conformance.** Upgrading mcpp through xlings and removing the previous version is ordinary use.

**Mechanism.** The emitter writes the absolute path of the running engine into `build.ninja` for the
`__action` wrapper and for `stage` (`src/build/ninja_backend.cppm:3502-3512`), and states why that is
safe: "A version change regenerates the file (the version is in the fingerprint)." The fingerprint does
contain the version (`modules/toolchain-model/src/fingerprint.cppm:125`), but no fast path computes a
fingerprint. `try_fast_build` (`execute.cppm:1594`), `try_fast_workspace_build` (`:1749`) and
`try_fast_run` (`:1884`) select a recorded entry by target, profile, cache mode, features and toolchain
request, and then compare the recorded fingerprint with the recorded directory's basename, which is the
record compared with itself. `BuildCacheEntry` (`execute.cppm:150`) records no engine identity. The
emitter assumed a property that the fast path never promised. When the old executable still exists the
failure is silent instead: a newer front end drives the actions of an older engine.

**A structural observation.** The three fast paths carry 23, 30 and 30 hand-written declines. A
property added to one must be added to the others by hand, and every field of `BuildCacheEntry` carries
its own paragraph explaining what its absence means. #757 is the case in which no path received the
property at all.

**Verdict.** An engine defect. Repair in §3.1. Recognising `CreateProcess failed` in
`is_stale_ninja_failure` is rejected: it is a criterion over error text, and a launch failure of a
program the user declared must not be retried as a stale graph.

### 1.4 #669: the macOS 27 link

- The Xcode 27 SDKs list `arm64e.x1-macos` and `arm64e.x1-maccatalyst` in their `.tbd` stubs.
  `ld64.lld` up to 23.1.2 rejects them (`unknown architecture` in 22.1.8, `unknown target` in 23.1.2).
- The fix, llvm/llvm-project#222721, merged to `main` on 2026-09-11. It reached `release/23.x` on
  2026-09-29 as `532fa5afbe2b`, `8f747d63ffb2` (lldb) and `ee66426152f9` (no ABI break), after
  `llvmorg-23.1.2` was tagged on 2026-09-22. The 23.1.2 announcement schedules 23.1.3 for Tuesday
  2026-10-06.
- Measured (speak-agent/llvm-macos27-lab, run 36878561820): an `ld64.lld` built from `release/23.x` at
  `21ef2ddb8060`, with every other component taken from the official 23.1.2 package, links and runs C,
  C++23 and `import std` on the `xcode-27` image (macOS 27.0, Xcode 27.0, SDK 27.0). The stock 23.1.2
  `ld64.lld` fails there; both link on `macos-15` (SDK 15.5). The Command Line Tools `MacOSX26.5.sdk`
  does not list the slice, so the failure follows the SDK, not the operating system.
- Other distributors carry the backport as a patch: Homebrew's `llvm` formula from 23.1.1_1, and
  hermetic-llvm for 22 and 23. Apple's own linker reads the slice (third-party reports, not Apple
  documentation).

It is neither an mcpp defect nor the SDK-selection defect that xim-pkgindex#858 repaired.

### 1.5 What the census found

1. **The host defaults are older than the title of this work presumes.** macOS and Windows with MSVC
   default to `llvm@20.1.7` (`modules/toolchain-model/src/triple.cppm:1045`, `:1049`); Linux on an
   architecture other than x86_64 defaults to `gcc@15.1.0-musl` (`:1057`). `llvm@22.1.8` appears only as
   the pin of 17 target rows and as mcpp's own `[toolchain] macos`.
2. **The macOS default cannot build mcpp.** libc++ 20's `std` module does not expose the comparison of
   `directory_iterator`, so `llvm@20.1.7` cannot compile mcpp's sources (`.github/workflows/ci-linux.yml:278-284`);
   mcpp's own manifest therefore builds with 22.1.8 while it gives its users 20.1.7.
3. **Defaults are stated in two tables and copied widely.** Table A is `pins::kFirstRun*` with duplicate
   `kSuggest*` literals (`triple.cppm:1034-1081`); table B is `kKnownTargets[].pin` (`triple.cppm:447-845`).
   Beyond them: 21 literals in nine source files, `mcpp.toml`, 175 rows of `tests/matrix/expected.tsv`,
   seven workflows, one action, six CI tools, at least eight e2e scripts, examples, and 33 documentation
   files. Two couplings are checked: `kFirstRunWinGnu` against the `x86_64-windows-gnu` row
   (`tests/unit/test_windows_defaults.cpp:55-61`), and the host default against four documentation
   statements (`.github/tools/check_default_toolchain_docs.py`).
4. **Two capability probes name payload versions.** `tests/e2e/run_all.sh:70-71` grants `musl` only if
   `musl-gcc/15.1.0` is installed, and `:78-79` grants `mingw-cross` only for `mingw-cross-gcc/16.1.0`.
   When the default moves, sixteen tests skip and report nothing.
5. **A first-run default is written once.** `src/build/prepare/toolchain.cpp:1607` persists it to
   `config.toml`, and nothing revisits it. A default that moves therefore reaches fresh homes only.
6. **The pack ABI tag carries the compiler major** (`src/pack/abi_tag.cppm:215-224`). GCC 16.1 to 16.2
   keeps `gcc16-libstdcxx16`; LLVM 22 to 23 changes `clang22-libcxx22` to `clang23-libcxx23`, and a
   prebuilt artefact with the old tag is refused (`src/pack/prebuilt.cppm:117`). `mcpp-index`'s
   `main` holds no such artefact today.
7. **The GCC payload has no recipe that runs anywhere but one machine.** 16.1.0 was built by hand from
   the `fromsource` recipe (`make -j8`, about 28 minutes), then stripped and its `specs` rewritten by
   steps no repository scripts; SPEC-006 §5.1 already records the gap. `musl-gcc` and
   `aarch64-linux-musl-gcc` have dispatchable builder workflows in `xlings-res`; `mingw-cross-gcc` and the
   Windows-host Canadian cross are manual; `mingw-gcc` mirrors winlibs, which publishes 16.2.0.

## Part II. PR CI acceleration

### 2.1 What was measured

Seven head commits were measured from the GitHub Actions API and the job logs. The four pull-request heads
are the final heads of #758 (`51b66cec`), #754 (`71d09f0b`), #752 (`72e238dc`) and #745 (`ac4beb88`).
The three pushes to `main` are `4d81d062`, `68e49981` (documentation only) and `23c9590b`. Only runs with
event `pull_request` or `push` are counted. The release workflow, which a tag push starts beside the CI of
the same commit, is reported separately. The repository has no required status check, so a pull request
waits for the last job to finish; the four pull requests were merged between 32 s and 183 s after their
last job ended.

| | #758 | #754 | #752 | #745 | `4d81d062` | `68e49981` (docs) | `23c9590b` |
|---|---|---|---|---|---|---|---|
| jobs | 47 | 45 | 46 | 46 | 35 | 35 | 35 |
| wall clock, first attempt (min) | 49.6 | 42.0 | 37.1 | 35.7 | 59.5 | 42.9 | 27.0 |
| wall clock with reruns (min) | 79.5 | 42.0 | 37.1 | 50.7 | 59.5 | 42.9, left red | 27.0 |
| wall clock without queueing, modelled (min) | 36.3 | 28.6 | 26.7 | 25.5 | 59.4 | 33.5 | 27.0 |
| runner minutes | 568 | 477 | 467 | 501 | 478 | 436 | 417 |
| builds of mcpp from source | 37 + 6 | 35 + 6 | 36 + 6 | 36 + 6 | 30 + 6 | 30 + 6 | 30 + 6 |

The second figure in the last row counts the deliberate rebuilds after `mcpp clean` or with another
toolchain.

Where the runner minutes go, as a share of each commit's total:

| category | share |
|---|---|
| building mcpp (primary builds, then the deliberate rebuilds) | 34-40 percent, then 8-11 percent: 44-49 percent together |
| the e2e suite | 18-22 percent |
| `mcpp test` | 6-7 percent |
| setup actions | 4-14 percent; the high values are `Install wine` (below) |
| cache saves | 1-5 percent |
| everything else | 13-17 percent |

Median duration of the primary build step, over all seven commits:

| host | median | range |
|---|---|---|
| Linux | 6.6 min | 4.1 to 16.3 min; the maximum is the aarch64 fresh install |
| Windows | 4.8 min | 2.2 to 6.0 min |
| macOS | 2.7 min | 1.9 to 4.9 min |

### 2.2 What the measurements show

**F1. Every job builds mcpp, and no job reuses another's build.**
- Thirty to thirty-seven jobs per commit compile mcpp from source with the pinned bootstrap.
- Only three hand-offs of a built binary exist: ci-windows `build-test` to `no-msvc-fallback`
  (`ci-windows.yml:113-165`), the openkal `run` legs, and the Windows-to-Linux cross build.
- The present shape was chosen deliberately (`ci-linux.yml:8-15`), on the premise that a warm rebuild costs
  about 2.5 minutes, which is cheaper than serialising the legs behind a shared artifact. The premise no
  longer holds: the measured median is 6.6 minutes on Linux, and F2 shows that the rebuild is not warm.

**F2. The `target/` cache makes no build incremental.**
- On the documentation-only commit `68e49981`, the compiled inputs were identical to the previous commit's,
  and 14 jobs restored their `target/` by exact key. In the Linux `build + unit tests` job of that commit,
  ninja still ran 830 of 830 edges.
- On #758, a restore-key hit ran 834 of 834.
- In #758's `toolchain: gcc`, the first build (with 94 MB of restored `target/`) took 362.75 s. The cold
  rebuild after `mcpp clean` took 381 s, only 5 percent more.
- Build steps after an exact hit are no faster than after a miss (Linux 488 s against 340 s, where the six
  exact hits also missed the sandbox; Windows 293 s against 324 s).
- The cause is not measured. Checkout giving every source a modification time newer than the restored
  outputs is the likely one.

**F3. The caches evict each other.**
- The repository held 10.78 GB of caches against a 10 GB limit.
- A pull-request run saves 34 to 42 caches, of 17 MB to 1.6 GB each; `target/` caches alone range from
  46 MB to 3.3 GB. 26 job identifiers form 26 `target/` lineages on Linux.
- In 155 job logs:
  - the sandbox cache missed 44 percent of lookups and the xlings cache 36 percent;
  - a Windows sandbox key that hit at 17:02 and 17:11 was gone by 17:44;
  - after #758's saves, the next push to `main` missed 20 of 26 sandbox lookups.
- Parallel jobs race to save one key: two to twenty-seven "Unable to reserve cache" failures per commit.
- A cancelled run saves nothing, and every CI workflow cancels in progress, including on `main`.

**F4. The 20-job limit is reached by this repository alone.**
- On each of the seven commits, the peak number of running jobs was exactly 20; macOS reached its own
  limit of 5 on three of them.
- Sixty of the 67 Linux and Windows jobs that queued 90 s or more started within 8 s of another of our
  jobs finishing, which is the signature of a slot cap.
- Queueing added 9 to 13 minutes of wall clock on five of the seven commits.
- macOS jobs queued up to 12.7 minutes. 15 of the 18 macOS jobs that queued over two minutes carry
  GitHub's capacity notice, so part of the macOS wait is GitHub's capacity rather than ours.

**F5. The e2e suites are the last jobs, and they hit their limit.**
- An e2e shard was the last job on three of the seven commits (Windows 2/2 at 27 to 34 minutes; Linux 2/2
  at 28.6).
- The 25-minute step limit was reached on three of the seven commits, and a fourth failed a test that
  passed on rerun. Two were rerun, at +30 and +15 minutes of wall clock; two stayed red on `main`.
- On Windows, about 15 tests of over 30 seconds take 58 to 64 percent of a 15 to 18 minute shard. The same
  tests take about one second on Linux, for example `36_llvm_toolchain` and
  `687_the_vendored_xlings_probe_is_an_argument_vector`.
- Shards are assigned round-robin (`tests/e2e/run_all.sh:379-414`), and the two shards differ by 2.7
  minutes on average on Linux and 3.6 on Windows.
- macOS is not sharded: one job of 242 tests runs 17 to 21 minutes. The workflow header still gives
  3.5 minutes (`ci-macos-e2e.yml:6-7`).

**F6. A documentation-only commit costs as much as a code commit.**
- No workflow has a `paths-ignore`.
- `68e49981`, which changed two files under `.agents/docs`, ran 35 jobs and 36 builds in 436 runner
  minutes, against 417 for the code commit before it, and it ended red on a Windows shard timeout.

**F7. One evicted cache costs up to fifty minutes.**
- When the `wine-debs` cache is evicted, `apt` downloads the Wine packages at stalled speeds:
  - 25.8 minutes on #758;
  - 49.9 minutes on `4d81d062`, where libwine (105 MB) alone took 841 s;
  - 40 seconds when the cache hits.
- On two of the seven commits this made `mingw-cross-wine` the last job (59.5 minutes of wall on
  `4d81d062`).

**F8. Known-red legs spend macOS slots on pull requests.**
- The two `xcode-27` legs fail on every commit (#669), after 2 to 4 minutes, each holding a macOS slot.
- A pull request cannot change their outcome unless it is about #669.

**F9. Seven e2e tests never run in CI.**
- `run_all.sh` lists `llvm` as a capability a test may require (`run_all.sh:263`), but no line grants it.
- Of the 32 tests that require it, 25 are invoked directly by name in a dedicated job.
- Seven are invoked nowhere: 134, 135, 136, 137, 741, 875 and 876. Among them are 875 and 876, the tests
  #755 added for a toolchain named by path and for the toolchain phase.
- Their skip lines read like legitimate ones.

### 2.3 Rules

These rules govern PR CI. They are stated so that a later change of the CI can be checked against them.

| Rule | Statement |
|---|---|
| R1 | A commit builds mcpp from source once per host; every job that needs the commit's mcpp consumes that build. A job builds mcpp again only when the rebuild is what it tests: another toolchain, a cold build, a cross target. |
| R2 | What a job runs follows from what the commit changed. A change that cannot affect a job's outcome does not start the job. |
| R3 | A cache has one writer, the `main` lineage. Pull requests restore; they do not save. A cache that does not shorten the job that restores it is removed. |
| R4 | A job's time limit follows from its measured duration; a limit that is reached without a hang is a budget defect, not a test failure. |
| R5 | Every test runs on at least one host, or carries a recorded reason why no host runs it; CI checks this. |
| R6 | The scarcest resource is a macOS slot (5 per organisation). A macOS job runs only work that needs macOS. |
| R7 | A pull request waits only for work that can change its outcome. |

### 2.4 The design inside the repository

#### 2.4.1 One build per host (R1)

Each commit has one build job per host:

| host | runner |
|---|---|
| Linux x86_64 | ubuntu-24.04 |
| Linux aarch64 | ubuntu-24.04-arm |
| macOS arm64 | macos-15 |
| Windows x86_64 | windows-latest |

A build job:
1. restores the sandbox;
2. runs the bootstrap;
3. builds the commit's mcpp;
4. packs it with `mcpp pack`, as ci-windows `build-test` already does for `no-msvc-fallback`
   (`ci-windows.yml:68-119`), so that the artifact carries its own runtime closure;
5. uploads the result as an artifact.

Every consumer downloads and unpacks that artifact and uses it as `$MCPP`. A consumer still restores the
sandbox when its tests need toolchain payloads. The `find … -newer mcpp.toml` and newest-binary selections
that protect against a stale cached binary (`ci-target-matrix.yml:59-63`, `ci-windows.yml:43-48`,
`openkal-cross.yml:150-180`) are removed, because the binary no longer comes from a restored `target/`.

Jobs whose subject is a rebuild keep it, and start from the artifact instead of a bootstrap build:
- the GCC cold rebuild;
- the musl and LLVM builds;
- the cross builds;
- the macOS rebuild with the LLVM default;
- the Windows LLVM rebuild.

This removes the bootstrap build that each of them pays first.

`mcpp test` keeps a job of its own per host in the first wave. It compiles the code under test and the
144 unit-test programs itself, so the artifact would spare it only the bootstrap build, at the price of
waiting for the build stage. On Linux it is the longest job today: 24.7 minutes, of which 8.2 are the
build and 15.4 the tests. If it becomes the critical path, it splits into the root suite and the
per-member suites (`ci-linux.yml:196-218`).

The target-matrix and openkal legs on `macos-14` and `windows-2022` consume the artifact of their host
family. For `macos-14` this holds only if the artifact's minimum macOS version is at most 14 (Part VII).
Whether these legs must build with `--dev` is not recorded anywhere; they move to the artifact unless a
reason is found (Part VII). The target matrix's `invariants` and `scan` become one job per host, because
`scan` already waits for `invariants` on the same host (`ci-target-matrix.yml:233`).

Counted per pull-request commit, the change is:
- builds: from 41 to 43 down to about 16, which is four artifact builds, three `mcpp test` jobs, and about
  nine rebuilds that are a job's subject;
- build runner minutes: from about 235 down to about 90.

#### 2.4.2 One workflow, stages in order (R1, R7)

An artifact is shared without extra machinery only within one workflow run. The CI therefore becomes
one top-level workflow, `ci.yml`, with three stages:

1. `changes`: classify the commit's paths (§2.4.3), in seconds.
2. `docs`: the text checks that need no binary. They are the 14 checks `build-test` runs today before
   its bootstrap (`ci-linux.yml:55-158`).
3. `build`: a matrix over the four hosts.

The present per-area workflows become reusable workflows that the top-level workflow calls with the
artifact names: Linux, Linux e2e, Windows, Windows e2e, macOS, macOS e2e, iOS, target matrix, cross
build, openkal cross, and the MSVC xlings test. Their jobs keep their names, so that the checks a reader
knows remain recognisable. Path-gated workflows (`ci-aarch64-fresh-install`,
`measure-windows-tool-crt`, `pypi-publish`) stay separate.

The wall clock of a pull request becomes the sum of three stages:

| stage | duration |
|---|---|
| `changes` | under a minute |
| the slowest build | about 10 minutes: Linux setup, a 6.6-minute build, packing, upload |
| the slowest consumer | setup, plus the longest shard of §2.4.5 |

The first wave of jobs is the four builds, the docs job and the three `mcpp test` jobs. The first wave
today is all 46 jobs at once, which is what saturates the 20 slots (F4).

#### 2.4.3 What a change starts (R2, R7)

`changes` sorts the changed paths into classes:

| class | paths | starts |
|---|---|---|
| documentation | `**/*.md`, `docs/**`, `.agents/**`, `LICENSE*` | the `docs` stage |
| documentation that a check reads | a changed documentation path that a script under `tests/` or `.github/tools/` names, found by searching for it; today this includes `docs/01-getting-started.md`, `docs/20-toolchains.md` and `docs/03-examples.md`, with their `docs/zh/` copies | the `docs` stage, plus the Linux build and every test and tool that names the path, among them the default-toolchain check (`check_default_toolchain_docs.py`) and e2e 616 |
| everything else | | the whole CI |

A commit that changes nothing outside the documentation class costs one docs job of about two minutes,
instead of 35 jobs and 436 runner minutes (F6). No check is required by the repository's rules, so a
workflow that does not start leaves no check pending.

The second class is derived by a search, not listed by hand, so that a test added later to read a
document is found without editing the classifier. The classes are deliberately coarse. A finer selection, such as running only the e2e tests a change
could affect, is rejected. A differential selection cannot see a test that is red on both sides, and the
repository has paid for that before.

#### 2.4.4 Caches with one writer (R3)

- **Who writes.** Sandbox (`~/.mcpp`) and xlings (`~/.xlings`) caches are saved only by the build job of
  each host, and only on a push to `main` (`actions/cache/save` after the build).
- **Who reads.** Every other job, and every pull-request job, uses `actions/cache/restore`. A pull request
  reads the `main` lineage, which GitHub scopes to every branch, and adds nothing to the store.
- **Saves per run.** From 34 to 42 down to at most three per host (sandbox, xlings and the e2e durations of
  §2.4.5), each key with one writer, on `main` only.
- **The `target/` caches are removed.** F2 shows they do not shorten a build, and at up to 3.3 GB each they
  are the main cause of F3. They return only if the build becomes incremental across checkouts. That needs
  a measured cause (Part VII) and a fix that keeps the stale-object hazards already recorded in
  `.agents/docs/2026-05-15-stdcompat-restat-e2e.md` and `cross-build-test.yml:416-434` out of the result.
- **Concurrency.** `cancel-in-progress` becomes `${{ github.event_name == 'pull_request' }}`. A push to
  `main` runs to completion and writes its caches; a superseded pull-request run is still cancelled.
- **Wine.** The Wine packages come from one pinned archive, published once as a release asset of this
  repository, instead of from the `apt` mirrors with a cache that eviction removes (F7). The archive's
  sha256 is checked before installation.

#### 2.4.5 Shards by measured duration (R4)

- `run_all.sh` already reports each test's duration (`run_all.sh:452-462`). On a push to `main`, each shard
  writes its durations under a key of its own, and the next pull request merges the shards of its host.
- A pull request assigns tests to shards greedily by duration, longest first. This is the plan recorded
  in `.agents/docs/todos/2026-06-24-e2e-suite-sharding.md`, of which only round-robin was built.
- A test without a recorded duration counts as the host's median.
- Assignment changes only balance, never which tests run, so a stale timing file cannot hide a test.

Once §2.4.1 lands, a shard no longer pays a build, so more shards cost only their setup. The proposed
counts:

| host | shards | each shard |
|---|---|---|
| Linux | 3 | about 10 minutes |
| Windows | 3 | about 12 minutes, the long tests spread first |
| macOS | 2 | about 10 minutes |

Each shard's step limit is twice its budgeted duration, so a limit is reached only by a hang (R4). The
25-minute limits that F5 measured being reached become 20 to 24 minutes on shards half as long.

The 15 slow Windows tests are a finding in their own right. A test that takes one second on Linux and
up to 104 seconds on Windows measures something about Windows, and Part VII lists it for investigation.
It is not a reason to remove them from the suite.

#### 2.4.6 No test is silently absent (R5)

- `run_all.sh` grants `llvm` when the sandbox holds an LLVM payload with `clang++`, as it already does for
  `scan-deps` and `import-std-libcxx` (`run_all.sh:221-230`). The Linux e2e shards install the LLVM
  payload beside GCC.
- Every shard writes the list of tests it ran and skipped, with the reason for each skip, as a step
  summary and as a small artifact.
- A final `e2e-coverage` job collects the lists of every host. It fails if a test ran on no host and is not
  listed in `tests/e2e/never-in-ci.tsv`, which states for each such test why no hosted runner can run it.
- The seven tests of F9 must either run or be listed. On Linux, 134, 135, 136, 137, 741, 875 and 876 are
  expected to run once `llvm` is granted.

The run_all.sh comment at `:290-305` records why a hard-required capability was rejected: a token cannot
tell a misconfigured runner from a platform that lacks the capability. The coverage job answers the
question that the token could not, because it knows every host's list.

#### 2.4.7 macOS slots (R6, R7)

- The `xcode-27` legs, which are known red, run on pushes to `main` and on dispatch, and on a pull request
  only when the pull request carries the label `macos-27`. A pull request that addresses #669 sets the
  label.
- The macOS legs of the target matrix and of openkal consume the macOS artifact. macOS builds of mcpp per
  commit fall from eight to one.
- The number of macOS jobs per pull-request commit changes less than their minutes do: from 9 or 10 to
  about 8.
  - The build.
  - Two e2e shards.
  - `ci-macos` integration, which keeps its fresh sandbox (`setup-macos-llvm/action.yml:7-9` states that
    a fresh sandbox is what it proves).
  - iOS.
  - The target matrix as one job.
  - openkal's build and run legs.
- What falls is the time each macOS job holds a slot: no job but the build compiles mcpp, and the two
  known-red legs leave pull requests.

### 2.5 An external lane under `speak-agent` (optional)

#### 2.5.1 What it would buy

The 20-job and 5-macOS limits apply per account or organisation, not per repository. `mcpp-community` is
on the Free plan. A repository under `speak-agent`, a machine account, runs against a second pool of
the same size. One repository is enough, because more repositories under one account add no capacity.
The lane can therefore raise the parallelism of a pull request. It cannot shorten a job.

#### 2.5.2 Constraints

- **Terms of Service.** GitHub allows one free account per person, plus a machine account that is used
  only for automated tasks; `speak-agent` is one. The Actions terms forbid using hosted runners for
  activity unrelated to the testing of the software project associated with the repository in which the
  Actions run. A lane repository whose stated purpose is to test `mcpp-community/mcpp`, which runs only
  mcpp's tests at mcpp's commits, satisfies the wording. Spreading one project's CI over two accounts to
  exceed one plan's concurrency is not addressed by the terms, and that is a risk this record cannot
  resolve. The lane is therefore optional. It has an off switch, and the repository's own CI stays
  complete without it.
- **Forks.** A `pull_request` run from a fork receives no secrets, so it cannot dispatch the lane. Fork
  pull requests run every leg inside the repository.
- **Authority.** A push to `main` always runs every leg inside the repository. The lane serves pull
  requests only.

#### 2.5.3 Shape

- **Repository.** `speak-agent/mcpp-ci-lane`. It is separate from the validation labs, whose `main`
  holds only rules and a guard; the lane's workflows live on its `main`, because they are dispatched there.
- **Dispatch.** A job in mcpp's CI (seconds, no slot held while the lane runs) calls the lane's
  `workflow_dispatch` with the commit, the pull request number and the legs. The token is a fine-grained
  token of `speak-agent` with Actions write on the lane repository only, stored as an mcpp secret.
- **Lane run.** It checks out `mcpp-community/mcpp` at that commit (public, no token). It builds once per
  host as in §2.4.1, and runs the legs as consumers.
- **Test jobs.** They have `permissions: {}` and no secret.
- **Report.** A last job, which runs no code from the commit, writes one commit status per leg to the mcpp
  commit (`lane/macos-e2e-1` and so on), linking to the lane run. Its token is a second fine-grained token
  with commit-status write on `mcpp-community/mcpp` only. That job reads its inputs from
  `needs.<job>.result` only.
- **In mcpp.** When the lane is on (a repository variable), the legs it carries are skipped on same-repo
  pull requests and run as usual everywhere else.

#### 2.5.4 Costs

- Two places to read logs.
- Two secrets to rotate.
- A cold cache lineage in the lane until its own runs warm it.
- Contention with the validation labs, which use the same `speak-agent` pool.

#### 2.5.5 When to build it

Only if, after D1 to D7 have landed, the median first-attempt wall clock of five consecutive code pull
requests exceeds 30 minutes, and queueing accounts for more than a fifth of it. The first legs to move
are then the macOS and Windows e2e shards, because macOS slots are the scarcest and Windows shards the
longest.

### 2.6 Expected effect and how it is checked

An estimate, from the medians of §2.1, to be replaced by measurement:

| | today (median of the 4 pull requests) | after D1 to D7 |
|---|---|---|
| from-source builds of mcpp | 42 | about 16 |
| runner minutes | 489 | about 330 |
| first-attempt wall clock | 39.6 min | 25 to 28 min, of which about 10 is the build stage |
| macOS jobs, and macOS builds of mcpp | 9 to 10, and 7 | about 8, and 1 |
| caches saved | 34 to 42 | none on a pull request; on `main`, the sandbox and xlings caches of each host and one durations key per shard |
| a documentation-only commit | 35 jobs, 436 runner minutes | 1 to 3 jobs, under 15 runner minutes |

The work is accepted when five consecutive code pull requests after it show all of the following. The
measurement uses the same scripts as §2.1.
- A median first-attempt wall clock of at most 28 minutes.
- At most 16 from-source builds per commit.
- No e2e step reaching its limit.
- A green `e2e-coverage` job.
- Cache usage below 8 GB.
- A documentation-only pull request finishing under 5 minutes.

### 2.7 What was built, and where it departs from §2.4

Implemented in mcpp#759. Each departure below was decided by a measurement or a
reading taken while building it.

- **The artifact is the binary itself, not a packed copy (§2.4.1).** The Linux
  self-host binary has the interpreter
  `~/.mcpp/registry/data/xpkgs/xim-x-glibc/2.44/lib64/ld-linux-x86-64.so.2`, needs
  `libgcc_s.so.1` from the `xim-x-gcc/16.1.0` payload, and links libstdc++
  statically (measured locally, `readelf`). A consumer that restores the
  sandbox runs it as it is. Packing it would test a different binary from the
  one every self-host build produces. `use-built-mcpp` runs the binary first,
  and only when it does not run does the bootstrap install the toolchain
  `mcpp.toml` names for the host; a binary that still does not run fails the
  step. The binary is 25 MB.
- **The Wine packages keep their cache (§2.4.4).** Their eviction was a
  consequence of F3, not a property of the cache. With one writer per key, the
  `wine-debs` cache is saved only by `mingw-cross-wine` on main and is no longer
  displaced. A release asset would have added a second thing to publish and
  keep current.
- **The timing tables are in the repository (§2.4.5).** They live under
  `tests/e2e/timings/<host>.tsv`, seeded from the per-test lines of the
  2026-10-01 logs. A shard's membership is then a function of the commit,
  which makes it reproducible (`E2E_LIST=1` prints it). The `e2e-coverage` job
  uploads the merged durations of each run as the artifact `e2e-timings`, and
  refreshing a table is copying a file. The tables were seeded from the
  2026-10-01 logs, then replaced by the durations of the first run of this
  change, in which 515 Linux tests ran instead of 466. Linux therefore has four
  shards rather than three, each budgeted at 9.9 minutes. Windows has three at
  14.4 to 14.5, and macOS two at 9.1 and 9.3. The step limits are about twice
  the budgets: 22, 30 and 20 minutes.
- **The classifier searches exact paths (§2.4.3).** It searches for the
  changed path and for its translation (`docs/X` and `docs/zh/X`), and not for
  a bare file name. A search on the name made `.agents/docs/README.md`, which
  every new record regenerates, a code change, because release packaging names
  `README.md`. The checks of the `docs` job do not count as readers, because
  they run on every change. A document that any other script or source names
  starts the whole CI rather than a subset of it, which is simpler and errs
  towards running more.
- **The macOS legs of the target matrix and of openkal still build (§2.4.1).**
  They run on `macos-14`, and the artifact is built on `macos-15`. release.yml
  records that a bootstrap build linking the system libc++ with a minimum
  version of 14 failed at launch on `macos-14`. Until the artifact is measured
  there, those two legs build their own, and the gate carries
  `ci-lint: allow-r1` with that reason. Their Linux and Windows legs consume the
  artifact.
- **`invariants` and `scan` remain two jobs (§2.4.1).** Merging them is a
  rewrite of the target-matrix workflow beside a change that already rewrites
  twelve; it is left for a change of its own.
- **A test that never ran was broken.** 741, one of the seven `llvm` tests,
  failed on its first run in CI. The root package of its fixture was a binary,
  so the build linked `cabi-probe.exe` for `x86_64-windows-gnu`, and with
  `allow_host_libs` the link found the host's mingw-w64 libraries. It passed on a
  machine that has them and failed on every runner, and nothing noticed, because
  no runner ran it. The fixture's root is now a library, which is what the test
  says it is: "this test compiles only".
- **Coverage found more than F9.** Classifying the tests of the 2026-10-01
  logs found 24 that ran on no runner and were named by no workflow. The
  seven `llvm` tests are among them, and so are three `musl` tests: the probe
  named 15.1.0 while the runners installed 16.1.0. Seven `mingw-cross` tests
  are in the list too, a toolchain no shard installed. The rest were 105 (nasm),
  65 (scan-deps), 239 (named by its `E2E_ONLY` pattern, which the check now
  reads), 257 (needs wine and a Linux-hosted MinGW), 658 (an attached Android
  device), and 873-877 (added after the logs). The capability probes now ask for
  a family. The Linux shards install musl, llvm, mingw-cross and nasm. 257 runs
  in `mingw-cross-wine`. 658 is the one entry of
  `tests/e2e/coverage-exceptions.tsv`.

## Part III. Defects repaired by the next release

### 3.1 #757: the engine's identity is part of a build record

1. `BuildCacheEntry` records the engine that wrote the graph: its version (`MCPP_VERSION`) and the
   canonical path of its executable (`self_exe_path()`, the same function the emitter uses). An entry
   without them declines once: "the recorded build predates the engine identity".
2. **One admission predicate.** The gates common to the three fast paths are one function: the engine
   identity, the record's version fields, the toolchain request, the runtime binding, the graph's
   existence, mode and request tag, and the freshness sweeps. `try_fast_build`,
   `try_fast_workspace_build` and `try_fast_run` call it and add only their own gates (run targets, a
   runner, the run tier, the selection). A field added later is then checked in one place, which is the
   structural repair of the shape §1.3 describes.
3. Criteria: an e2e test builds with an engine copied to one path, deletes it, and builds again with the
   same binary at another path; the second build regenerates the graph and succeeds, and no command in
   the new graph names the old path. A unit test of the predicate covers a recorded version that differs
   from the running one, and an entry that lacks the fields.

### 3.2 #756: each path dependency is classified by its own package

1. When the plan records the path-dependency roots (`src/build/prepare/plan.cpp:139-177`), it records with
   each root that package's own `module_extensions` and `device_extensions`, from its effective manifest.
2. `dep_sources_newer_than` classifies the files under each root with that root's table. The record's
   block is count-prefixed like `depSourceRoots=`, and an entry without the tables declines once.
3. Criteria: an e2e test with a provider that declares `.ixx` and a consumer that declares nothing.
   Editing the provider's host module re-runs the consumer's build program, the program's output
   changes, and no dead-entry warning is printed.

## Part IV. The mcpp toolchain specification (SPEC-009, as approved)

This part is normative. It is to be published as SPEC-009 in `docs/specs/`, in that directory's
language and format, with an implementation status on each rule. SPEC-006 continues to define what a
toolchain is, how one is named and selected, and what a payload must contain; SPEC-009 defines how the
set of supported toolchains changes over time. Terms follow RFC 2119.

### TS-1 Terms

| Term | Meaning |
|---|---|
| Family | `gcc`, `llvm`, `msvc`, `emsdk`, `android-ndk` (SPEC-006 §2.1) |
| Line | a family and a major version: GCC 16, LLVM 23 |
| Release | one upstream version of a line: 16.2.0, 23.1.3 |
| Payload | an installable tree for a release, a host and a variant: glibc `gcc`, native `musl-gcc`, cross `<arch>-linux-musl-gcc`, `mingw-gcc`, `mingw-cross-gcc`, `llvm` |
| Row | one row of the target matrix (`kKnownTargets`), or a host default |
| Default | the release a row resolves when nothing is declared |
| Line table | the one table in the engine that states every row's default (TS-3) |
| Self-host toolchain | the toolchains mcpp's own `mcpp.toml` declares |

### TS-2 Support tiers

| Tier | Which releases | Obligation |
|---|---|---|
| Default | the release the line table names for a row | the full e2e suite on that row's CI host; the build of mcpp itself where that host builds mcpp; release artefacts are built with it |
| Supported | the previous Default of each row, and releases the line table lists explicitly | the acceptance programs (SPEC-006 §6.2) in CI whenever the line table or one of its payloads changes; a regression is a defect |
| Available | every other release in the index | no CI. Reports are accepted. A known defect is stated as a capability of the payload (`.mcpp-toolchain.json`), never as a version branch in the engine |

Each family's floor, the oldest line whose `import std` mcpp accepts, MUST be stated in the line
table. Today the floor is implicit.

### TS-3 One line table

1. Every default and every row pin MUST be read from one table in the engine. For each row it states the
   family, the release, the variant, the tier, and, for a row whose release differs from its family's
   Default, the reason and the condition under which the difference ends.
2. Help text, install suggestions, error messages and `mcpp self env --format json` MUST be formatted
   from the table. A second literal of a default inside the engine is a defect.
3. Everything outside the engine that names a default (documentation, workflows, tests, examples and
   mcpp's own manifest) MUST either read it from `mcpp self env --format json` or be compared with it by
   a CI check. A literal that is neither read nor checked is a defect.
4. The index's `latest` is not a default. mcpp pins exact releases, and moving `latest` changes nothing
   mcpp builds.

### TS-4 A line moves as a whole

1. On one host, the rows of one family SHOULD resolve one release. A row MAY lag only with a reason in
   the line table, and the reason MUST be revisited at the next move.
2. A release enters the Default of a row only when every payload that row needs exists at that release,
   on both mirrors (TS-9). The payloads of one GCC line (glibc, native musl, cross musl, mingw) are one
   line for this purpose.

### TS-5 Provenance

1. A payload MUST derive from an upstream release: an official tag and its official source or binary
   archive, whose sha256 the payload's description records (SPEC-006 §4.5).
2. A payload MUST be produced by a recipe in a repository, run by a CI job or by a script a CI job could
   run. A manual step is not part of a recipe (SPEC-006 §5.1).
3. A patched payload, an upstream release plus upstream commits that no release carries yet, MAY be
   published only when all of the following hold:
   - every patch is a commit on the upstream release branch, or on its main branch with an open
     backport, cited by hash;
   - it is published as a revision of that release (`revision = N`, assets under `<release>-r<N>`), never
     under the official asset name, and its description lists the patches;
   - it is placed on the rows that need it and on no other;
   - it has an exit: when an upstream release carries the patches, the Default moves to that release,
     and the revision remains in the index only for those who pinned it.
4. The engine MUST NOT work around a toolchain defect by changing the platform's inputs (selecting an
   older SDK, rewriting SDK files, substituting another linker) unless that is the row's declared design.
   A toolchain defect is repaired by a toolchain release, or by a patched payload under rule 3.

### TS-6 What it means that mcpp supports a line

1. The engine MUST derive what a toolchain can do from the payload, not from its version: the standard
   library module from the library's manifest or layout, the scanner from the driver, flags from probes
   measured on the row. A comparison of version numbers is permitted only for a defect registered under
   TS-7, or in a language-feature table whose rows cite the release notes.
2. On every row where a line is Default, the engine MUST pass the acceptance programs (a program using
   `<memory>`, `<mutex>` and `<thread>`; `import std`; `import std.compat`), the e2e suite, and, where the
   host builds mcpp, the build of mcpp itself.
3. Every output that encodes a release MUST be reviewed at a move: the pack ABI tag (compiler major),
   BMI and cache identity (the version enters the fingerprint and every cache key, so a move needs no
   epoch change), and diagnostics that quote a version.

### TS-7 The compiler-defect register

1. Every engine behaviour, and every shape of mcpp's own sources, that exists because of a compiler
   defect MUST have an entry: family, first release observed, last release verified, upstream report,
   a minimal reproduction under `tests/` that fails while the defect exists, and the sites that depend
   on it.
2. At every move of a line, every reproduction runs against the new release, and the entry records the
   result.
3. A workaround is removed only when every Supported release is past the fix.

Entries known today, from source comments and earlier records: an instantiation dropped along a module
import chain (GCC 16.1); a `FILE`-typed entity in a module interface breaking a later
`#include <cstdio>` (GCC PR 99000, open); a nested `std::map` member truncating a BMI (GCC 16.1); a new,
widely imported module with standard types in its interface poisoning downstream BMIs (GCC 16.1); a
segmentation fault on a new interface unit (GCC 16.1, `src/build/prepare.cppm:37-50`); a miscompile with
a full BMI, which is why clang uses the two-phase reduced interface (clang 22.1.8,
`src/build/ninja_backend.cppm:1331`); a crash on an inline helper in a module purview (clang 20.1.7 on
Windows); the 27.0 SDK leaving `INFINITY` and `NAN` to `<float.h>` under modules
(`src/toolchain/hostflags.cppm:162-181`); the missing `directory_iterator` comparison in libc++ 20's
`std`; an ICE on `AMDGPUAsmParser.cpp` (GCC 16.1, the reason `llvm-dev` is built with GCC 15.1).

### TS-8 Host platform releases

1. A new operating system or SDK release that a row's users will meet gets a CI leg as soon as a hosted
   image exists, before that image becomes the runner default.
2. A leg that is red for an external cause carries `known_red: '#<issue>'`, the workflow assertion keeps
   the issue open, and the leg leaves the list when the issue closes.
3. The repair follows TS-5.

### TS-9 Mirrors

A version row MUST NOT reach the index before every asset it names is present on the GLOBAL and CN
mirrors and verified on both by a GET (status 200, byte size and sha256). An upload of more than about
8 MiB to the CN mirror is made from a host inside the CN network.

### TS-10 Moving a Default

1. The upstream release exists.
2. Its payloads are built by their recipes (TS-5) and pass the admission of SPEC-006 §6; each records its
   inputs.
3. The assets are on both mirrors and verified (TS-9).
4. The index gains the version rows; `latest` is unchanged.
5. The engine is verified on every row that will take the release as Default, and the release passes
   the gate below. The payloads MAY be named by path (SPEC-006 §2.2.1), so that this step does not wait
   for step 4.
6. One mcpp pull request moves the line table and every reader and checked copy of it; mcpp is
   released.
7. The index's `latest` moves after that release has shipped.
8. The tiers shift: the previous Default becomes Supported, and the line before it becomes Available.

A move is reverted by reverting step 6. Payloads and index rows stay.

**TS-10.5 The gate: a new release does not make the module experience worse.** A candidate release R is
compared with the row's current Default D on the same mcpp commit, on the same runner image, in the
same job, so that the comparison spans only the change of release. R passes when all six hold:

| Gate | Criterion |
|---|---|
| G1 | every e2e test that passes with D passes with R, and no test that runs with D is skipped with R |
| G2 | where the row builds mcpp, mcpp builds itself with R, and that binary passes the suite |
| G3 | the acceptance programs of TS-6.2 build and run |
| G4 | every TS-7 reproduction is run, and none of G1 to G3 needs a new workaround or a reshaped source; a defect that does is a failure of the gate unless the review accepts it with a register entry |
| G5 | the scanned module graph (each unit's provided and required modules) of mcpp's own sources and of the e2e module fixtures is identical under R and D |
| G6 | the cold and the warm build of mcpp and of the `bench/` projects take no more than 10 percent longer with R, as the median of three runs, and the BMIs are no more than 10 percent larger |

A release that fails the gate stays Available. The next release of the same line becomes the
candidate.

### TS-11 Machines that already have a default

1. A release that a user or a project states (`[toolchain]`, `--toolchain`, `MCPP_TOOLCHAIN`,
   `mcpp toolchain default`) MUST NOT move.
2. A default that mcpp wrote on a first run is not a statement by the user, and its record MUST say so.
   - On a home with no recorded default, a first run installs and records the line table's answer, as
     it does today, and states nothing more.
   - The notice exists for one case only: the home holds a default recorded by an earlier mcpp's first
     run, and the running mcpp's line table names a newer release for that row.
   - In that case mcpp MUST state once the recorded and the newer release, together with the two
     commands that settle the question: `mcpp toolchain default <newer>` moves the record, and
     `mcpp toolchain default --keep` keeps it. Either command turns the record into a statement by the
     user.
   - The notice does not block, does not prompt, and is not repeated. Until the user answers, the
     recorded release stays in use.
3. Payloads already installed stay; removing them is the user's decision.

### TS-12 mcpp builds with what it gives

mcpp's own manifest MUST use the Default release of each row it is built on. A divergence is a line-table
entry with a reason and an exit.

### TS-13 Conformance checks

The rules a program can check MUST be checked in CI:

| Check | Rule |
|---|---|
| C1 | no default literal in `src/` or `modules/` outside the line table (TS-3.2) |
| C2 | every documentation statement of a default, including the target-row tables of `docs/21` and the README, matches `mcpp self env --format json` (TS-3.3) |
| C3 | workflows, actions and tests take versions from the line table; a capability probe asks for a family, not a version (TS-3.3) |
| C4 | `mcpp.toml`'s `[toolchain]` equals the line table or a recorded divergence (TS-12) |
| C5 | a `known_red` leg names an open issue (TS-8; exists) |
| C6 | the TS-7 reproductions run on every Default row |
| C7 | the gate of TS-10.5 is one workflow that takes a row and a candidate payload, and the pull request of TS-10.6 cites its run |

## Part V. The toolchain candidates, kept for the move that follows this work

Nothing in this part is done in this work (T2). It states where each line would go and what each move
needs, so that a candidate that passes the gate (TS-10.5) can move without another design.

### 5.1 The line table if both candidates pass

| Row | Today | After | Payloads it needs |
|---|---|---|---|
| Linux x86_64 (host default) | `gcc@16.1.0` | `gcc@16.2.0` | `gcc` 16.2.0 |
| Linux, other architectures (host default) | `gcc@15.1.0-musl` | `gcc@16.2.0-musl` | `musl-gcc` 16.2.0 (native) |
| Windows without MSVC (host default) | `gcc@16.1.0` | `gcc@16.2.0` | `mingw-gcc` 16.2.0 |
| Windows with MSVC (host default) | `llvm@20.1.7` | `llvm@23.1.3` | `llvm` 23.1.3 windows |
| macOS (host default) | `llvm@20.1.7` | `llvm@23.1.3` | `llvm` 23.1.3 macosx-arm64 |
| `x86_64-linux-musl`, `aarch64-linux-musl`, `x86_64-windows-gnu` | `gcc@16.1.0` | `gcc@16.2.0` | `musl-gcc`, `aarch64-linux-musl-gcc`, `mingw-gcc`, `mingw-cross-gcc` 16.2.0 |
| `x86_64-windows-musl` | `llvm@22.1.8` | `llvm@23.1.3` | `llvm` 23.1.3 |
| bare-metal rows (12) and iOS rows (3) | `llvm@22.1.8` | `llvm@22.1.8`, lagging (T5) | picolibc sysroots carry compiler-rt builtins built with 22.1.8; iOS rows need `llvm.libcxx` and `llvm.compiler-rt-builtins` packages at 23 (`src/build/prepare/scan.cpp:631-632`) |
| mcpp's own `[toolchain]` | `gcc@16.1.0`, `macos = llvm@22.1.8`, `windows = llvm@20.1.7`, musl `gcc@16.1.0-musl` | the Default of each row | none beyond the above |

Not moved: `android-ndk` and `emsdk` rows; the `bench/` pins, which are fixed on purpose so that a
benchmark does not report a toolchain change as an engine change; fixtures that pin a release to test a
specific behaviour.

The move from 20.1.7 to 23.1.3 on macOS and Windows also removes finding 2 of §1.5: the macOS default
will be able to build mcpp.

### 5.2 GCC 16.2

**Upstream.** GCC 16.2.0 was released on 2026-08-07, a bug-fix release with 672 commits since 16.1.0.
The commit range does not touch the p1689 output, the module mapper or the driver options mcpp uses.
It repairs nine module defects (among them 124953, failed to load pendings; 124981, an undefined
reference for an instantiation streamed from a BMI; 125768 and 126209, ICEs). libstdc++ adds the symbol
version `GLIBCXX_3.4.36`.

**The risk that decides T6.** Bug 126577, a corrupt CMI when `-fmodule-mapper`, a re-exported partition,
a `chrono` alias and `unique_ptr` meet, is a regression of the 16 branch after 16.1.0, present in 16.2.0,
and retargeted to 16.3. mcpp passes `-fmodule-mapper` to GCC for every unit with a module scope
(`src/build/plan.cppm:3402`). The rule: if measurement M1 reproduces it, the GCC Default stays at
16.1.0 and 16.2.0 is Available, until a 16.3 release or a TS-5 revision carrying the fix.

**Payloads.**

| Payload | How it is produced this round |
|---|---|
| `gcc` 16.2.0 (glibc) | the `fromsource` recipe at 16.2.0, with the strip and the `specs` canonicalisation turned into a script (the musl builders' strip loop is the model), run by a builder workflow in `xlings-res/gcc` in the shape of `xlings-res/musl-gcc`'s; the description records the configure line, the source sha256 and the build's C library |
| `musl-gcc`, `aarch64-linux-musl-gcc` 16.2.0 | dispatch of the existing builder workflows with `gcc_ver=16.2.0` |
| `mingw-gcc` 16.2.0 | mirror of winlibs `16.2.0posix-14.0.0-ucrt-r2` |
| `mingw-cross-gcc` 16.2.0 | a builder workflow for the recipe that 16.1.0 followed by hand (GCC, binutils 2.44, mingw-w64 v12 CRT); if it is not ready, the `x86_64-windows-gnu` row lags under TS-4 |

Out of this round: `riscv64-linux-musl-gcc`, the Windows-host `x86_64-linux-musl-gcc`, `gcc-runtime`
(only 15.1.0 exists), and rebuilding `llvm-dev` and `libllvm` with 16.2 (they are built with 15.1
because of the 16.1 ICE in TS-7).

### 5.3 LLVM 23

**Upstream.** 23.1.0, 23.1.1 and 23.1.2 were released on 2026-08-25, 09-08 and 09-22; 23.1.3 is
scheduled for 2026-10-06 and is expected to carry the `arm64e.x1` repair, which reached
`release/23.x` after the 23.1.2 tag. Release archive names and layouts are unchanged from 22.1.8; 23
adds `.tar.zst` siblings and `.msi` installers. The Windows archive carries no
libc++ and no `std` module, as before, so `import std` on that row continues to come from the MSVC
STL.

**What changes for a build tool**, from the 23 release notes and the sources:

- P1857R3 changes how `module` and `import` directives are lexed during dependency discovery. The
  scanner's output must be measured (M2, §5.5).
- `__has_feature(modules)` is no longer true with only `-std=c++20`. The SDK 27 `INFINITY`/`NAN`
  workaround may therefore become unnecessary on 23; it stays harmless, because its values equal the
  SDK's, and its TS-7 entry is re-measured.
- `export` inside a module implementation partition is now rejected.
- libc++ removed many transitive includes, and `std.cppm` now includes `<text_encoding>` and
  `<atomic>`.
- The libc++ `[abi:...]` tag follows `_LIBCPP_VERSION` and changes with every patch release. The
  fingerprint and the cache keys already contain the version.
- Reduced BMIs became the default in 22. `-fmodule-output` and `-fmodules-reduced-bmi` remain.
  `GetStdModuleManifestPath` is unchanged between 22.1.8 and 23.1.2.

**Payloads.** The carve script (`xim-pkgindex/.agents/tools/build-llvm-subpkg.sh`) applies unchanged to
the three official archives. The Linux carve still injects `libatomic` from a GCC on the build host
(23.1.2's `libc++.so.1` still names `libatomic.so.1`, and the archive carries none). That GCC should be
16.2 once its payload exists. `llvm-tools` and `mcpp-vscode-clangd` take the same version. The index's
parity test requires the new version on the linux, macosx and windows sections together. The existing
22.1.8 and 20.1.7 rows stay, because index tests name their assets and pinned users rely on them.

### 5.4 The macOS 27 link

The repair is the official 23.1.3 (TS-5.4, T3). Four days separate this record from its scheduled tag,
and the `xcode-27` legs are known red without blocking anything. Every LLVM row therefore moves to
23.1.3 when 23.1.3 passes the gate; the 23.1.2 measurement only shows early whether the line is likely
to pass.

A patched payload is available under TS-5.3 if it is asked for: `llvm@23.1.2` revision 1 for the macOS
row. It would be the official 23.1.2 archive carved as usual, with `ld64.lld` and `llvm-otool` rebuilt
from `release/23.x` at a recorded commit containing `532fa5afbe2b` and `ee66426152f9`, published as
`23.1.2-r1` assets whose description names the commits. It is not part of the plan unless 23.1.3 is
delayed and macOS 27 users must be served before it.

Rejected alternatives: selecting the Command Line Tools 26.5 SDK, rewriting the SDK's `.tbd` files,
and linking with Apple's `ld` instead of `ld64.lld`. Each changes the platform's inputs to hide a
toolchain defect (TS-5.4), and each would survive the repair as a second behaviour.

When both `xcode-27` legs are green with the new release, #669 closes and the `known_red` entries leave
the workflows (TS-8.2).

### 5.5 Measurements that start the move

Each one decides something. They run when the first move begins, not in this work.

| # | Measurement | Decides |
|---|---|---|
| M1 | mcpp built by itself, and its e2e suite, with GCC 16.2.0 on Linux x86_64; the 126577 reproducer | T6 |
| M2 | the same with the official LLVM 23.1.x on Linux x86_64, named by path: scanning, partitions, transitive includes | the engine changes of §5.3 |
| M3 | the same on Windows with LLVM 23.1.x and the MSVC STL | the Windows row |
| M4 | macOS 15 with 23.1.x; `xcode-27` with 23.1.3 | T3, #669 |
| M5 | every TS-7 reproduction against the candidates | the register's entries |
| M6 | a program referencing a `GLIBCXX_3.4.36` symbol, built with 16.2.0 and packed in each pack mode, run on a clean container | the runtime closure of the GCC row |
| M7 | a bare-metal row and an iOS row with the 23 compiler and their 22.1.8 companion packages | whether T5 can be shortened |

## Part VI. Order of the work

Each item is one pull request unless stated otherwise. CI changes need no mcpp release; the engine
repairs do.

| # | Item | Contents | Depends on |
|---|---|---|---|
| 1 | SPEC-009 | Part IV in `docs/specs/`, in that directory's language and format, with an implementation status on each rule (most are "not implemented"); SPEC-006 §7 becomes a reference to it | none |
| 2 | Engine repairs | Part III; released as the next mcpp version | none |
| 3 | CI, low-risk rules | D4 (cache writers, `target/` removal, concurrency on `main`, the Wine archive), D6 (the `llvm` capability and the coverage job), D7 (known-red legs off pull requests), and the macOS step limit of T8 | none |
| 4 | CI, the build stage | D1, D2 and D3: `ci.yml`, the build matrix with packed artifacts, the reusable workflows, path classes | item 3, so that its measurements start from the new cache policy |
| 5 | CI, shards | D5: duration-based shards and their limits | item 4 |
| 6 | Measurement | §2.6, over five consecutive code pull requests | item 5 |
| 7 | External lane | §2.5, only if the rule of §2.5.5 holds | item 6 |

Items 1, 2 and 3 run in parallel.

The first application of SPEC-009 (Part V) is not in this list. It starts when LLVM 23.1.3 is tagged, under
a record of its own, and it begins with the measurements of §5.5 and the single line table (D10).

## Part VII. Not yet measured

- **Why a restored `target/` does not make a build incremental (F2).** Checkout modification times are the
  likely cause, and are not verified. The answer decides whether `target/` caching can return, and in what
  form.
- **Whether every consumer works with the packed artifact (§2.4.1).** The Windows hand-off proves it for
  one job. The e2e suite on Linux and macOS with a packed `$MCPP` is the measurement, run once in a
  validation pull request before item 4.
- **Why the target-matrix and openkal legs build with `--dev`.** Nothing records it.
- **The minimum macOS version of an mcpp built on `macos-15`.** It decides whether the `macos-14` legs can
  consume it.
- **Why about 15 e2e tests take 39 to 104 seconds on Windows and about one second on Linux.**
- **The cost of the e2e-coverage job's lists**, and whether any host-specific skip is missing from them.
- **Toolchains.**
  - Whether mcpp builds itself with GCC 16.2.0 and with LLVM 23 (§5.5, M1 to M3).
  - The `libstdc++.modules.json` relative path in a 16.2.0 install.
  - Whether the clang 23 driver changes a default that the generated `clang++.cfg` relies on.
  - Whether the build-program cache and the host-module store key the engine version as well as the
    compiler. If they do not, §3.1 extends to them.

## Part VIII. Questions for review

1. One top-level `ci.yml` with reusable workflows (recommended), or each platform workflow with a build job
   of its own and artifacts shared only within it.
2. The artifact as the packed self-host build (recommended), or the raw binary next to a restored
   sandbox.
3. Removing the `target/` caches now (recommended), or keeping them until F2's cause is measured.
4. The documentation classes of §2.4.3, with the second class derived by searching `tests/` and
   `.github/tools/` for the changed path.
5. The shard counts of §2.4.5 (Linux 3, Windows 3, macOS 2), and step limits at twice the budget.
6. The known-red legs on pull requests only with the label `macos-27` (recommended).
7. The external lane: designed now and built under §2.5.5 (recommended), or built now. If it is built, the
   repository name `speak-agent/mcpp-ci-lane`.
8. The single line table (TS-3) deferred to the first toolchain move (recommended), or built in item 2.
