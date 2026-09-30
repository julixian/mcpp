#!/usr/bin/env bash
# requires: unix-shell
# 865 -- `mcpp test` narrates on standard error and reports its results on
# standard output (output streams plan 2026-10-01, §6 R3, and §11 item 5).
#
# The steps of a test run (`Resolving`, `Compiling`, `Running`) are narration,
# like a build's. The report is the command's result: each test's verdict, the
# output of a test that failed, the `test result` line and, for a workspace,
# the `workspace result` line. Cargo prints libtest's report on standard output
# for the same reason, and a script that redirects a test run to a file
# expects the verdicts in it.
#
# Criteria:
#   A. A run whose tests pass: standard output has each verdict and the
#      `test result ok.` line, and none of the steps; standard error has the
#      steps, and none of the verdicts.
#   B. A run with a failing test exits 1: standard output has its verdict, its
#      output and the `failures:` list; standard error has the failed summary.
#   C. `mcpp test --list` prints the names on standard output alone.
#   D. `mcpp test --workspace`: `workspace result ok.` is on standard output
#      and the members' steps are on standard error.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

mkdir -p p/src p/tests
cat > p/mcpp.toml <<'EOF'
[package]
name    = "p"
version = "0.1.0"

[targets.p]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > p/src/main.cpp
cat > p/tests/t_ok.cpp <<'EOF'
#include <cstdio>
int main() { std::puts("output of the passing test"); return 0; }
EOF
cd p

# ── A ──────────────────────────────────────────────────────────────────────
"$MCPP" test > a.out 2> a.err || fail "A: mcpp test failed" a.out a.err
grep -q 't_ok ... ok' a.out || fail "A: the verdict is not on standard output" a.out
grep -q 'test result ok\.' a.out || fail "A: the test result line is not on standard output" a.out
grep -qE 'Compiling|Resolving|Resolved' a.out && fail "A: a step is on standard output" a.out
grep -q 'Compiling' a.err || fail "A: the steps are not on standard error" a.err
grep -q 'test result' a.err && fail "A: the test result line is on standard error" a.err
grep -q '\.\.\. ok' a.err && fail "A: a verdict is on standard error" a.err

# ── B ──────────────────────────────────────────────────────────────────────
cat > tests/t_bad.cpp <<'EOF'
#include <cstdio>
int main() { std::puts("output of the failing test"); return 1; }
EOF
set +e
"$MCPP" test > b.out 2> b.err; rc=$?
set -e
[ "$rc" = 1 ] || fail "B: mcpp test with a failing test exited $rc, not 1" b.out b.err
grep -q 't_bad ... FAIL' b.out || fail "B: the failing verdict is not on standard output" b.out
grep -q 'output of the failing test' b.out || fail "B: the failing test's output is not on standard output" b.out
grep -q 'failures:' b.out || fail "B: the failures list is not on standard output" b.out
grep -q 'test result: FAILED' b.err || fail "B: the failed summary is not on standard error" b.err
grep -qE 'Compiling|Resolving' b.out && fail "B: a step is on standard output" b.out
rm tests/t_bad.cpp

# ── C ──────────────────────────────────────────────────────────────────────
"$MCPP" test --list > c.out 2> c.err || fail "C: mcpp test --list failed" c.err
grep -q '^t_ok$' c.out || fail "C: the name is not on standard output" c.out
grep -q 't_ok' c.err && fail "C: a name is on standard error" c.err

# ── D ──────────────────────────────────────────────────────────────────────
cd "$TMP"
mkdir -p ws/a/src ws/a/tests ws/b/src ws/b/tests
cat > ws/mcpp.toml <<'EOF'
[workspace]
members = ["a", "b"]
EOF
for m in a b; do
    cat > ws/$m/mcpp.toml <<EOF
[package]
name    = "$m"
version = "0.1.0"

[targets.$m]
kind = "bin"
main = "src/main.cpp"
EOF
    printf 'int main() { return 0; }\n' > ws/$m/src/main.cpp
    printf 'int main() { return 0; }\n' > ws/$m/tests/t_$m.cpp
done
cd ws
"$MCPP" test --workspace > d.out 2> d.err || fail "D: mcpp test --workspace failed" d.out d.err
grep -q 'workspace result ok\.' d.out || fail "D: the workspace result line is not on standard output" d.out
grep -q 'test result ok\.' d.out || fail "D: a member's test result line is not on standard output" d.out
grep -qE 'Compiling|Resolving|Resolved|Workspace' d.out && fail "D: a step is on standard output" d.out
grep -q 'Compiling' d.err || fail "D: the steps are not on standard error" d.err
grep -q 'workspace result' d.err && fail "D: the workspace result line is on standard error" d.err

echo "OK: mcpp test narrates on standard error and reports on standard output"
