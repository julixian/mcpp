#!/usr/bin/env bash
# requires: unix-shell
# 862 -- `mcpp run -q 2>/dev/null` produces exactly the program's stdout, byte
# for byte (output streams plan 2026-10-01, §6 R1 and R3; criterion R-A).
#
# Until 2026.9.30.2 `mcpp run` wrote its status lines to standard output, and
# wrote a bare blank line after the `Running` line whether or not that line
# had been written. `mcpp run -q > file` therefore began with an empty line
# (`od -c`: `\n` `O` `U` `T` `\n`), and without `-q` the file began with the
# build's status lines: standard output was not the program's.
#
# The program writes to both streams, so the test can tell a status line that
# reached the wrong stream from one that did not exist.
#
# Criteria:
#   A. `mcpp run -q` (the full path: it builds) leaves standard output equal to
#      the program's own, and standard error equal to the program's own: under
#      `-q` neither the `Running` line nor the blank line after it is written.
#   B. The same for the second `mcpp run -q`, which takes the fast path.
#   C. Without `-q`, standard output is still exactly the program's. The
#      `Running` line and the blank line after it are on standard error, and
#      the program's own standard error follows them.
#   D. The program's first byte is the first byte of standard output, and its
#      last, which has no line end, is the last: nothing is added in front of or
#      behind the program's output.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
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
cat > p/src/main.cpp <<'EOF'
#include <cstdio>
int main() {
    std::fputs("OUT one\n", stdout);
    std::fputs("ERR one\n", stderr);
    std::fputs("OUT two\n", stdout);
    std::fputs("ERR two\n", stderr);
    std::fputs("OUT no-line-end", stdout);
    return 0;
}
EOF
cd p

# ── A ──────────────────────────────────────────────────────────────────────
"$MCPP" run -q > a.out 2> a.err || fail "A: mcpp run -q failed" a.err
BIN=$(find target -path "*/bin/*" -name "p$EXE" -type f | head -1)
[ -n "$BIN" ] || fail "A: the program was not built"
"$BIN" > direct.out 2> direct.err
cmp -s a.out direct.out || fail "A: standard output is not exactly the program's" a.out direct.out
cmp -s a.err direct.err || fail "A: standard error is not exactly the program's" a.err direct.err

# ── B ──────────────────────────────────────────────────────────────────────
"$MCPP" run -q > b.out 2> b.err || fail "B: the second mcpp run -q failed" b.err
cmp -s b.out direct.out || fail "B: standard output is not exactly the program's" b.out direct.out
cmp -s b.err direct.err || fail "B: standard error is not exactly the program's" b.err direct.err

# ── C ──────────────────────────────────────────────────────────────────────
"$MCPP" run > c.out 2> c.err || fail "C: mcpp run failed" c.err
cmp -s c.out direct.out || fail "C: standard output carries more than the program's" c.out direct.out
grep -q 'Running `' c.err || fail "C: no Running line on standard error" c.err
# The line after `Running` is the blank one that separates it from the program.
blank=$(awk '/Running `/ { getline nxt; print (nxt == "") ? "blank" : "not blank"; exit }' c.err)
[ "$blank" = blank ] || fail "C: the line after Running is not blank" c.err
tail -n 2 c.err | cmp -s - direct.err || fail "C: the program's own standard error does not end the stream" c.err

# ── D ──────────────────────────────────────────────────────────────────────
[ "$(head -c 7 a.out)" = "OUT one" ] || fail "D: standard output does not begin with the program's first byte" a.out
[ "$(tail -c 15 a.out)" = "OUT no-line-end" ] || fail "D: standard output does not end with the program's last byte" a.out

echo "OK: mcpp run -q writes exactly the program's output; status is on standard error"
