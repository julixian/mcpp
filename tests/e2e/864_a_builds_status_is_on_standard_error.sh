#!/usr/bin/env bash
# requires: unix-shell
# 864 -- a command's narration is on standard error, and standard output is
# left to its result (output streams plan 2026-10-01, §6 R3; criterion R-C).
#
# Until 2026.9.30.2 `status`, `info`, `Finished`, the progress bars and the
# status row were written to standard output, and only warnings and errors to
# standard error. `mcpp build | tee log` recorded the steps, and a pipe that
# was to receive a result received the progress of the command as well. The
# narration now goes to standard error on every command, which is where Cargo
# puts it, and where a pipe or a redirection does not capture it.
#
# The test reads captured streams, not a terminal: a pipe is what a script has.
#
# Criteria:
#   A. On a project without warnings, `mcpp build >/dev/null` still shows
#      `Compiling` and `Finished`, on standard error.
#   B. `mcpp build 2>/dev/null` writes nothing to standard output, on the build
#      that compiles, on the one that has nothing to do, and under `--verbose`.
#   C. `mcpp build -q` writes nothing to either stream.
#   D. `mcpp build 2>&1 | tee log` records the steps: the migration for a script
#      that read them from standard output.
#   E. `mcpp new` and `mcpp clean` narrate on standard error too.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

mkdir -p p/src
cat > p/mcpp.toml <<'EOF'
[package]
name    = "p"
version = "0.1.0"

[targets.p]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > p/src/main.cpp
cd p

# ── A ──────────────────────────────────────────────────────────────────────
"$MCPP" build > /dev/null 2> a.err || fail "A: the build failed" a.err
grep -q 'Compiling p v0.1.0' a.err || fail "A: no Compiling line on standard error" a.err
grep -q 'Finished ' a.err || fail "A: no Finished line on standard error" a.err
grep -q '^warning' a.err && fail "A: the project is not warning-free" a.err

# ── B ──────────────────────────────────────────────────────────────────────
printf 'int main() { return 1 - 1; }\n' > src/main.cpp
"$MCPP" build > b1.out 2> b1.err || fail "B: the build that compiles failed" b1.err
[ ! -s b1.out ] || fail "B: the build that compiles wrote to standard output" b1.out
grep -q 'Compiling p v0.1.0' b1.err || fail "B: the build that compiles names nothing on standard error" b1.err
"$MCPP" build > b2.out 2> b2.err || fail "B: the build with nothing to do failed" b2.err
[ ! -s b2.out ] || fail "B: the build with nothing to do wrote to standard output" b2.out
grep -q 'Finished ' b2.err || fail "B: the build with nothing to do has no Finished line on standard error" b2.err
printf 'int main() { return 2 - 2; }\n' > src/main.cpp
"$MCPP" build --verbose > b3.out 2> b3.err || fail "B: the verbose build failed" b3.err
[ ! -s b3.out ] || fail "B: --verbose wrote to standard output" b3.out
[ -s b3.err ] || fail "B: --verbose wrote nothing to standard error"
# A pipe receives nothing of the build.
printf 'int main() { return 3 - 3; }\n' > src/main.cpp
piped=$("$MCPP" build 2>/dev/null)
[ -z "$piped" ] || fail "B: a pipe received: $piped"

# ── C ──────────────────────────────────────────────────────────────────────
printf 'int main() { return 4 - 4; }\n' > src/main.cpp
"$MCPP" build -q > c.out 2> c.err || fail "C: the quiet build failed" c.err
[ ! -s c.out ] || fail "C: -q wrote to standard output" c.out
[ ! -s c.err ] || fail "C: -q wrote to standard error" c.err

# ── D ──────────────────────────────────────────────────────────────────────
printf 'int main() { return 5 - 5; }\n' > src/main.cpp
"$MCPP" build 2>&1 | tee d.log > /dev/null
grep -q 'Compiling p v0.1.0' d.log || fail "D: 2>&1 | tee did not record the steps" d.log
grep -q 'Finished ' d.log || fail "D: 2>&1 | tee did not record Finished" d.log

# ── E ──────────────────────────────────────────────────────────────────────
cd "$TMP"
"$MCPP" new q > e1.out 2> e1.err || fail "E: mcpp new failed" e1.err
[ ! -s e1.out ] || fail "E: mcpp new wrote to standard output" e1.out
grep -q 'Created' e1.err || fail "E: mcpp new does not say on standard error what it created" e1.err
cd p
"$MCPP" clean > e2.out 2> e2.err || fail "E: mcpp clean failed" e2.err
[ ! -s e2.out ] || fail "E: mcpp clean wrote to standard output" e2.out
grep -q 'Cleaned' e2.err || fail "E: mcpp clean does not say on standard error what it cleaned" e2.err

echo "OK: narration is on standard error; standard output is left to the result"
