#!/usr/bin/env bash
# requires: unix-shell
# 832_the_fast_path_resumes_after_an_edit.sh -- #734.
#
# The project fast path compares every source with build.ninja's time. A build
# after an edit is planned in full, confirms the graph, and leaves an unchanged
# build.ninja unwritten; its time then stayed older than the edited source, and
# every later build declined the fast path until the graph's text changed.
#
#   R1  after an edit and one build, the next build is replayed by the fast
#       path (under -v: no "declined", no "Resolving toolchain");
#   R2  the replay still sees the next edit: the program prints the new value.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p src
printf '[package]\nname = "resume832"\nversion = "0.1.0"\n' > mcpp.toml
printf '#include <cstdio>\nint main() { std::puts("one"); return 0; }\n' > src/main.cpp
"$MCPP" build > b0.log 2>&1 || fail "the first build failed" b0.log

sleep 1.1
printf '#include <cstdio>\nint main() { std::puts("two"); return 0; }\n' > src/main.cpp
"$MCPP" build > b1.log 2>&1 || fail "the build after the edit failed" b1.log

# R1
"$MCPP" build -v > r1.log 2>&1 || fail "R1: the build failed" r1.log
if grep -q "declined" r1.log || grep -q "Resolving toolchain" r1.log; then
    fail "R1: the build after a confirmed edit was not replayed by the fast path" r1.log
fi

# R2
sleep 1.1
printf '#include <cstdio>\nint main() { std::puts("three"); return 0; }\n' > src/main.cpp
"$MCPP" run > r2.log 2>&1 || fail "R2: the run failed" r2.log
grep -qx three r2.log || fail "R2: the next edit did not reach the program" r2.log

echo "OK"
