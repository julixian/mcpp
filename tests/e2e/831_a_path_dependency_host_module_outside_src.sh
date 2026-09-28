#!/usr/bin/env bash
# requires: unix-shell
# 831_a_path_dependency_host_module_outside_src.sh -- #734.
#
# A path dependency's host module is compiled into the consumer's build
# program, which no edge of the consumer's build.ninja names. The project fast
# path therefore has to see an edit to it itself. It swept only the
# dependency's `src/`, so a feature unit elsewhere -- mcpp-plugins keeps its
# members in `deps/`, `rules/`, `dist/` and `tools/` -- was replayed as "no
# work" after an edit.
#
#   S1  an edit to `rules/hm.cppm` of the dependency re-runs the build program,
#       which reads the new value;
#   S2  a newer file inside a package nested in the dependency (a test fixture
#       with its own mcpp.toml) does not decline the fast path.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p plug/src plug/rules plug/tests/fixture/src app/src
cat > plug/mcpp.toml <<'EOF'
[package]
namespace = "probe"
name      = "plug831"
version   = "0.1.0"

[build]
sources = ["src/plug831.cppm"]

[features.hm]
sources = ["rules/hm.cppm"]
EOF
printf 'export module probe.plug831;\nexport int plug_root() { return 0; }\n' > plug/src/plug831.cppm
printf 'export module probe.hm831;\nexport inline int value() { return 1; }\n' > plug/rules/hm.cppm
printf '[package]\nname = "fixture831"\nversion = "0.1.0"\n' > plug/tests/fixture/mcpp.toml
printf 'int main() { return 0; }\n' > plug/tests/fixture/src/main.cpp

cat > app/mcpp.toml <<'EOF'
[package]
name    = "app831"
version = "0.1.0"

[build-dependencies.probe]
plug831 = { path = "../plug", features = ["hm"], host-module = true }
EOF
cat > app/build.mcpp <<'EOF'
import std;
import mcpp;
import probe.hm831;
int main() {
    if (value() != 1) { std::println("host module value {}", value()); return 1; }
    return 0;
}
EOF
printf 'int main() { return 0; }\n' > app/src/main.cpp
cd app

"$MCPP" build > b1.log 2>&1 || fail "the first build failed" b1.log
"$MCPP" build -v > b1b.log 2>&1 || fail "the second build failed" b1b.log

# S1
sleep 1.1
printf 'export module probe.hm831;\nexport inline int value() { return 2; }\n' > ../plug/rules/hm.cppm
if "$MCPP" build > s1.log 2>&1; then
    fail "S1: the build after an edit to the dependency's rules/hm.cppm succeeded without re-running the build program" s1.log
fi
grep -q "host module value 2" s1.log || fail "S1: the build program did not read the edited host module" s1.log

# S2
printf 'export module probe.hm831;\nexport inline int value() { return 1; }\n' > ../plug/rules/hm.cppm
"$MCPP" build > s2a.log 2>&1 || fail "S2: the build after restoring the host module failed" s2a.log
sleep 1.1
touch ../plug/tests/fixture/src/main.cpp
"$MCPP" build -v > s2.log 2>&1 || fail "S2: the build failed" s2.log
if grep -q "declined" s2.log; then
    fail "S2: the fast path declined after a file of a package nested in the dependency changed" s2.log
fi
if grep -q "Resolving toolchain" s2.log; then
    fail "S2: the build was planned instead of replayed" s2.log
fi

echo "OK"
