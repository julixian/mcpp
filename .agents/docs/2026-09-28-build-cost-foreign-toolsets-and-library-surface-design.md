---
subject: design
status: active
---

# Build cost, foreign toolsets, the build-plugin architecture and the library surface: engine, plugin and index design (#734)

**Status:** active, revision 5 (2026-09-28). The design is settled (§11); §13
divides it into tasks per repository, with their dependencies and criteria, and
records their implementation.

- **Revision 1** proposed engine items E1 to E6 and plugin items P1 to P5.
- **Revision 2** records the first review:
  - D1 (keyed sub-build) and D2 (general build information) are settled.
  - E6 is settled as warnings only.
  - It designs the library interface specification and the plugin implementation.
- **Revision 3** records the Windows readings of §2.2. E2b is confirmed, E4 is
  adopted, and P1's default is designed again as `resolved`, with a mechanism
  chosen by the toolset's origin.
- **Revision 5** settles D11 and D16 and adds §13: the tasks, their order across
  the three repositories and the validation project, and the record of their
  implementation.
- **Revision 4** records three rounds of discussion and a global review (§12):
  - **The build-plugin architecture (§3).** It has three layers: `mcpp.core`
    (engine), `mcpp.plugins` (official general library) and plugins (official
    families and third parties). `import mcpp` and `import mcpp.core` are
    permanently equivalent.
  - **Module naming (§3.5).** It extends the engine's existing reserved-prefix
    warning and becomes an admission rule of mcpp-index.
  - **A per-package engine floor (§3.6).** `[package] mcpp = ">=V"`.
  - **Compatibility units (§3.7).** They carry a six-month retirement date.
    D3 is settled on that basis: `detected` moves to a compatibility unit.
  - **L2 behind one feature.** The general library is reached through one
    feature, `plugins-core`, which every family feature implies. This replaces
    revision 3's E7 (a `[host-module]` table), which is withdrawn. The new E7 is a
    general improvement: a missing build-program module names the feature that
    provides it.
  - **Corrections found by the global review.** Among them: an older engine does
    **not** refuse an unknown key in `[lib]`, `[build]` or `[package]` (measured),
    so revision 2's compatibility argument is corrected. `[lib]` accepts an
    unknown key without any warning, which is a defect (E12).

**Input.** The Windows CI of Sunrisepeak/GalTranslPP#3 (runs 36324593343,
36378870254 and 36396398845; windows-2025, 4 vCPU, mcpp 2026.9.28.2, mcpp:plugins
0.16.0), the sources of mcpp at `dbf71941`, of mcpp-plugins at `8e0362d`, of
vcpkg-tool and of mcpp-index at `e9b80c5`, vcpkg's documentation of triplets and
binary caching, local measurements with mcpp 2026.9.28.2, and the Windows readings
of §2.2.

## 0. Scope and the rule that selects the items

GalTranslPP is a validation project: a five-member workspace on Windows with a core
library of 76 translation units, a Qt GUI, 22 vcpkg ports and one CMake project.
It is evidence, not a requirement.

An item enters this design only if its need survives the removal of that project,
that is, only if it can be stated for every workspace, every foreign build system
or every platform. The official plugins are general plugins in the same sense.
Their behaviour is chosen through options, and nothing in them names a project.
Project-specific items are listed in §9 with the reason they stay with the project.

## 1. Principles

1. **The engine provides general mechanisms only.** It states facts about the
   resolved build in a form that belongs to no foreign build system, and it never
   learns CMake, vcpkg or Qt.
2. **A plugin behaviour is an option set from `build.mcpp`, and its default is
   the best design that readings support.** A default that differs from the
   previous release is adopted only when readings show which builds it changes.
   Each such build must either gain consistency (the same toolset, the same
   runtime) at the cost of one rebuild, or stop with a named error where the
   previous release produced an inconsistent result. The previous behaviour stays
   available for six months in a compatibility unit (§3.7).
3. **A check is made by the reader of the property it protects.** A warning that
   no step reads is noise. A step that reads a property and does not check it
   produces silently wrong output.
4. **No silent wrong output.** Where the engine cannot do what was asked, it says
   so. An explicit statement that cannot be met is an error; an undeclared
   default may fall back, and says so once.
5. **Every change states its upgrade cost.** One re-run of the build programs, one
   recompilation of path dependencies or one rebuild of vcpkg ports is acceptable
   when the changelog states it. A changed result is not acceptable.
6. **One authority per fact.** Which toolset builds a graph is decided by mcpp's
   toolchain selection alone. A plugin does not keep a second answer.
   Equivalent spellings of one name (`mcpp` and `mcpp.core`; `host-module`) are
   permanent, and neither is deprecated.

## 2. Readings

### 2.1 From the validation project's CI and from the sources

| Reading | Value | Source |
|---|---|---|
| `mcpp build --workspace`, vcpkg binaries cached | 1726 s | run 36378870254 |
| of which: member `GalTranslPP` (core) | 285 s | same |
| of which: `GPPCLI`, including a second compilation of core | 315 s | same |
| of which: `GPPGUI`, including a third compilation of core | 1039 s | same |
| core's 76 compile commands in the three positions | identical except the output directory (76/76) | `mcpp emit build-database`, same run |
| `mcpp build --workspace`, no vcpkg cache | 69.7 min, of which about 43 min are the 22 ports | run 36324593343 |
| `mcpp pack --format release`, two members | 168 s, of which about 70 s are 2675 single-file copy actions | run 36378870254 |
| files deployed beside each program by `mcpp build` | about 1270 per program, each one `stage_file` edge | `release-files.txt`, run 36396398845; `ninja_backend.cppm` |
| a no-op `mcpp run -p GPPCLI` on Windows | about 3 s; the project fast path is not taken | run 36378870254 |
| build systems of the 22 ports at baseline `ee6a47d` | 18 CMake, 3 header-only, 1 make under msys (icu); none MSBuild | the ports' `portfile.cmake` |
| `mcpp pack` of a library exporting `Alpha` and `Beta`, no lib root | exit 0, "Interface (headers only)", "Withheld (nothing)", `sources = []` | local, mcpp 2026.9.28.2 |
| mcpp-plugins with `deps-*` among its default features | its own build fails: `deps/deps.cppm: module 'mcpp' not found` (and the same for `src/declare.cppm`) | local copy of mcpp-plugins `8e0362d`, mcpp 2026.9.28.2 |
| the build program's compile with the `deps` family added | 0.92 s without, 1.56 s with (+0.66 s), two runs each | local, Linux |
| what the `deps-*` features provision | `deps-vcpkg`: `xim:vcpkg`; `deps-cmake` and `deps-archive`: `xim:cmake` (provisioned before the build program runs) | mcpp-plugins `mcpp.toml` |
| vcpkg under a chain-loaded triplet without `VCPKG_LOAD_VCVARS_ENV` | the toolset is `"external"`; `VCPKG_PLATFORM_TOOLSET=external` | vcpkg-tool `get_toolset`, `commands.build.cpp` |
| vcpkg's build environment | a clean environment whose `PATH` holds only system directories; the caller's `PATH` survives only through `VCPKG_KEEP_ENV_VARS=PATH`; `VCPKG_VISUAL_STUDIO_PATH` in the environment selects the instance | vcpkg-tool `system.process.cpp`, `vcpkgpaths.cpp` |
| ports built with MSBuild in vcpkg's registry | 31, among them `libsodium`, `libusb`, `python3`, `libvpx`, `mp3lame` | `vcpkg_msbuild_install` / `vcpkg_install_msbuild` in `ports/` |
| what a host-module edge offers a build program | the lib root and the units of enabled features only: a consumer without features that imports `mcpp.deps` is told `importable here: mcpp.plugins` | local, mcpp 2026.9.28.2 |
| a unit that imports `mcpp` in an ordinary `[build] sources` list | the ordinary build fails: `module 'mcpp' not found` (a unit also listed by a disabled feature is left out) | local, mcpp 2026.9.28.2 |
| an unknown key in `[package]`, `[build]`, `[lib]`, a dependency spec | `[package]`, `[build]`: warned and ignored; `[lib]`: ignored without a message; dependency spec: warned and ignored | local, mcpp 2026.9.28.2 |
| the engine's reserved module prefix | `kReservedModulePrefix = "mcpp."`: a build rule's module under it from a package outside namespace `mcpp` is warned about | `modules/buildmcpp/src/provisions.cppm` |
| namespaces in mcpp-index | 20; the most used are `compat` (152 packages), `mcpplibs` (32), `freedesktop` (18); none equals `core`, `plugins`, `deps`, `rules`, `dist` or `tools` | mcpp-index `e9b80c5` |

### 2.2 Windows readings taken for this design

Runs 36405747279, 36406515321, 36407317373 and 36408145450 on windows-2025 (Visual Studio 18,
MSVC 14.51.36231; vcpkg 2026-07-27). Each leg is listed with the fault it could
have shown, so that a success is not read as a leg that never ran.

| Id | Leg | Result |
|---|---|---|
| M1 | zlib installed twice; the second time with `VCPKG_VISUAL_STUDIO_PATH` naming the same instance, and a fresh install root | same `vcpkg_abi_info` hash (`80eb9d4a…`); the second install restored 3 packages from the binary cache and built nothing |
| M2a | control: libusb (MSBuild) under `x64-windows` | builds |
| M2b | zlib (CMake) under a chain-loaded triplet naming the instance's `cl.exe`, without vcvars | builds |
| M2c | libusb (MSBuild) under the same triplet | **fails**: `msbuild ... /p:PlatformToolset=external` |
| M2d | libusb and zlib under a chain-loaded triplet with `VCPKG_LOAD_VCVARS_ENV ON` | both build |
| M3a | Visual Studio masked, the managed `msvc@14.44.35207` named by a chain-loaded triplet; zlib (CMake) | builds |
| M3b | the same; icu (make under msys) | **fails**: `configure: error: link.exe is not a valid linker. Your PATH is incorrect.` |
| M3c | the same; icu with the toolset's `bin` first on a `PATH` kept through `VCPKG_KEEP_ENV_VARS=PATH` | builds |
| P3 | the managed toolset copied to another path, with the environment pointing at the copy | zlib restored 3 packages from the cache: the ABI hash does not depend on the toolset's path |
| E2b-1 | mcpp 2026.9.28.2, plugins 0.16.0, fmt; `toolchain-coupled` with `x64-windows` and with `x64-windows-static-md` | both build and run |
| E2b-2 | `self-contained` with `x64-windows-static-md` | **fails**: `lld-link: error: /failifmismatch: mismatch detected for 'RuntimeLibrary'` |
| E2b-3 | `self-contained` with `x64-windows` (fmt as a DLL) | builds and runs: two C++ runtimes in one process, and nothing says so |
| E4 | a first `mcpp build` deploying 1270 files, twice | 1270 `stage_file` edges; 4.5 s from the first to the last edge (26.8 s of edge time, four at a time); the build 6.3 s against 1.3 s without them. One process copying the same files: 0.5 s |

Three harness faults were found and corrected while taking these readings. Each is a
fact the plugin design must respect, and §5 does:

- a toolchain file that writes a Windows path from the environment into
  `CMAKE_MT` fails (`Invalid character escape '\P'`), so paths go through
  `file(TO_CMAKE_PATH)`;
- the host triplet must be the derived triplet as well, or vcpkg builds its host
  ports (`vcpkg-cmake`) with the standard triplet and looks for Visual Studio;
- the SDK's header is `Windows.h`, which a case-sensitive search misses.



## 3. The build-plugin architecture

### 3.1 Three layers

| Layer | Module names | Provided by | Contents | Stability and specification | How a build program reaches it |
|---|---|---|---|---|---|
| **L1 core** | `mcpp.core`; `mcpp` is a permanent equivalent spelling | the engine: embedded in the binary, versioned by the build-program protocol (13 today) | mechanisms only: **build information** (target, toolchain and its tools, environment, contracts, package identity, directories, dependency facts) and **control** (directives, actions, placement, pack formats, diagnostics) | SPEC-007, each symbol with the protocol that introduced it; additive; a removal only after a deprecation period | `import mcpp.core;` (or `import mcpp;`) |
| **L2 official general library** | `mcpp.plugins` (facade), `mcpp.plugins.declare`, `mcpp.plugins.toolset`, `mcpp.plugins.fs`; `mcpp.plugins.testing` separately | mcpp-plugins | general building blocks written against L1 only; no knowledge of a particular foreign tool | the package's version; breaking changes behind compatibility units for six months | the feature `plugins-core` (implied by every family feature); `plugins-testing` for the test kit |
| **L3 plugins** | official families `mcpp.deps.*`, `mcpp.rules.*`, `mcpp.dist.*`, `mcpp.tools.*`; third parties `mcpp.<namespace>.*` | mcpp-plugins; any package | concrete plugins built on L1, or on L1 and L2 | their packages' versions | official families by feature (code and payload together); third parties by dependency |

The layers depend downward only: L3 on L2 or L1, L2 on L1. L1 knows neither.

### 3.2 L1: `mcpp.core`

- **Content.**
  - **Build information.** The accessors of today, plus E2's (§4.2).
  - **Control.** The directives, actions and pack formats of today, plus
    batched placement (E4, §4.5) and structured diagnostics (E11, §4.12).
- **Names.** The engine embeds two module units:
  - `mcpp.core`, which holds the whole interface;
  - `mcpp`, whose only content is `export import mcpp.core;`.

  Both export the same symbols, so they cannot diverge. SPEC-007 and the
  documentation use the name `mcpp.core`, and state that the two spellings are
  equivalent. The C++ namespace stays `mcpp::`, so no code changes.
- **Specification.** SPEC-007 gains a section "`mcpp.core`". It lists every
  accessor and directive with the protocol that introduced it and its availability
  per row. It states the stability policy: additions only; a symbol is removed only
  after it has been deprecated for six months; a changed meaning is a new symbol.
  It also gives the table mapping mcpp releases to protocol numbers, which is the
  only place a protocol number appears for a user.

### 3.3 L2: `mcpp.plugins`

| Module | Contents | Source today |
|---|---|---|
| `mcpp.plugins` | the facade and the std-only surface shared with `mcpp-embed` | `src/plugins.cppm` (unchanged) |
| `mcpp.plugins.declare` | the build-program half of the surface | `src/declare.cppm` (unchanged) |
| `mcpp.plugins.toolset` | the translation of E2's facts for foreign build systems (§6.2) | new; revision 3 called it `mcpp.deps.toolset` |
| `mcpp.plugins.fs` | deterministic file generation (`write_if_changed`) and placement helpers | moved from `mcpp.deps` |
| `mcpp.plugins.testing` | the test kit for plugins (P6, §6.8) | new |

- **Why L2 sits behind a feature.** The plugins package also has an ordinary build.
  It produces `mcpp-embed`, and docs/05 §2.14 places that tool in the same package
  as the rules on purpose, so that the tool and the rules are one version. A unit
  that imports `mcpp.core` cannot enter that ordinary build: its source list fails
  with `module 'mcpp' not found` (§2.1). A host-module edge offers a build program
  only the lib root and the units of enabled features (§2.1). Today's `surface`
  feature exists for exactly this reason.
- **The feature.** `surface` is therefore renamed `plugins-core` and documented as
  the entry to L2. Every family feature implies it. The old name stays as an
  alias in a compatibility unit.
- **Alternatives considered and rejected.**
  - Deriving from a unit's imports which build it belongs to.
  - A separate table for build-program units.

  Both change the engine's build semantics for the sake of one package (§12,
  finding 1).
- **Usage.** A project that uses any official plugin writes nothing more than
  today. A build program that uses only L2, and a third-party plugin built on L2,
  add `features = ["plugins-core"]`.

### 3.4 L3: plugins

- **Official families.** Each family is enabled by feature. A family feature
  enables the code and provisions the family's payloads, which is why it is a
  feature: provisioning runs before the build program, so it cannot follow use.
  - `deps-vcpkg` provisions `xim:vcpkg`;
  - `deps-cmake` and `deps-archive` provision `xim:cmake`;
  - `rules-*` also claim source extensions;
  - `dist-*` provision packaging tools per target;
  - `tools-*` provision none.

  `mcpp.deps` remains the deps family's own base module (prefix types, placement
  of prefix files).
- **Third parties.** A third-party plugin is a package whose modules import
  `mcpp.core`. It depends on `mcpp.plugins` with `host-module = true`,
  `features = ["plugins-core"]` and, when its consumers' build programs import L2
  directly, `reexport = true`. It names its modules by the rule of §3.5.

### 3.5 Module names (the engine's warning, and the index's admission rule)

| Module name | Who may provide it |
|---|---|
| `mcpp`, `mcpp.core` | the engine only |
| `mcpp.plugins.*`, `mcpp.deps.*`, `mcpp.rules.*`, `mcpp.dist.*`, `mcpp.tools.*` | packages in namespace `mcpp`; the reserved second segments are listed in SPEC-007 and may be extended |
| `mcpp.<namespace>.*`, for example `mcpp.acme.protobuf`, `mcpp.mcpplibs.capi.lua` | packages in namespace `<namespace>` |
| any other module of a plugin | the library rule of SPEC-008 I3: `<namespace>.<name>.*` |

The rule gives three things:

- A third-party plugin is recognisable as an mcpp plugin by its name.
- Third parties cannot collide with each other, because index namespaces are
  unique.
- The official families cannot be impersonated, because a reserved second segment
  cannot be registered as a namespace. None of them is one today: mcpp-index
  `e9b80c5` has 20 namespaces, and `compat`, `mcpplibs` and `freedesktop` are the
  most used.

**Enforcement.**

- **The engine.** It already warns when a build rule's module starts with `mcpp.`
  and its package is not in namespace `mcpp` (`kReservedModulePrefix`,
  `provisions.cppm`). E10 extends that warning to every module of a
  build-program surface and accepts `mcpp.<own namespace>.*` (§4.11). The engine
  cannot tell who is official (a fork or a private mirror is legitimate), so this
  stays a warning.
- **The index.** mcpp-index makes it an admission rule (I1, §7).

### 3.6 A package's engine floor: `[package] mcpp = ">=V"`

**Three versions.**

- The **mcpp release** (`2026.9.28.2`) is what a user pins in `.xlings.json` and
  reads in a message.
- The **`mcpp.core` protocol** (13) is stated in SPEC-007 only.
- The **manifest vocabulary** grows with releases.

A user sees only the first.

**Pin and floor answer different questions.**

- **Pin.** Which mcpp this project uses: `.xlings.json`, installed by
  `xlings install`, unchanged.
- **Floor.** Which engines this package supports: `[package] mcpp = ">=V"` in
  `mcpp.toml`, inheritable through `[workspace.package]`. Cargo's
  `package.rust-version` is the precedent.

**Rules.**

- **Only `>=` is accepted.** A bare version is exact everywhere else in mcpp, so a
  bare version here is refused with a hint to write `>=`.
- **An engine below the floor stops.** It names the package, the floor, its own
  version and the way to upgrade (`.xlings.json`, or `xlings install mcpp@…`).
- **An older engine warns and continues.** 2026.9.28.2 says `[package] has
  unsupported key 'mcpp' (ignored)` (§2.1), so an older client can still load the
  package.
- **Relation to the protocol.** A plugin that needs protocol 14 declares the first
  release that carries it. No separate protocol key exists.

**Follow-up, outside this design.** An index descriptor may carry the floor, so
that a resolver skips versions its engine cannot build (as Cargo resolves by
`rust-version`).

### 3.7 Compatibility units

The convention is the same for the engine and for the plugins.

- **Place.** Compatibility code lives in a `compat/` directory, one unit per kept
  behaviour.
- **Header.** Each unit's header states five things: what it keeps, since which
  release, its retirement date (six months after its introduction), what replaces
  it, and the note it prints.
- **The note.** It is printed once per build when the unit's behaviour is used.
- **Retirement.** A CI check lists the units past their date and fails, so that a
  deferral retires itself.

**The first units** (retirement 2027-03-28):

| Unit | Keeps | Replacement |
|---|---|---|
| `deps/compat/detected_toolset.cppm` | `toolset = detected` for deps-vcpkg and deps-cmake (D3) | the engine's toolchain selection: `msvc@system` makes `resolved` use that instance |
| `deps/compat/program_compilers.cppm` | the name `mcpp::deps::program_compilers` | `mcpp::plugins::toolset::resolve` |
| the `surface` feature alias | the old name of `plugins-core` | `plugins-core` |
| `mcpp.deps` re-exports of `write_if_changed` and the placement helpers | the old module of those functions | `mcpp.plugins.fs` |

The equivalence of `mcpp` and `mcpp.core` is not a compatibility unit. It is
permanent (principle 6).

### 3.8 What makes the plugin system strong

| Capability | Today | This design |
|---|---|---|
| Read: target, profile, features, package identity, directories, dependency paths | present | — |
| Read: the toolchain's tools, their environment, the C++ runtime contract, the toolset's identity and instance, mcpp's ninja | absent | E2 |
| Write: compile and link directives, placement, runtime search directories, generated sources, pack formats | present | — |
| Write: actions with roles, inputs, outputs, depfile, env, cwd, `PATH` prefix | present | — |
| Write: batched placement | absent | E4 |
| Diagnose: warnings in the engine's own form (impact, hint) | plain text only | E11 |
| Contract: interface specification, stability policy, engine floor | the protocol is described in docs/30 only | §3.2 (SPEC-007), §3.6 (E9) |
| Compose: L2 by one feature, families by feature, re-export chains | present except L2's name | §3.3 (P0) |
| Guide: a missing module names the feature that provides it | absent | E7 |
| Name: a plugin's modules are recognisable and cannot be impersonated in the index | a warning for rule modules only | E10, I1 |
| Test: a plugin's logic runs against a stated build context, and its directives are compared | absent | P6 |

## 4. Engine items

### 4.1 E1 · A workspace member used as a path dependency is built once (D1 settled)

**Problem.** `mcpp build --workspace` fans out over the members, and each member
is a separate graph with its own `target/<triple>/<fp>/`. A library member reached
as a path dependency is compiled in its own build and again in every member that
consumes it. Two `-p` invocations behave the same way. This holds for any
workspace in which several members share a library.

**Why neither existing store serves it.**

- **The global dependency cache** stores only index packages
  (`DepCacheIdentity::sourceKind == "version"`), because a path package's sources
  can change while its identity does not. That is correct for a cache shared
  across projects.
- **The tool store** keys a path package by `tree_stamp`, which covers the files
  under the package root. A library's inputs are not confined to its root:
  GalTranslPP's core compiles `../3rdParty/3rdModule/*.ixx` and includes headers
  from `../3rdParty/...`. A key that sees only the root would serve stale objects.

**Design.**

- **Scope.** The design applies to workspace members reached as path dependencies.
  A path dependency outside the workspace has a single consumer and keeps today's
  behaviour.
- **Directory and key.** The member is built as a keyed sub-build, as host tools
  are, in `<workspace>/target/.members/<package>/<key>/`. The key is the
  package's existing per-package build key (`cache_key::key_hex`: toolchain, flags,
  profile, features and the keys of its upstreams). The key excludes the consumer,
  so consumers that resolve the member identically compute the same key, and
  consumers that request other features or another profile compute another one.
- **Staleness.** The key selects a configuration, never freshness. Before its own
  ninja runs, every consumer runs the sub-build's ninja, which is a no-op when
  nothing changed. Ninja's time stamps and depfiles therefore judge staleness,
  including for inputs outside the root.
- **Handover.** The member's build program runs in the sub-build. Its directives
  (link libraries, deploys, runtime search directories) reach each consumer from
  the sub-build's cached record, as a dependency's directives do today. Its
  actions, such as a vcpkg install, run in the sub-build's ninja. Objects and BMIs
  reach the consumer through the stage edges the global cache already uses,
  including the phony order-only prerequisite that keeps partition order.
- **Concurrency.** Concurrent consumers take a lock on the sub-build directory.

**Compatibility.** After the upgrade each such member is compiled once more, into
the new directory. The root package's outputs and command lines are unchanged.

**Criterion.** Compilations are counted from the sub-build's and the consumers'
`.ninja_log`, with the number of library units as the denominator.

- A workspace whose two program members depend on one library member:
  `mcpp build --workspace` compiles each library unit once, and a following
  `mcpp build -p <second program>` compiles none of them.
- A header outside the library's root is changed: the next build recompiles its
  includers and no other library unit.
- Two concurrent `-p` builds of different programs both succeed, and each library
  unit is compiled once.
- The no-op ninja of the sub-build is timed on the Windows row (§12, finding 11).

### 4.2 E2 · Build information for build programs (D2 settled)

**Problem.** A build program can read `toolchain_dir()`, `compiler()`,
`cxx_stdlib()` and `sysroot_dir()`. It cannot name the tools of the resolved row, the
environment they need, or the runtime contract. A plugin that drives a foreign
build system therefore lets that system detect a toolset of its own, or
reconstructs mcpp's toolset privately. deps-vcpkg and deps-cmake do the latter on
Linux (`mcpp::deps::program_compilers`), for one row only.

**What the engine holds today.**

- **The cl.exe row.** Detection synthesises `INCLUDE`, `LIB` and `PATH` into
  `Toolchain::envOverrides`, and mcpp applies them to its own compiles and to
  ninja.
- **The llvm row with an MSVC sysroot.** `bind_msvc_sysroot` records
  `msvcToolsDir`, `msvcToolsVersion`, `msvcRedistDir`, `windowsSdkRoot` and
  `windowsSdkVersion`. clang locates the headers itself, and no environment is
  synthesised.

**Design.** The build-program framework states the resolved build as a set of
facts, carried as `MCPP_*` variables in the way the existing accessors are. How to
use them is the plugins' concern.

| Accessor | Answers |
|---|---|
| `mcpp::tool(role)` | the row's tool for `cc`, `cxx`, `ld`, `ar`, `rc`, `as`, `mt` |
| `mcpp::abi_tool(role)` | the target ABI's native tool for the same roles. On the MSVC ABI these are `cl`, `link`, `lib`, `rc`, `ml64` and `mt` of the resolved toolset and SDK; on other rows they equal `tool(role)` |
| `mcpp::tool_env()` | the environment the ABI's tools read, one `KEY=value` per line. On the MSVC ABI this is `INCLUDE`, `LIB`, `LIBPATH` and the directory variables (`VCToolsInstallDir`, `WindowsSdkDir`, `WindowsSDKVersion`, `UniversalCRTSdkDir`, `UCRTVersion`), synthesised for the llvm row by the function the cl.exe row uses. It is empty elsewhere |
| `mcpp::toolset_identity()` | a path-free identity of the ABI toolset, for example `msvc 14.44.35207; sdk 10.0.26100.0` |
| `mcpp::msvc_instance_dir()` | the Visual Studio instance the MSVC toolset belongs to when it comes from one (`msvc@system`), and empty for a managed toolset. `bind_msvc_sysroot` already records the origin |
| `mcpp::ninja_program()` | the ninja that mcpp itself runs, so that a foreign build system can use it rather than require one on the host |
| `mcpp::cxx_runtime()` | the resolved contract: `self-contained`, `toolchain-coupled` or `host-coupled` (E2b) |
| `mcpp::msvc_crt_linkage()` | `static`, `dynamic`, or empty off the MSVC ABI (E2b). These are the values `place-dlls --crt` already receives |

The facts belong to `mcpp.core` (§3.2) and are listed in SPEC-007 with the
protocol that introduced each and its availability per row. They are stated, never interpreted. `toolset_identity()` lets
a consumer key a cache by version rather than by path.

**Compatibility.** The new variables enter every build program's context hash, so
every build program runs once more after the upgrade and then returns to its
cache. The changelog lists this among the compatibility effects.

**Criterion.**

- On the Windows row with Visual Studio masked and `msvc@14.44.35207` resolved
  (`measure-windows-tool-crt.yml`), a build program reads an `abi_tool("cxx")`
  under the managed toolset and a `tool_env()` whose `INCLUDE` names that toolset
  and its SDK.
- On the llvm MSVC-ABI row, `tool("cxx")` is clang++ and `abi_tool("cxx")` is the
  sysroot's `cl.exe`.
- On the Linux rows, both accessors name the payload's tools and `tool_env()` is
  empty.
- A unit test compares `msvc_crt_linkage()` with the `--crt` value of the same plan
  for each of the three contracts.
- On the Visual Studio row `msvc_instance_dir()` names the instance; on the masked
  row with the managed toolset it is empty.

### 4.3 E2b · The C++ runtime contract (confirmed by reading E2b)

Objects a plugin produces are linked into the program, so they must follow the
program's C++ runtime contract. The readings E2b-1 to E2b-3 show both ways a
mismatch appears on the MSVC ABI:

- **With a static library.** A static library built with `/MD` and a program
  built with `/MT` fail the link (`/failifmismatch ... 'RuntimeLibrary'`). This
  is loud, and correct.
- **With a DLL.** A DLL built with `/MD` and a program built with `/MT` link and
  run, and the process holds two C++ runtimes. Nothing reports it. This is the
  silent case, and the more dangerous one: memory or a standard-library object
  that crosses the boundary is owned by two heaps.

The accessors `cxx_runtime()` and `msvc_crt_linkage()` of E2 state the contract.
P2 (§6.5) acts on them, including the silent case.

### 4.4 E3 · `mcpp pack -p`

`build`, `run` and `test` select a workspace member with `-p`; `pack` does not.
`pack` takes `-p` with the same resolution order (qualified name, package name,
member path).

**Criterion.** `mcpp pack -p <member> --format <f>` at the workspace root produces
the same stage and output as `mcpp pack --format <f>` in the member's directory.

### 4.5 E4 · Batched file placement with `mcpp stage --list` (D5 settled: adopted)

**What it does.** It places many files with one process. Today every placed file is
one process:

- each `mcpp::deploy` entry becomes one `stage_file` edge running `mcpp stage` for
  one file;
- a build program's copy action runs `${mcpp.self} stage` for one file.

Reading E4: 1270 deploy edges take 4.5 s of a first build on Windows, and one process
places the same files in 0.5 s. The release layout of the validation project adds
2675 such actions to a pack. A no-op build runs none of these edges (`restat`), so
the gain is on a first build, a changed input and a pack.

**Why it belongs to the engine.** File placement is a general build step, and mcpp
already owns it through `mcpp stage`, an internal subcommand that generated edges
reach as `$mcpp` and actions reach as `${mcpp.self}`. The item extends that
subcommand, and no new subcommand is added.

**Design.**

- **The command.** `mcpp stage --list <file> [--verify content|size]`. The file
  holds one entry per line, `<source>\t<destination>`, separated by a tab as the
  `deploy` directive is, because a Windows path contains `:`. Each entry keeps
  today's single-file semantics: an equivalent destination is not written, a
  write goes out of place and is renamed, sharing violations are retried, and a
  destination with several sources compares their bytes (SPEC-007 R4.2).
- **The engine's own use.** The deploy entries of one program become one edge. Its
  inputs are every source and the list file, and its outputs are every
  destination. The rule has `restat = 1`, so a destination whose bytes did not
  change keeps its time stamp and dirties nothing downstream. The list file is
  written only when its content changes, so the edge re-runs only when an entry is
  added, removed or changed.
- **Actions.** A build program may pass a list file to `${mcpp.self} stage
  --list`. docs/30 documents the form, and the single-file form stays.
- **Rejected alternatives.** A tree-mirror mode cannot express the renames and
  exclusions a release layout needs. A layout stated in the pack format would put
  each project's layout policy into the engine's format.

**Compatibility.** The deploy edges of `build.ninja` change shape, so after the
upgrade the placement edge runs once. Its content comparison writes nothing that is
already equal.

**Criterion.**
- 1270 deploy entries produce one placement edge, and on the Windows row it takes
  at most 1 s (4.5 s before, by reading E4).
- A no-change build runs no placement edge.
- One changed source rewrites its destination only; the other destinations keep
  their time stamps.
- Two packages that place the same destination with different bytes fail as they
  do today.

### 4.6 E5 · The project fast path on PE and Mach-O

**Problem.** `try_fast_build` accepts a build only when `validated_artifact_snapshot`
finds a stored `Pass` verdict for every artifact. Only the ELF run-time validator
writes such a verdict, so on Windows and macOS every `mcpp build` and `mcpp run`
plans again. This follows from #400 and is recorded in e2e 645 and 821, but no
issue tracks it.

**Design.**

- An artifact whose format has no run-time validator is recorded with the verdict
  `NotApplicable`, together with the same contract hash and stat fingerprint.
- The fast path accepts `Pass` and `NotApplicable`.
- This is sound on PE, because the check that matters there is an edge
  (`place-dlls` with its depfile), which ninja runs on every relink, including
  under the fast path.
- On Mach-O no check is lost, because none exists today.

**Criterion.**

- e2e 645 reads MEASURED on the Windows and macOS rows.
- An A-B-A test in the form of e2e 611 (build A, build B, build A; the third runs
  A) passes on both rows.

### 4.7 E6 · The library interface: a specification, and warnings at its readers (settled: warnings only)

The specification is designed in §5. The engine changes of phase 1 are these:

- `mcpp pack` warns when an exported module is not shipped, and names each such
  module (§5.4, W2).
- The "Withheld" row lists every unit that is not shipped (§5.4). This is a
  correction of a report, not an enforcement.
- `mcpp build` keeps its warning for the package being built, reworded (§5.4, W1).
- `mcpp build` warns when the package being built imports a module of a dependency
  that is outside that dependency's interface (§5.4, W3).

Nothing becomes an error in this design.

### 4.8 E7 · A missing build-program module names the feature that provides it

**Problem.** When a build program imports a module that no dependency offers, the
engine lists what is importable and which dependencies were declared without
`host-module = true`. It does not say that the module exists in a feature of a
declared dependency that is not enabled. This applies to every package with
features: `rules-*`, `dist-*`, `tools-*`, and L2 under §3.3.

**Design.** On that error path only, the engine reads the module declarations of
the units listed by the features that are not enabled, in the dependencies that
the build program may reach. When one of them provides the missing module, the
error names it:

```
error: build.mcpp imports 'mcpp.plugins.toolset'
  provided by: mcpp.plugins, feature "plugins-core" (not enabled)
  hint: [build-dependencies] mcpp.plugins = { ..., features = ["plugins-core"] }
```

The declarations are read as text. The feature's units are not compiled, and a
successful build does no additional work.

**Criterion.**
- With `mcpp.plugins` declared without features, `import mcpp.plugins.toolset;`
  fails with a message that names `plugins-core`.
- The same for `import mcpp.rules.qt;` and `rules-qt`.
- A successful build's plan is unchanged.

### 4.9 E8 · `mcpp.core`, and `mcpp` as its permanent equivalent

As §3.2 describes: two embedded units, one re-exporting the other, and SPEC-007's
`mcpp.core` section with the protocol table and the stability policy.

**Criterion.**
- A build program with `import mcpp.core;` and one with `import mcpp;` produce the
  same directives.
- A program that imports both compiles.
- The protocol table of SPEC-007 is checked against `kProtocolVersion` by a unit
  test.

### 4.10 E9 · The per-package engine floor

As §3.6 describes.

**Criterion.**
- A package with `mcpp = ">=2099.1.1"` stops with a message naming the package,
  the floor and the running version.
- `mcpp = "2026.9.28.2"` (bare) is refused with the `>=` hint.
- A floor at or below the running version builds.
- A workspace member inherits `[workspace.package] mcpp`.

### 4.11 E10 · The module-name warning covers every build-program module

**What exists.** `reserved_prefix_warning` covers only a build rule's module.
**What changes.**

- **Scope.** The warning applies to every module a package offers to build
  programs: its lib root and its feature units when it is reached with
  `host-module = true`.
- **Accepted names.** `mcpp.<own namespace>.*` is accepted.
- **Reserved names.** The reserved second segments of §3.5 are refused for
  packages outside namespace `mcpp`, as a warning.

**Criterion.**
- `mcpp.acme.x` from namespace `acme` is silent.
- `mcpp.rules.x` from namespace `acme` warns.
- `mcpp.other.x` from namespace `acme` warns and names the accepted form.

### 4.12 E11 · Structured diagnostics from build programs

**What exists.** A build program's `mcpp::warning(text)` reaches the user as plain
text.

**What changes.**

- **The directive.** `mcpp::diagnostic{severity, message, impact, hint, path}`
  is carried by one directive and rendered in the engine's own form. It also
  reaches `--message-format json` with the same fields as the engine's
  diagnostics (SPEC-003 codes do not apply; a plugin names its own code).
- **The old call.** `mcpp::warning(text)` remains, equivalent to a diagnostic
  with a message only.

**Criterion.**
- A plugin's diagnostic renders with its impact and hint lines.
- It appears in the JSON stream with its fields.
- It is shown on a cached replay as the engine's own advice is (e2e 139's shape).

### 4.13 E12 · `[lib]` reports unknown keys

`[lib]` accepts an unknown key without any message (measured, §2.1), while
`[build]` and `[package]` warn and ignore it. A misspelt `path` in `[lib]`
therefore changes the lib root silently.

**Design.** `[lib]` reports unknown keys as `[build]` does.

**Criterion.** `[lib] pth = "src/x.cppm"` produces the warning naming the
supported keys.

## 5. The library interface specification (new SPEC-008, phase 1)

The specification is written in `docs/specs/` in Chinese, following that
directory's conventions. This section fixes its content.

### 5.1 What the specification must achieve

| Property | What it means here |
|---|---|
| Clear semantics | one term for what a consumer may import, one for what a package ships, one for what it withholds |
| Consistency | a consumer sees the same interface whether the package reaches it as source or as a packed distribution |
| Stability | the interface is a named set that changes only through the package's own declaration, so a consumer can depend on it across versions |
| Distribution | the packed form is complete (a consumer can compile against it) and minimal (nothing outside the interface's closure leaks) |
| Elegance | one declaration per package, expressed in C++ itself (a module and its re-exports), not in a list maintained beside the code |
| Compatibility | no manifest that builds today stops building |

### 5.2 Definitions

- **Interface root.** The unit found at `[lib].path`, or at
  `src/<last segment of the package name>.<module interface extension>` by
  convention. It must declare a primary module interface (`export module <name>;`,
  not a partition). This rule exists today.
- **Public modules.** The interface root's module, and every module and partition
  that it re-exports with `export import`, transitively. These are the names a
  consumer may import.
- **Public headers.** The files under `include/` (docs/12; unchanged).
- **Interface.** The public modules together with the public headers.
- **Shipped closure.** Every unit that the public modules' interfaces import,
  transitively, whether re-exported or not. A consumer needs these units to build
  the public modules' BMIs. `mcpp pack` ships the shipped closure, and names in its
  report any implementation partition that the closure contains, as it does today.
- **Withheld units.** Every other unit of the package. They are compiled into the
  library and are not shipped.

The distinction between public modules and the shipped closure is the one part
that is new. It separates what a consumer may import, which is the contract, from
what a consumer must be able to compile, which is a necessity of BMIs.

### 5.3 Rules

- **I1. One interface for every form.** A package's interface is the same whether a
  consumer builds it from source or uses its packed form. *Phase 1: stated, and
  warned about (W3); source builds are not restricted.*
- **I2. One root per package.** A package has at most one interface root, so it
  has one importable name that corresponds to its identity `(namespace, name)`. A
  package whose code is organised as several modules expresses them through the
  root with `export import`; this is the facade form. No list of roots exists, and
  none is planned.
- **I3. Public module names SHOULD be qualified by the package.** Module names are
  global in a program, so a public module SHOULD be named `<namespace>.<name>` or
  below it (`<namespace>.<name>.<part>`). *Phase 1: a recommendation;
  `mcpp pack` reports public modules outside the prefix as a note.* The engine
  already refuses two modules of the same name in one graph. This rule covers a
  package's library interface. The modules a package offers to build programs
  follow §3.5 instead.
- **I4. A headers-only interface is legitimate.** A library may implement itself in
  modules and publish only headers, for example a C API. Such a library has no
  interface root, and its public modules are empty. *Phase 1: `mcpp pack` treats a
  library without a root as headers-only, as today, and warns (W2) because it
  cannot tell intent from omission.*
- **I5. The packed form ships exactly the shipped closure and the public headers.**
  It is complete, because every public module's BMI can be built, and minimal,
  because no withheld unit is shipped. This is today's behaviour.

### 5.4 Diagnostics (phase 1: warnings only)

Each diagnostic is emitted by a step that reads the interface, and only for the
package whose author can act on it.

| | Emitted by | Condition | Text (impact and hint form) |
|---|---|---|---|
| W1 | `mcpp build`, for the package being built (a dependency is silent, as today) | a `lib` target exports modules and has no interface root | impact: `mcpp pack` will publish this library without a module interface. hint: add `src/<tail>.<ext>` re-exporting the public modules, or set `[lib].path`; a headers-only library can ignore this |
| W2 | `mcpp pack` | exported modules exist that are not public: all of them when there is no root, or those not reachable from the root | names each such module. impact: a consumer of the packed form cannot import it. hint: re-export it from the root, or keep it internal on purpose |
| W3 | `mcpp build`, for the package being built | one of its units imports a module of a dependency that declares a root, and that module is not among the dependency's public modules | names the module and the dependency. impact: the build succeeds from source, but fails against the dependency's packed form. hint: import a public module of the dependency |
| Report | `mcpp pack` | always | "Interface" lists the shipped closure, and "Withheld" lists every other unit. This replaces the "(nothing)" that the local reading of §2 shows |

W3 fires only for dependencies that declare a root, so libraries without a root,
such as GalTranslPP's core, cause none.

### 5.5 Phase 2 (conditions, no design)

W1 and W2 become errors, with an explicit declaration for a headers-only library,
when three conditions hold:

1. An mcpp-index sweep reports the libraries that export modules without a root,
   and which of them publish headers only on purpose.
2. The engine release named by the index `min_mcpp` reads the headers-only
   declaration. An older engine ignores an unknown key in `[lib]` (§2.1), so the
   declaration breaks no older client, but it has no effect there. Phase 2's
   error therefore applies only from the floor on.
3. The specification has reached the review state.

When the sweep reports none, W1 and W2 have served their purpose, and phase 2
reduces to the report.

## 6. Plugin design (mcpp-plugins)

The official plugins are general plugins. Every behaviour below is an option on a
plugin's `options` structure, set from `build.mcpp`, and none of it names a
project.

### 6.1 P0 · The package's structure after §3

- **L2.** `plugins-core` (renamed from `surface`) lists `src/declare.cppm`,
  `src/toolset.cppm` and `src/fs.cppm`. `plugins-testing` lists
  `src/testing.cppm`. `[build] sources` keeps `src/plugins.cppm`, the std-only
  unit that `mcpp-embed` shares.
- **L3.** Every family feature implies `plugins-core`, as every member implies
  `surface` today.
- **The floor.** `[package] mcpp = ">=V"` names the first release carrying E2,
  E4, E7 and E11.
- **Compatibility.** The units of §3.7.

### 6.2 P1 · One shared module: `mcpp.plugins.toolset` (L2)

A new L2 module turns E2's facts into what a foreign build system needs, once
for every plugin:

```cpp
namespace mcpp::plugins::toolset {
enum class source   { resolved, detected };    // who chooses the tools
enum class compiler { abi_native, row };       // which compiler `resolved` hands over

struct choice {                                // embedded in each plugin's options
    source   toolset = source::resolved;       // D3
    compiler cc      = compiler::abi_native;
};

enum class mechanism {                         // how `resolved` reaches the foreign system
    instance,     // an MSVC toolset from a Visual Studio instance: that instance, selected
    chain,        // any other toolset: compilers named in a chain-loaded toolchain
    detected,     // `source::detected`: nothing is named (compatibility unit, until 2027-03-28)
};

struct resolved_tools {
    mechanism   how = mechanism::detected;
    std::string instance_dir;                  // msvc_instance_dir(), for `instance`
    std::string toolset_version;               // e.g. 14.44.35207, for `instance`
    std::string cc, cxx, ld, ar, rc, mt;       // absolute paths, for `chain`
    std::vector<std::pair<std::string, std::string>> env;   // tool_env(), for `chain`
    std::string path_prefix;                   // the tools' directories, for `chain`
    std::string identity;                      // toolset_identity()
    std::string crt;                           // msvc_crt_linkage()
};

std::expected<resolved_tools, std::string> resolve(const choice&);
}
```

**The mechanism follows the toolset's origin.**

- **`instance`.** An MSVC toolset from a Visual Studio instance (`msvc@system`,
  which is what mcpp resolves on a machine with Visual Studio) is used through the
  instance. The foreign system's own toolset loading stays in place, pointed at the
  instance mcpp resolved. MSBuild is present, and every port kind builds (M2a).
- **`chain`.** A managed MSVC toolset, and every toolset on the other rows, is named
  in a chain-loaded toolchain file. On the MSVC ABI there is no Visual Studio
  instance to load, and no MSBuild exists.
- **`compiler = row` on the MSVC ABI.** It always uses `chain`: the instance
  mechanism can hand over only the instance's own `cl`, so a request for the row's
  clang is met by naming it.
- **Linux.** `program_compilers` becomes `resolve({resolved, row})` on the Linux
  libc++ row, and the plugin-private reconstruction is removed.

### 6.3 P1 · deps-vcpkg

**Options.** `options.toolset` (the `choice`) and `options.crt_linkage` (§6.5).

**Under `instance`.**
- The plugin runs vcpkg with `VCPKG_VISUAL_STUDIO_PATH=<instance_dir>` in the
  action's environment. Reading M1: when the instance is the one vcpkg would choose
  anyway, the ABI hash is unchanged and nothing is rebuilt.
- When the resolved toolset is not the instance's newest, the plugin derives a
  triplet that adds `VCPKG_PLATFORM_TOOLSET_VERSION <version>`. That triplet has
  its own hash, so its ports build once.
- MSBuild ports build (M2a).

**Under `chain`.**
- **The derived triplet.** It is named `<base>-mcpp-<hash>`. The base triplet
  (the project's, or the default) is inlined, not `include()`d, because vcpkg's
  documented hash covers the triplet file's content and not a file it includes.
  The plugin appends to it:
  - `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`;
  - `VCPKG_ENV_PASSTHROUGH_UNTRACKED` for the variables of `tool_env()` and the
    tool paths;
  - a comment carrying `toolset_identity()`;
  - `VCPKG_CRT_LINKAGE` from §6.5.
- **The host triplet.** The derived triplet is passed as the host triplet too
  (`--host-triplet`). Otherwise vcpkg builds its host ports with a standard
  triplet and looks for Visual Studio (§2.2).
- **The toolchain file.** It reads each tool path from the environment through
  `file(TO_CMAKE_PATH)` and sets `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`,
  `CMAKE_RC_COMPILER` and `CMAKE_MT`. It then includes vcpkg's own
  `scripts/toolchains/windows.cmake` on the MSVC ABI (or `linux.cmake`,
  `osx.cmake`), so that ports keep vcpkg's standard flags. Its text holds no path.
- **`PATH`.** The action runs vcpkg with the tools' directories first on `PATH`
  and `VCPKG_KEEP_ENV_VARS=PATH`, because make-based ports find `link.exe` on
  `PATH` (M3b, M3c). vcpkg does not hash that variable.
- **A port that needs MSBuild.** It cannot build without Visual Studio, and under
  `chain` it fails with `/p:PlatformToolset=external` (M2c). The plugin recognises
  that failure in vcpkg's output and reports it by name. The report says that the
  port needs MSBuild, which the managed toolset does not contain, and suggests
  `msvc@system` or `toolset = detected` for this project.

**Under `detected`.** The behaviour of 0.16.0, except that the CRT linkage of
§6.5 applies. It lives in the compatibility unit `deps/compat/detected_toolset.cppm`
until 2027-03-28 (§3.7) and prints its note once per build.

### 6.4 P1 · deps-cmake

**Options.** `options.toolset`, `options.generator` (`default`, `ninja`) and
`options.crt_linkage`.

- **Under `instance`.** CMake's default generator (Visual Studio) is kept and
  pointed at the resolved toolset through two mechanisms CMake documents:
  `CMAKE_GENERATOR_INSTANCE=<instance_dir>` and `-T version=<toolset_version>`.
  `generator = ninja` switches to the `chain` form below.
- **Under `chain`.** The Ninja generator, with mcpp's own ninja
  (`ninja_program()`) as `CMAKE_MAKE_PROGRAM`, and the tools by absolute path. The
  action's environment is `tool_env()`, with the tools' directories first on
  `PATH`.
- **In both.** `CMAKE_MSVC_RUNTIME_LIBRARY` follows §6.5.
- **Under `detected`.** CMake's default generator and the toolset it finds, as in
  0.16.0, with `CMAKE_MSVC_RUNTIME_LIBRARY` from §6.5. It lives in the same
  compatibility unit as deps-vcpkg's, until 2027-03-28.

### 6.5 P2 · The C++ runtime linkage follows the contract (E2b confirmed)

- **Deriving the linkage.** `VCPKG_CRT_LINKAGE` and `CMAKE_MSVC_RUNTIME_LIBRARY`
  follow `msvc_crt_linkage()`. Under `self-contained`, the default vcpkg triplet
  becomes `<arch>-windows-static`.
- **A project triplet that contradicts the contract.** If a triplet named by the
  project has a CRT linkage that contradicts the contract, the plugin stops with
  an error naming both statements. Two explicit statements that cannot both hold
  are an error (principle 4). This includes the silent DLL case of reading E2b-3,
  which no linker reports.
- **Override.** `options.crt_linkage` overrides the derived value; the override is
  the project's explicit statement, and it wins.

**Criterion.** Reading E2b repeated after P2:
- `self-contained` with the default triplet builds and runs with `/MT`
  throughout;
- `self-contained` with an explicit `x64-windows` fails with the plugin's error;
- `toolchain-coupled` is unchanged.

### 6.6 P3 · vcpkg's ABI hash across machines

Under `chain`, the hash depends on `toolset_identity()` and on the compilers'
executables, never on a path. Reading P3 confirms it: the same toolset at another
path restored every package from the cache.

### 6.7 P4 · Binary sources, and P5

- **P4.** vcpkg reads `VCPKG_BINARY_SOURCES` and `VCPKG_DEFAULT_BINARY_CACHE`
  from the environment, and the plugin passes them through. The documentation
  states it; nothing is added. Hosting a cache is a project's decision (§9).
- **P5.** No design exists until a reading gives a CMake dependency's share of a
  consumer's build.

### 6.8 P6 · The plugin test kit (`mcpp.plugins.testing`)

A plugin's logic is a function of the build context. Its effect is the set of
directives and actions it emits, which are structured `mcpp:` lines.

- **What the kit does.** It runs a plugin function against a stated context: the
  target, the toolchain facts of E2, the profile and the features. It then
  compares the directives and actions the function emitted with the expected
  ones.
- **Where the tests run.** In the plugin's own `tests/`, on every row, without a
  foreign toolchain installed. A foreign tool's behaviour stays with the CI rows of
  §6.9.
- **Feature.** `plugins-testing`, so that no build program compiles it unless it
  asks.

**Criterion.**
- deps-vcpkg's selection of mechanism (`instance`, `chain`, `detected`) is tested
  on Linux against contexts that describe a Visual Studio row, a managed row and a
  libc++ row.
- A deliberate change to the emitted triplet fails such a test.

### 6.9 Plugin CI and documentation

**CI.** The legs of §2.2 become the plugins' CI rows:
- the Visual Studio row with `resolved` (M1, M2a);
- the masked row with the managed toolset: a CMake port, icu, a CMake project;
- the row that expects the MSBuild error by name (M2c);
- E2b with P2.

The existing Linux libc++ row covers `resolve({resolved, row})`.

**Documentation.**
- Each option is documented with its values, default and upgrade effect, and
  which port kinds each mechanism builds.
- L2 is documented as `plugins-core`.
- The naming rule of §3.5 is documented for plugin authors.

## 7. Index items (mcpp-index)

- **I1 · The naming rule as an admission rule.** Four places change:
  - `docs/package-types.md` (both languages) gains a section on build-plugin
    packages: the naming rule of §3.5, the manifest form (`plugins-core`, and
    `reexport` where consumers need L2), and the engine floor of §3.6.
  - `docs/repository-and-schema.md` states the rule among the validation rules.
  - `.agents/skills/add-mcpp-index-package/SKILL.md` adds the rule to its
    checklist.
  - `validate.yml` checks every member's provided modules (the `provides` fields
    of `mcpp emit build-database`) against its namespace. The reserved second
    segments are refused as new namespaces.
- **I2 · The sweep for E6 phase 2.** It reports the libraries that export modules
  without a root, and which of them publish headers only on purpose.

## 8. Order, repositories and versions

| Step | Repository | Depends on | Version |
|---|---|---|---|
| E2, E3, E4, E5, E6 phase 1, E7, E8, E9, E10, E11, E12; SPEC-007 (`mcpp.core`, build information, reserved segments); SPEC-008 | mcpp | none | next mcpp release (V) |
| E1 | mcpp | none; can ship alone | V or a following release |
| P0 to P4, P6 | mcpp-plugins | V released; `[package] mcpp = ">=V"` | plugins 0.17.0 |
| I1 | mcpp-index | E10 released (the engine and the index state one rule) | none |
| Validation | Sunrisepeak/GalTranslPP | plugins 0.17.0 | `resolved` on the masked row; `pack -p` |
| I2 | mcpp-index | E6 phase 1 released | input to E6 phase 2 |

## 9. Outside this design, and why

| Item | Why |
|---|---|
| Hosting a vcpkg binary cache (release assets, GitCode, NuGet) | a project decides whether first builds justify a feed; vcpkg already reads the sources (P4) |
| Two `-p` invocations instead of `--workspace`; Updater as an `artifacts` dependency; hoisting repeated `[target.windows.build]` values to the root | the project's manifests; each is available today |
| A CI job for the `release` profile | the project's CI |
| Re-running build programs under `mcpp pack` | by design: the pack context (`pack_format`) is an input of the build program; the recompilation it prints costs about 1 s |
| Flat module names (`Tool`, `Dictionary`) in GalTranslPP's core | the project's naming; I3 and the facade form of I2 are the remedy when the library is published |
| Deriving `host-module` from a build program's imports (a consumer writes no `host-module = true` when `build.mcpp` imports the module) | a usability improvement, independent of every item here; it has its own evaluation, with its own criterion, so that it is not lost by being folded into another item |
| Payloads provisioned when a build program uses them | unnecessary once families are enabled by feature (§3.4) |
| An index resolver that skips versions above the engine's floor | a follow-up of E9 in mcpp-index and xlings |

## 10. Compatibility and upgrade

| Change | Effect on an existing project |
|---|---|
| E1 | each workspace member used as a path dependency is compiled once more, into the workspace-scoped directory |
| E2 | every build program runs once more, because its context gains variables |
| E4 | the placement edge runs once; its content comparison writes nothing that is equal |
| E5 | the first build records the new verdicts; later no-op builds take the fast path on Windows and macOS |
| E6 phase 1, E10, E12 | new warnings only; the only output change is the corrected "Withheld" row |
| E7, E8, E11 | none; `import mcpp` keeps working permanently |
| E9 | none, until a package declares a floor |
| P0 | none; `surface` is an alias for six months |
| P1 default `resolved` | see D3 in §11 for each case; `detected` stays in a compatibility unit until 2027-03-28 |
| P2 | none on the dynamic CRT; `self-contained` projects change from a mismatch (loud or silent) to a consistent link, or to a named error when their own triplet contradicts the contract |

The first build after upgrading to V pays E1, E2 and E4 at once. The changelog
states them together, so that one slow build is not read as a regression.

## 11. Decisions

| | Decision | State |
|---|---|---|
| D1 | E1 as a keyed sub-build | settled |
| D2 | E2 as general build information; use belongs to plugins | settled |
| D3 | P1's default is `resolved`, with the mechanism chosen by the toolset's origin; `detected` in a compatibility unit until 2027-03-28 | settled |
| D4 | E2b first, then P2 | settled; E2b confirmed |
| D5 | E4 as `mcpp stage --list` | settled |
| D6 | E6 as warnings only | settled |
| D7 | E5 `NotApplicable` verdict | settled |
| D8 | W3 in phase 1 | settled |
| D9 | I3 as a recommendation, with a note from pack | settled |
| D10 | the three layers of §3.1, with L2 behind `plugins-core` and the families behind their features | settled |
| D11 | P2 refuses a project triplet that contradicts the contract, including the silent DLL case | settled (revision 5) |
| D12 | `mcpp` and `mcpp.core` permanently equivalent | settled |
| D13 | module names by §3.5: engine warning (E10) and index admission rule (I1) | settled |
| D14 | the per-package engine floor `[package] mcpp = ">=V"` (E9) | settled |
| D15 | compatibility units with a six-month retirement date and a CI check (§3.7) | settled |
| D16 | E7 (the missing module names its feature), E11 (structured diagnostics), E12 (`[lib]` unknown keys), P6 (test kit) | settled (revision 5) |

**D3 in detail: what the default `resolved` changes, case by case.**

| Case | 0.16.0 (`detected`) | 0.17.0 default (`resolved`) | Evidence |
|---|---|---|---|
| Windows with Visual Studio; mcpp resolves `msvc@system` (the usual machine, and GalTranslPP's CI) | vcpkg picks an instance itself | the same instance, named; **no rebuild**; MSBuild ports build | M1, M2a |
| the same, but the machine has several instances or toolsets and mcpp resolves another one than vcpkg would | ports built by a toolset other than the program's | ports built by the program's toolset; **one rebuild** | M1 (mechanism), vcpkg `get_toolset` |
| Windows without Visual Studio; managed toolset | deps-vcpkg fails: no instance | CMake and make ports build; MSBuild ports fail with the named error | M3a, M3c, M2c |
| Windows with Visual Studio, and the project pins a managed toolset | ports built by Visual Studio's toolset, the program by the managed one (inconsistent) | CMake and make ports rebuilt once by the managed toolset; **an MSBuild port now stops with the named error** | M2c |
| Linux, gcc or clang row | ports built by the host's `cc`/`c++` | ports built by the row's compilers; one rebuild | the libc++ row already works this way |
| macOS | ports built by Apple clang | ports built by the row's clang; one rebuild | as above |

The fourth row is the only one where the new default stops a build that 0.16.0
completed. What 0.16.0 produced there was a program and its dependencies compiled
by two toolsets. The named error gives two remedies: `msvc@system`, or
`toolset = detected` while the compatibility unit exists.

## 12. Global review (revision 4)

The whole design was read again against its principles, against the readings, and
against the recorded failure shapes of earlier rounds. Findings 1 to 14 of
revisions 2 and 3 stand as recorded there unless a finding below revises them.

1. **A withdrawn item was replaced, not patched.** Revision 3's E7 (a `[host-module]`
   table) and its intermediate successor (deriving a unit's build from its
   imports) both changed the engine's build semantics. The only package that needs
   them is mcpp-plugins, which has an ordinary build (`mcpp-embed`) beside its
   build-program modules on purpose. A general engine change for one package's
   layout contradicts principle 1. L2 behind `plugins-core` needs no engine
   change; the new E7 improves the one usability gap it leaves, for every package
   with features. Every mention of `[host-module]` as a table was removed.
2. **A compatibility argument was wrong (corrected in §5.5 and §3.6).**
   Revisions 2 and 3 said an older engine refuses a manifest with an unknown key
   inside a known table. That was measured on 2026-09-20 for `[c-abi]` and
   generalised. Measured now on 2026.9.28.2:
   - `[package]` and `[build]` warn and ignore an unknown key;
   - `[lib]` ignores it silently;
   - a dependency spec warns and ignores it.

   The phase-2 condition of E6 and the choice of `[package] mcpp` rest on this
   reading, not on the older one.
3. **Measuring the correction found a defect (E12).** `[lib]` gives no message for
   an unknown key, so a misspelt `path` goes unnoticed. This is the silent half of
   a behaviour whose loud half (`[build]`) already exists.
4. **A requirement had been folded away (now recorded in §9).** Deriving
   `host-module` from a build program's imports appeared in the discussion as half
   of item B. When B changed, it had no entry of its own. It is recorded as a
   separate evaluation with its own criterion, so that no later item carries it
   implicitly.
5. **The architecture needs its contract written.** Without SPEC-007's `mcpp.core`
   section, the stability policy and the protocol table exist only in this design.
   E8's criterion checks the table against `kProtocolVersion`, so that the
   specification cannot drift from the code.
6. **The naming rule has two enforcers, and they must state one rule.** E10
   (engine) and I1 (index) read the same reserved list. SPEC-007 is its single
   source, and I1 is ordered after E10 (§8).
7. **The reserved second segments must not collide with existing namespaces.**
   Checked against mcpp-index `e9b80c5`: none of `core`, `plugins`, `deps`,
   `rules`, `dist`, `tools` is a namespace. A dotted namespace such as
   `mcpplibs.capi` maps to `mcpp.mcpplibs.capi.*`, which the rule accepts.
8. **The floor cannot help an engine that does not read it.** An engine below V
   warns about `[package] mcpp` and continues, and a plugin needing E2 then fails
   to compile with a missing symbol. This is today's behaviour, not a regression.
   The index follow-up of §9 (a resolver that skips versions above the engine's
   floor) is the complete answer and is recorded, not designed.
9. **Compatibility units need their own check (D15).** A retirement date that
   nothing checks is not a retirement date. The CI check of §3.7 is part of the
   convention, and each first unit has its date.
10. **Upgrade costs coincide.** E1, E2 and E4 each cost one run after upgrading to
    V. §10 states them together.
11. **Items not measured.** These rest on documentation or on reasoning, and each
    is marked as such where it appears:
    - `CMAKE_GENERATOR_INSTANCE` with `-T version=` under `instance`;
    - the default `resolved` on the Linux gcc and macOS rows;
    - E1's extra ninja run on Windows;
    - E7's reading of inactive feature units.

    Each has a criterion that measures it.

Items checked and found in order:

- no item makes the engine learn a foreign tool;
- every warning has a reader;
- every criterion names its denominator or its reading;
- no item is justified by GalTranslPP alone;
- every changed generated command line has a stated one-time cost;
- every decision in §11 has a state.

## 13. Tasks, dependencies and criteria (revision 5)

Each repository receives one pull request that carries all of its tasks. The
order follows the dependencies: the engine first, because the plugins' floor
names its release; the plugins next; the index and the validation project last.

### 13.1 mcpp (one pull request, release V = 2026.9.29.1)

| Task | Items | Criterion (test) |
|---|---|---|
| T1 manifest | E9 `[package] mcpp` and `[workspace.package] mcpp`; E12 `[lib]` unknown keys | unit tests of the parser; an e2e that stops below the floor and builds at it |
| T2 `mcpp.core` | E8 the two embedded units and SPEC-007's `mcpp.core` section with the protocol table | an e2e with `import mcpp.core;`, `import mcpp;`, and both; a unit test comparing the table with `kProtocolVersion` |
| T3 build information | E2 accessors, including E2b's two contract accessors, `msvc_instance_dir`, `ninja_program`; protocol 14 | an e2e reading every accessor on the Linux row; the Windows rows of CI read the MSVC ones |
| T4 diagnostics | E11 structured diagnostics; E7 the missing module names its feature; E10 the module-name warning | e2e for each, including the cached replay of a diagnostic |
| T5 placement | E4 `mcpp stage --list` and one placement edge per program | a unit test of the list format; an e2e with many deploy entries: one edge, a no-change build runs nothing, one change rewrites one file |
| T6 packaging | E3 `mcpp pack -p`; E6 phase 1 (W1 to W3, the corrected "Withheld" row) and SPEC-008 | e2e for `pack -p`, and for the `Alpha`/`Beta` library with and without a facade |
| T7 fast path | E5 `NotApplicable` verdicts | e2e 645 reads MEASURED on Windows and macOS |
| T8 workspace | E1 keyed sub-builds for workspace members used as path dependencies | the e2e criteria of §4.1, counted from `.ninja_log` |
| T9 records | docs (04, 05, 07, 10, 12, 30 and their Chinese copies), CHANGELOG with the compatibility list, SPEC-007, SPEC-008 | the documentation checks of CI |

T1 to T7 are independent of each other and of T8. T3 precedes the plugins.

### 13.2 mcpp-plugins (one pull request, release 0.17.0)

| Task | Items | Depends on | Criterion |
|---|---|---|---|
| U1 structure | P0: `plugins-core` (was `surface`), `plugins-testing`, `mcpp.plugins.fs`, compatibility units with their CI check, `[package] mcpp = ">=V"` | V | the package builds; a consumer with `plugins-core` imports L2 |
| U2 toolset | P1: `mcpp.plugins.toolset` with `instance`, `chain`, `detected` (compat) | T3 | P6 tests of mechanism selection |
| U3 deps | P1 and P2 in deps-vcpkg and deps-cmake, P3, P4 documentation | U2 | the CI rows of §6.9 |
| U4 test kit | P6 `mcpp.plugins.testing` | T3 | the plugins' own tests use it |

### 13.3 mcpp-index (one pull request)

I1 (documentation, skill, `validate.yml` check) and the registration of mcpp V and
plugins 0.17.0; then I2 as a report of the sweep. Criterion: the full sweep is
green with V.

### 13.4 Validation

- **Sandbox.** A fresh xlings sandbox with the CN mirror configured for mcpp and
  xlings. It checks that the released V and plugins 0.17.0 install, build and run
  the index members and the examples of this design.
- **GalTranslPP.** PR #3 is rebased onto the latest upstream and adopts the
  results: `plugins-core` through the family features, `mcpp pack -p` from the
  root, and `resolved` in its CI. Its Windows CI measures the build again against
  the readings of §2.

### 13.5 Implementation record

**mcpp (release 2026.9.28.3).** T1 to T9 landed on one branch, one commit per task.
Each item has an e2e:

| Item | Test |
|---|---|
| E9, E12 | e2e 822 |
| E8 | e2e 823, plus `tests/scripts/test_protocol_table.py` |
| E2 | e2e 824 on Linux; e2e 825 on the Windows rows |
| E11, E7, E10 | e2e 826, plus unit tests in `test_provisions` |
| E4 | e2e 827, plus the updated `test_ninja_backend` |
| E6 | e2e 828 |
| E3 | e2e 829 |
| E1 | e2e 830 |

**Departures from the design, each for a reason found while implementing.**

1. **E1's shared directory is the member's own build directory.** The design
   planned a separate keyed directory `target/.members/<pkg>/<key>`. The member's
   own root build is used instead, so a `--workspace` build, which builds the
   member anyway, compiles it exactly once, and no second layout exists.
2. **E1 admits by key inputs, not by key hash.** The key hash of the member as a
   dependency differed from its key as the root in one input only,
   `package.index`: the namespace under which a dependency is reached, which is
   empty for a root. The admission therefore compares the key's inputs with that
   field removed.
3. **E1's stage edges compare content.** The global cache's stage edges compare
   sizes, which is sound for an immutable entry. e2e 830 measured that it is not
   sound for a member: a header change produced an object of the same size, and
   the consumer kept the old one. Units served from a member carry
   `servedFromMember`, and their edges compare content.
4. **E1 applies in the global cache mode.** The build keys are computed only
   there; `--cache off` compiles everything in the graph that asks for it.
5. **E2's `tool_env()` is the environment the engine itself runs the toolset
   with** (`INCLUDE`, `LIB`, `PATH`, `VSLANG` from `build_env_for_cl`), not the
   list of directory variables §3.2 named. One environment, one producer.
6. **E11's diagnostic has no `path` field.** No reader of a path was found.
7. **E5 is a reading first.** The verdict a PE or Mach-O artifact records on
   those platforms is already `Pass` (the ELF rules state "not applicable" and
   pass), so revision 1's attribution was not right. Every refusal point of both
   fast paths now states its condition under `-v`, e2e 645 prints it on the rows
   where the fast path declines, and the fix follows the CI reading.
8. **W3 is skipped under `MCPP_SCANNER=p1689`.** That scanner does not report
   which imports are `export import`, so every re-exported module would read as
   private.

**mcpp-plugins, mcpp-index, validation.** Recorded below as they land.
