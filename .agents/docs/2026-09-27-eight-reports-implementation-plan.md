---
subject: plan
status: active
---

# Eight reports after 2026.9.27.1: implementation plan

This record implements `2026-09-27-eight-reports-by-home-and-one-optimisation-plan.md`
(the design, revision 3, all decisions settled). The design fixes what is built. This
record fixes the following:

- the order;
- which files each task owns;
- the repositories involved and the order of their releases;
- how each step is verified.

## 1. Readings that shaped the plan

- **xlings emits no progress events for an index sync (measured).** The command is
  `xlings interface update_packages --args '{}'` (xlings 2026.9.27.1).
  - It emits heartbeats and one result, and no progress event.
  - It also writes its terminal progress text (`[1/7] awesome::xim.lua` followed by
    an erase sequence) onto the NDJSON stream. That text is not JSON, and the
    xlings interface protocol (`docs/spec/interface-ndjson-v1.md`) does not allow
    it.
  - W11 therefore needs an xlings change and an xlings release before the mcpp
    release.
- **mcpp-language-server.** speak-agent has read access only, so its S1 change is
  proposed from a fork.
- **The next e2e number is 805.** Unit tests live in `tests/unit/`.

## 2. Repositories and their order

| Order | Repository | Pull request | Content | Release |
|---|---|---|---|---|
| 1 | openxlings/xlings | one | interface mode emits `progress` events for an index sync and keeps terminal text off the NDJSON stream | yes; the date version of the day |
| 2 | Sunrisepeak/mcpp-language-server | one, from a fork | S1: the generated-output record (design §4.4, D6) | no, a specification only |
| 3 | mcpp-community/mcpp | one | W1 to W12, docs, specs, CHANGELOG, version, xlings pin | yes |
| 4 | openxlings/xim-pkgindex | the bot's bump pull request | mcpp's new version | merged by a maintainer account |
| 5 | mcpplibs/mcpp-index | one, if its CI pin or `latest_mcpp` must move | index consumer pins | no release; the index publishes on merge |

The mcpp pull request pins the xlings release of row 1 (`kXlingsVersion`), and the
release pull request carries that pin.

## 3. Tasks, owners and dependencies

The work uses one integration branch, `feat/eight-reports`, in the worktree
`mcpp-eight`. Each task has its own worktree, branched from the integration
branch, and is merged back when its criteria pass.

| Task | Steps | Files owned (smallest hunks elsewhere) | Depends on |
|---|---|---|---|
| T1 | W1 | `src/build/prepare/manifest.cpp`, `src/project.cppm` (member resolution), `src/cli.cppm` (`-p` help), `docs/07` (en, zh), SPEC-004 §9, `tests/unit/test_workspace_inheritance.cpp`, e2e 805 and 806 | none |
| T2 | W2 | `src/build/prepare/features.cpp` (host-module unit order), e2e 807 | none |
| T3 | W3, W4, W5 | `src/build/plan.cppm` (the unit loop only), `src/build/prepare/target_side.cpp` (the device-source check), `src/build/prepare/driver.cpp`, `src/build/prepare/xlings.cpp` (the project index file), `src/cli/cmd_build.cppm` (the emit failure path), SPEC-005, e2e 688 extended, e2e 808 and 809 | none |
| T4 | W6 | `src/build/plan.cppm` (`add_deploy` only), `src/build/stage.cppm`, `src/cli/cmd_build.cppm` (`cmd_stage` only), `src/build/ninja_backend.cppm` (the stage and `place_dlls` edges), `src/pack/pack.cppm` (`place_runtime_dlls`), SPEC-007 R4.2 and R4.3, e2e 810 and 811 | none |
| T5 | W12 | `src/pm/package_fetcher.cppm`, `src/pm/index_contract.cppm`, `src/xlings/xlings.cppm` (`update_index` reporting), `src/ui.cppm` (closing notices), `src/doctor.cppm`, `docs/09` and `docs/50`, e2e 185 updated, e2e 812 | none |
| T6 | W7 | `src/build/prepare/*.cpp` (phase functions), `.github/tools/` (the size gate), `tests/unit/test_prepare_helpers.cpp` | T1 to T5 merged |
| T7 | W8 | `modules/manifest/src/toml.cppm`, `modules/manifest/src/types.cppm`, `src/build/prepare/scan.cpp` and `target_side.cpp` (dialect resolution), `src/build/prepare_inputs.cppm`, SPEC-004 §3.1 and §9, e2e 813 | T6 |
| T8 | W9 | `modules/toolchain-model/src/dialect.cppm`, `src/build/flags.cppm`, `src/build/prepare/scan.cpp` (std-module CRT), `src/build/distribution.cppm`, the toolchain redistributable field (`src/toolchain/msvc.cppm`, the LLVM row's sysroot resolution), `src/pack/pack.cppm` (contract), `docs/20` and `docs/04`, unit tests, e2e 814 (Windows) | T6, T7 |
| T9 | W10 | `src/build/build_database.cppm`, SPEC-005 §3, e2e 815 | T3, T6; the S1 text |
| T10 | W11 | `src/ui.cppm` (terminal and non-terminal rendering), `src/xlings/xlings.cppm` (index refresh through the interface), the git fetch in `src/build/prepare/fetch.cpp` and `graph.cpp`, the sandbox bootstrap, `docs/09`, unit tests, e2e 816 | T5, T6; the xlings release |
| X1 | xlings | `openxlings/xlings`: the interface event stream for `update_packages` | none |
| L1 | mcppls | `docs/specs` S1 addition | none |

T1 to T5, X1 and L1 have no dependency on one another. At most three subagents run
at once. The author takes T2 and the merges, and runs the integration build and
the full test suites.

**Rules for parallel work.** These come from the 2026-09-12 and 2026-09-26 records.

- **No global configuration change.** No task changes `~/.mcpp/config.toml` or any
  other global configuration. A toolchain is selected per fixture or per command.
- **No broad `pkill -f`.** No task kills processes by a broad `pkill -f` pattern.
- **One build per worktree.** No two builds run in one worktree at once.
- **Clean up after merging.** A merged task's `target/` is removed.

## 4. Verification

**Per task.**

- The fresh binary passes `mcpp test` and the task's own e2e scripts.
- Each new criterion is also run with the fix removed, and must then fail.

**Integration.**

- A full `mcpp test`.
- The e2e suite on Linux, through `tests/e2e/run_all.sh` with the fresh binary.
- The golden fixtures of the #719 decomposition.
- CI on every platform through the one pull request.

**After the release.**

- **A sandbox.** `xlings subos new eight`, then `xlings subos use eight --sandbox
  --cmd ...`, with both mcpp and xlings on the CN mirror. The sandbox installs the
  released mcpp by its release path and runs one probe per step. The probe is
  passed in as base64, and each probe directory is removed at the start of its
  section.
- **A control.** The same script runs against 2026.9.27.1, where exactly the fixed
  criteria must fail.
- **The index ecosystem.** mcpp-index's validation sweep runs against the new
  release.

## 5. Release

The version is the date version of the release day. The xlings pin moves to the
xlings release of row 1.

1. After the release workflow starts, every archive and its sidecar are uploaded
   to GitCode with the local `gtc` as soon as each appears on the GitHub release.
2. Each GitCode asset is verified by a GET with a byte comparison.
3. The xim-pkgindex bump pull request is merged with the maintainer account, and
   its state is read back afterwards.
4. The release is complete when `pkgs/m/mcpp.lua` on the index's `main` has
   `latest` pointing at the release.
