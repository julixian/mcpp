#!/usr/bin/env bash
#
# Guard: no file under the prepare.cppm decomposition grows past 2,500 lines.
#
# WHY
#
# prepare.cppm was 16,105 lines: one exported function, prepare_build, ~85%
# of the file, ~180 top-level locals sharing a stack frame. It was split into
# a primary interface (src/build/prepare.cppm), an implementation partition
# (src/build/prepare/state.cppm) and twelve implementation units, each phase
# a function taking PrepareState& instead of closing over the old locals
# directly — see the layout comment at the top of prepare.cppm and
# .agents/docs/2026-09-27-mcpp-2026.9.27.1-ecosystem-plan.md §4.
#
# A size cap with no gate is a target nobody re-checks. The decomposition's
# whole point was to keep any one file's compile from blocking on the rest —
# a phase file that quietly grows back to several thousand lines is the same
# defect it fixed, arrived at one small commit at a time. This fails the
# build the day that happens, at the commit that did it, rather than leaving
# it for the next person who tries to read the file.
#
# THE RULE
#
# Every file directly under src/build/prepare/, plus src/build/prepare.cppm
# itself, stays at or under 2,500 lines. There is no per-file waiver: a file
# that needs one is a file that needs splitting the way graph.cpp/graph_load.cpp
# and toolchain.cpp/toolchain_decision.cpp already were.
#
# Usage: bash .github/tools/check_file_lengths.sh [repo_dir]

set -uo pipefail

REPO_DIR="${1:-$(pwd)}"
cd "$REPO_DIR" || { echo "FAIL: cannot cd to $REPO_DIR" >&2; exit 1; }

LIMIT=2500
PRIMARY="src/build/prepare.cppm"
DIR="src/build/prepare"

[ -f "$PRIMARY" ] || { echo "FAIL: $PRIMARY does not exist — this guard has gone stale" >&2; exit 1; }
[ -d "$DIR" ] || { echo "FAIL: $DIR does not exist — this guard has gone stale" >&2; exit 1; }

fail=0
checked=0

check_one() {
    file="$1"
    n=$(wc -l < "$file")
    checked=$((checked + 1))
    if [ "$n" -gt "$LIMIT" ]; then
        echo "FAIL: $file is $n lines (limit $LIMIT)" >&2
        fail=1
    fi
}

check_one "$PRIMARY"
while IFS= read -r f; do
    check_one "$f"
done < <(find "$DIR" -maxdepth 1 -type f \( -name '*.cppm' -o -name '*.cpp' \) | sort)

if [ "$fail" = 1 ]; then
    cat >&2 <<EOF

  A file over the limit needs splitting, not a raised limit: cut it at a
  phase boundary (see the layout comment at the top of $PRIMARY) the way
  P4 (graph_load.cpp/graph.cpp) and P1+P2+P5 (toolchain.cpp/
  toolchain_decision.cpp) already were. A phase whose closures escape into a
  later phase needs its captures promoted to PrepareState members first —
  see state.cppm's own comments for the pattern.
EOF
    exit 1
fi

echo "ok: $checked file(s) under $PRIMARY / $DIR/, all at or under $LIMIT lines"
exit 0
