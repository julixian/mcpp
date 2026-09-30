#!/usr/bin/env bash
# requires: unix-shell
# 863 -- a `mcpp run` whose planning or build failed exits 101, and a program's
# own status passes through (output streams plan 2026-10-01, §6 R2, D7;
# criterion R-B).
#
# Until 2026.9.30.2 a failed build and a program that returns 1 both exited 1,
# so a script that ran `mcpp run -q` could not tell a compile error from a
# program that failed. 101 is the status Cargo gives a failed `cargo run`
# build. The program's own status is not changed, and `build`, `test` and
# `pack` keep the statuses they had.
#
# Criteria:
#   A. A compile error exits 101: on the fast path (an edit of a header, which
#      ninja sees and which does not change the graph's shape), on the path
#      that builds (an edit of the main source), and in a first run.
#   B. A manifest that cannot be read, which fails the planning, exits 101.
#   C. A program that returns 1 exits 1, one that returns 3 exits 3: the
#      program's status passes through unchanged.
#   D. A program that cannot be started keeps the status of the refusal, 126
#      (found and not executable): it is not folded into 101.
#   E. `mcpp build` with the same compile error still exits 1, and `mcpp test`
#      with a failing test still exits 1.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
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
# The program's status comes from a header, so that an edit of the header is a
# change ninja sees and the fast path does not: the first compile error below
# is reached through `try_fast_run`.
cat > p/src/main.cpp <<'EOF'
#include <cstdlib>
#include "impl.h"
int main(int argc, char** argv) { return status_of(argc, argv); }
EOF
cat > p/src/impl.good <<'EOF'
inline int status_of(int argc, char** argv) { return argc > 1 ? std::atoi(argv[1]) : 0; }
EOF
cat > p/src/impl.bad <<'EOF'
inline int status_of(int, char**) { return this_name_is_not_declared; }
EOF
cat > p/tests/failing.cpp <<'EOF'
int main() { return 1; }
EOF
cd p
cp src/impl.good src/impl.h
cp src/main.cpp main.cpp.good

# Runs `mcpp run -q` with its output in `$1.out` and `$1.err`, and sets `rc`.
run_q() {
    set +e
    "$MCPP" run -q "${@:2}" > "$1.out" 2> "$1.err"
    rc=$?
    set -e
}

# ── C: a program's own status passes through ──────────────────────────────
run_q c0
[ "$rc" = 0 ] || fail "C: a program that returns 0 exited $rc" c0.err
run_q c1 -- 1
[ "$rc" = 1 ] || fail "C: a program that returns 1 exited $rc" c1.err
run_q c3 -- 3
[ "$rc" = 3 ] || fail "C: a program that returns 3 exited $rc" c3.err
BIN=$(find target -path "*/bin/*" -name "p$EXE" -type f | head -1)
[ -n "$BIN" ] || fail "C: the program was not built"

# ── D: a refused start keeps the refusal's status ─────────────────────────
chmod -x "$BIN"
run_q d
chmod +x "$BIN"
[ "$rc" = 126 ] || fail "D: a program that cannot be executed exited $rc, not 126" d.err
[ -s d.err ] || fail "D: a refused start wrote no reason to standard error"

# ── A: a compile error, on the fast path and on the path that builds ──────
cp src/impl.bad src/impl.h
run_q a1
[ "$rc" = 101 ] || fail "A: a compile error after a build (the fast path) exited $rc, not 101" a1.err
grep -q 'this_name_is_not_declared' a1.err || fail "A: the diagnostic is not shown" a1.err
cp src/impl.good src/impl.h
run_q a2 -- 1
[ "$rc" = 1 ] || fail "A: after the fix, a program that returns 1 exited $rc, not 1" a2.err
# The path that builds: the main source itself has the error.
printf 'int main() { return this_name_is_not_declared; }\n' > src/main.cpp
run_q a3
[ "$rc" = 101 ] || fail "A: a compile error in the main source exited $rc, not 101" a3.err
# A first run, with no build before it.
rm -rf target
run_q a4
[ "$rc" = 101 ] || fail "A: a compile error in a first run exited $rc, not 101" a4.err

# ── E: build and test keep their statuses ─────────────────────────────────
set +e
"$MCPP" build -q > e1.out 2> e1.err; rc_build=$?
set -e
[ "$rc_build" = 1 ] || fail "E: mcpp build with a compile error exited $rc_build, not 1" e1.err
cp main.cpp.good src/main.cpp
set +e
"$MCPP" test -q > e2.out 2> e2.err; rc_test=$?
set -e
[ "$rc_test" = 1 ] || fail "E: mcpp test with a failing test exited $rc_test, not 1" e2.err

# ── B: a manifest that cannot be read ─────────────────────────────────────
cp mcpp.toml mcpp.toml.good
printf '[package\nname = "p"\n' > mcpp.toml
run_q b
[ "$rc" = 101 ] || fail "B: an unreadable manifest exited $rc, not 101" b.err
[ -s b.err ] || fail "B: an unreadable manifest wrote no reason to standard error"
set +e
"$MCPP" build -q > b2.out 2> b2.err; rc_build=$?
set -e
[ "$rc_build" != 101 ] || fail "B: mcpp build adopted the status of mcpp run" b2.err
[ "$rc_build" != 0 ] || fail "B: mcpp build accepted an unreadable manifest" b2.err
cp mcpp.toml.good mcpp.toml

echo "OK: a failed build exits 101; the program's own status passes through"
