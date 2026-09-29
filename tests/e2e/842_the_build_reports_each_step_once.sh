#!/usr/bin/env bash
# requires: python3
# 842_the_build_reports_each_step_once.sh -- build progress design 2026-09-29,
# §4 and §5.2 (the log medium: the output is not a terminal).
#
# A workspace whose members are core and cli, where cli also depends on util,
# a path package outside the workspace:
#
#   R1  a first build: each member's line is written with its outcome
#       (`done <span>`), util is folded into `Compiling 1 dependency`, and one
#       blank line precedes `Finished`; nothing redraws (no carriage return,
#       no escape sequence);
#   R2  a build program's line states `ran` on the first build and `cached`
#       on a planned build that changed nothing it reads;
#   R3  an edit to cli lists cli alone;
#   R4  `--verbose` names util and prints each step as `[f/t] <command>`;
#   R5  a failed step is reported while a slow `prepare` action of another
#       package still runs: `error: build failed` arrives seconds before
#       mcpp exits, not after ninja.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p ws/core/src ws/cli/src util/src
cat > ws/mcpp.toml <<'EOF'
[workspace]
members = ["core", "cli"]
EOF
printf '[package]\nname = "core"\nversion = "0.1.0"\n\n[targets.core]\nkind = "lib"\n' > ws/core/mcpp.toml
printf 'export module rp_core;\nexport int core_v() { return 1; }\n' > ws/core/src/core.cppm
printf '[package]\nname = "util"\nversion = "0.2.0"\n\n[targets.util]\nkind = "lib"\n' > util/mcpp.toml
printf 'export module rp_util;\nexport int util_v() { return 2; }\n' > util/src/util.cppm
cat > ws/cli/mcpp.toml <<'EOF'
[package]
name = "cli"
version = "0.1.0"

[dependencies]
core = { path = "../core" }
util = { path = "../../util" }

[targets.cli]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'import rp_core;\nimport rp_util;\nint main() { return core_v() + util_v() == 3 ? 0 : 1; }\n' > ws/cli/src/main.cpp
printf 'import mcpp;\nint main() { mcpp::define("RP_CLI=1"); return 0; }\n' > ws/cli/build.mcpp

cd ws
"$MCPP" build --workspace > b1.log 2>&1 || fail "the first build failed" b1.log

# R1
grep -qE "^ +Compiling core \(core\) +done [0-9]" b1.log || fail "R1: core has no final line" b1.log
grep -qE "^ +Compiling cli \(cli\) +done [0-9]" b1.log || fail "R1: cli has no final line" b1.log
grep -qE "^ +Compiling 1 dependency +done" b1.log || fail "R1: util is not folded into one line" b1.log
! grep -q "Compiling util" b1.log || fail "R1: the default output named a dependency" b1.log
prev=$(grep -B1 -E "^ +Finished " b1.log | head -1)
[ -z "$prev" ] || fail "R1: no blank line precedes Finished" b1.log
! grep -q $'\r' b1.log || fail "R1: the log holds a carriage return" b1.log
! grep -q $'\033' b1.log || fail "R1: the log holds an escape sequence" b1.log

# R2
grep -qE "^ *build\.mcpp cli +ran [0-9]" b1.log || fail "R2: the program's line does not state that it ran" b1.log
"$MCPP" build --workspace --profile dev > b2.log 2>&1 || fail "the planned rebuild failed" b2.log
grep -qE "^ *build\.mcpp cli +cached" b2.log || fail "R2: the second program line does not state cached" b2.log

# R3
printf 'import rp_core;\nimport rp_util;\nint main() { return core_v() + util_v() == 3 ? 0 : 2; }\n' > cli/src/main.cpp
"$MCPP" build --workspace > b3.log 2>&1 || fail "the incremental build failed" b3.log
grep -qE "^ +Compiling cli \(cli\) +done" b3.log || fail "R3: the edited member has no line" b3.log
! grep -qE "Compiling (core|1 dependency)" b3.log || fail "R3: a package with nothing to do has a line" b3.log

# R4
printf 'export module rp_util;\nexport int util_v() { return 2; }\nint unused = 0;\n' > ../util/src/util.cppm
"$MCPP" build --workspace -v > b4.log 2>&1 || fail "the verbose build failed" b4.log
grep -qE "^ +Compiling util \(path\) +done" b4.log || fail "R4: --verbose does not name the dependency" b4.log
grep -qE "^\[[0-9]+/[0-9]+\] " b4.log || fail "R4: --verbose prints no step" b4.log

# R5 -- a separate project: app's source does not compile, and its path
# dependency `slow` has a prepare action that takes six seconds.
cd "$TMP"
mkdir -p r5/app/src r5/slow/src
printf '[package]\nname = "slow"\nversion = "0.1.0"\n\n[targets.slow]\nkind = "lib"\n' > r5/slow/mcpp.toml
printf 'export module rp_slow;\nexport int slow_v() { return 3; }\n' > r5/slow/src/slow.cppm
cat > r5/slow/build.mcpp <<'EOF'
import mcpp;
#include <string>
int main() {
    const std::string prefix = std::string(mcpp::out_dir()) + "/slow-install";
    mcpp::action a;
    a.id   = "slow:install";
    a.role = mcpp::roles::prepare;
    a.arg("python3").arg("-c")
     .arg("import os, sys, time; time.sleep(6); os.makedirs(sys.argv[1], exist_ok=True); open(os.path.join(sys.argv[1], \"ready\"), \"w\").close()")
     .arg(prefix.c_str())
     .output((prefix + ".stamp").c_str())
     .output_dir(prefix.c_str())
     .submit();
    return 0;
}
EOF
cat > r5/app/mcpp.toml <<'EOF'
[package]
name = "app"
version = "0.1.0"

[dependencies]
slow = { path = "../slow" }

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return R5_BROKEN; }\n' > r5/app/src/main.cpp
cd r5/app
python3 - "$MCPP" <<'PY' > r5.log 2>&1 || fail "R5" r5.log
import subprocess, sys, time
start = time.monotonic()
p = subprocess.Popen([sys.argv[1], "build"], stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, text=True, bufsize=1)
first_error = None
lines = []
for line in p.stdout:
    lines.append(line.rstrip("\n"))
    if first_error is None and line.startswith("error: build failed"):
        first_error = time.monotonic() - start
code = p.wait()
end = time.monotonic() - start
print("\n".join(lines))
print(f"exit {code}; error at {first_error}; end at {end:.1f}")
assert code != 0, "the broken build succeeded"
assert first_error is not None, "no `error: build failed` line"
assert end - first_error >= 3.0, \
    f"the failure was reported {end - first_error:.1f}s before mcpp exited; it waited for ninja"
PY

echo "PASS: 842_the_build_reports_each_step_once"
