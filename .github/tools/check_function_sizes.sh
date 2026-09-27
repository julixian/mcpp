#!/usr/bin/env bash
#
# Guard: no function under the prepare.cppm decomposition grows past ~400
# lines (mcpp-community/mcpp#722, T6 of the 2026-09-27 round).
#
# WHY
#
# check_file_lengths.sh caps each FILE at 2,500 lines. It says nothing about
# a single FUNCTION inside a file that stays under the cap while one phase
# function alone climbs back past a thousand lines and closes back over the
# ~180-local shape prepare.cppm was split to remove in the first place (see
# that script's own header, and the layout comment atop src/build/prepare.cppm).
# #722 split the seven functions that had grown past ~400 lines into
# sub-steps named after the sections their own banners already used; this
# gate is what keeps a phase function from quietly growing back into one.
#
# THE RULE
#
# Every function defined in a file directly under src/build/prepare/ (or in
# src/build/prepare.cppm itself) stays at or under LINE_THRESHOLD lines, as
# clang-tidy's readability-function-size check counts them (its own count,
# not a text-heuristic line counter -- a brace-counting or regex-based
# stand-in cannot tell a function's extent from a `{`/`}` pair inside a
# string literal or a designated initializer, both common in this codebase's
# std::format calls and manifest structs; see .agents/docs/
# 2026-09-27-eight-reports-by-home-and-one-optimisation-plan.md §8).
#
# WHAT THIS NEEDS
#
# A compile database that names BMIs explicitly (-fmodule-file=...), which
# only a build actually produces: `mcpp build --toolchain llvm@22.1.8` writes
# compile_commands.json at the project root. This script does not build it --
# the caller (a developer, or the CI step beside this one) runs that build
# first, the same division check_file_lengths.sh has none of because it reads
# the tree directly.
#
# clang-tidy itself is not part of the plain xim:llvm payload mcpp resolves
# for `--toolchain llvm@...` (measured: xim-x-llvm/22.1.8/bin has clang,
# clang-scan-deps and the LLVM binutils, no clang-tidy). It ships in the
# sibling package `xim:llvm-tools` at the same version -- resolved and
# searched for under the xlings package store; install it with
# `xlings install xim:llvm-tools@<version that matches your llvm toolchain>`
# if this script cannot find it.
#
# Usage: bash .github/tools/check_function_sizes.sh [repo_dir]

set -uo pipefail

REPO_DIR="${1:-$(pwd)}"
cd "$REPO_DIR" || { echo "FAIL: cannot cd to $REPO_DIR" >&2; exit 1; }

LINE_THRESHOLD=400
DIR="src/build/prepare"
PRIMARY="src/build/prepare.cppm"
CDB="compile_commands.json"

[ -d "$DIR" ] || { echo "FAIL: $DIR does not exist -- this guard has gone stale" >&2; exit 1; }

if [ ! -f "$CDB" ]; then
    cat >&2 <<EOF
FAIL: $CDB does not exist.
  This check reads clang-tidy's own function boundaries, which needs a
  compile database that names every imported module's BMI explicitly.
  Produce one first:
      mcpp build --toolchain llvm@22.1.8
  (any installed LLVM row works; the database is written at the project
  root regardless of the row's exact version).
EOF
    exit 1
fi

# Locate clang-tidy. It is not in the plain xim:llvm payload (see the header
# comment); look for the sibling xim:llvm-tools payload under either xlings
# store layout this machine may use, preferring a version that matches an
# xim:llvm payload actually installed (compile_commands.json was built with
# one of those), and falling back to any clang-tidy the store has.
find_clang_tidy() {
    local roots=(
        "$HOME/.mcpp/registry/data/xpkgs"
        "$HOME/.xlings/data/xpkgs"
    )
    local llvm_versions=()
    for root in "${roots[@]}"; do
        [ -d "$root/xim-x-llvm" ] || continue
        while IFS= read -r v; do llvm_versions+=("$v"); done \
            < <(find "$root/xim-x-llvm" -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null)
    done
    for root in "${roots[@]}"; do
        for v in "${llvm_versions[@]}"; do
            local cand="$root/xim-x-llvm-tools/$v/bin/clang-tidy"
            [ -x "$cand" ] && { echo "$cand"; return 0; }
        done
    done
    for root in "${roots[@]}"; do
        local cand
        cand=$(find "$root/xim-x-llvm-tools" -maxdepth 3 -type f -name clang-tidy 2>/dev/null | sort -V | tail -1)
        [ -n "$cand" ] && [ -x "$cand" ] && { echo "$cand"; return 0; }
    done
    return 1
}

CLANG_TIDY="$(find_clang_tidy)" || {
    cat >&2 <<EOF
FAIL: no clang-tidy found under an xlings package store.
  Install the sibling of your llvm toolchain, e.g.:
      xlings install xim:llvm-tools@22.1.8
EOF
    exit 1
}

# The files this database actually has entries for, restricted to the
# decomposition's own directory (plus the primary interface, if it is ever
# given its own compiled entry point -- it has none today, since it defines
# only declarations and inline exports; the loop below tolerates that).
mapfile -t FILES < <(python3 - "$CDB" "$DIR" "$PRIMARY" <<'PYEOF'
import json, sys
cdb_path, dirname, primary = sys.argv[1], sys.argv[2], sys.argv[3]
with open(cdb_path) as f:
    entries = json.load(f)
seen = set()
for e in entries:
    path = e["file"]
    if f"/{dirname}/" in path or path.endswith(f"/{primary}"):
        seen.add(path)
for p in sorted(seen):
    print(p)
PYEOF
)

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "FAIL: $CDB has no entry under $DIR -- was it built with a matching source tree?" >&2
    exit 1
fi

echo "checking ${#FILES[@]} file(s) with $CLANG_TIDY (LineThreshold=$LINE_THRESHOLD)..."

OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

"$CLANG_TIDY" \
    --checks='-*,readability-function-size' \
    --warnings-as-errors='*' \
    --config="{CheckOptions: {readability-function-size.LineThreshold: '$LINE_THRESHOLD'}}" \
    -p "$REPO_DIR" \
    "${FILES[@]}" > "$OUT" 2>&1
rc=$?

# Only findings inside the decomposition's own directory gate the build: a
# bundled third-party header (e.g. modules/libs/src/json/json.hpp) reached
# through one of these files' imports is not this decomposition's to fix.
relevant=$(grep "readability-function-size" "$OUT" | grep -F -e "/$DIR/" -e "/$(basename "$PRIMARY")" || true)

if [ -n "$relevant" ]; then
    echo "$relevant" >&2
    echo >&2
    echo "FAIL: function(s) over $LINE_THRESHOLD lines under $DIR -- see above." >&2
    echo "  Split at the sub-section boundaries its own banners already name" >&2
    echo "  (mcpp-community/mcpp#722's own method), the way phase13_finish," >&2
    echo "  phase4b_graph_worklist, phase6_features_and_host_tools and" >&2
    echo "  phase9_target_side were split." >&2
    exit 1
fi

if [ "$rc" -ne 0 ]; then
    echo "FAIL: clang-tidy exited $rc with no readability-function-size finding under $DIR" >&2
    echo "  (a diagnostic tool problem, not a function-size one -- see the log):" >&2
    cat "$OUT" >&2
    exit 1
fi

echo "ok: no function under $DIR (or $PRIMARY) exceeds $LINE_THRESHOLD lines"
exit 0
