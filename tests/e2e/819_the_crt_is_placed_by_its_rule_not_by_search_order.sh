#!/usr/bin/env bash
# requires: python3
# 819_the_crt_is_placed_by_its_rule_not_by_search_order.sh -- the post-link
# half of the runtime placement resolver (SPEC-006 §3.7.1, the 2026-09-28
# design WS1), on every host.
#
# `mcpp place-dlls` reads a PE program's imports from the file and runs on
# whatever host builds, so its rule for the MSVC C++ runtime is observable
# here without a Windows toolchain: the program, the toolset's runtime and two
# dependency directories are synthesised PE images with VERSIONINFO. Until
# 2026.9.28.2 the edge placed `vcruntime140.dll` from the first search
# directory that offered it, whatever the contract and whatever its version:
# that is how Qt's copy of the runtime, older than the toolset that compiled
# the program, came to sit beside it (review 2026-09-28 §2.1).
#
#   L1  host-coupled (`--crt system`): no copy of the runtime is placed.
#   L2  toolchain-coupled with the set already placed by the plan: the
#       dependency's older copy is neither placed nor warned about per link.
#   L3  a runtime name the plan did not see (`--crt static`): the toolset's
#       whole set is placed over an older complete set, and the dependency's
#       copy is stated once as a packaging fault.
#   L4  the same with a strictly newer complete set: that set is placed, with
#       one note saying so (D1).
#   L5  a newer but incomplete set does not replace the toolset's.
#   L6  a graph written before the rule (no `--crt`) keeps search order.
set -e
MKPE="$(cd "$(dirname "$0")" && pwd)/_synth_pe.py"

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

SET="vcruntime140.dll vcruntime140_1.dll msvcp140.dll"
mkset() {   # mkset <dir> <version> [names...]
    local dir="$1" ver="$2"; shift 2
    mkdir -p "$dir"
    for n in "${@:-$SET}"; do python3 "$MKPE" "$dir/$n" "$ver"; done
}
mkset toolset  14.44.35112.1 $SET
mkset dep-old  14.29.30139.0 $SET
mkset dep-new  14.51.36231.0 $SET
mkset dep-part 14.51.36231.0 vcruntime140.dll
TOOLSET="$PWD/toolset"

leg() {    # leg <name>: a fresh program directory
    rm -rf "$1"; mkdir -p "$1"
    python3 "$MKPE" "$1/app.exe" program
}
place() {  # place <leg> <args...>: runs the edge as ninja does, from the leg
    local d="$1"; shift
    (cd "$d" && "$MCPP" place-dlls --output app.exe.dlls --depfile app.exe.dlls.d \
        "$@" > place.log 2>&1) || fail "place-dlls failed in $d" "$d/place.log"
}
advice() { cat "$1/.mcpp-advice/app.exe.dlls.advice" 2>/dev/null || true; }

# L1
leg l1
place l1 --crt system app.exe "$PWD/dep-old"
[[ ! -e l1/vcruntime140.dll ]] || fail "L1: host-coupled placed a copy of the runtime" l1/place.log
echo "ok: L1 host-coupled places no copy of the runtime"

# L2
leg l2
cp toolset/* l2/
place l2 --crt carry --toolset-crt "$TOOLSET" app.exe "$PWD/dep-old"
cmp -s l2/vcruntime140.dll toolset/vcruntime140.dll \
    || fail "L2: the toolset's copy was replaced" l2/place.log
advice l2 | grep -q vcruntime140 \
    && fail "L2: a per-link statement about the dependency's runtime copy" <(advice l2)
echo "ok: L2 the plan's set stays, and nothing is said per link"

# L3
leg l3
place l3 --crt static --toolset-crt "$TOOLSET" app.exe "$PWD/dep-old"
for n in $SET; do
    cmp -s "l3/$n" "toolset/$n" || fail "L3: $n is not the toolset's copy" l3/place.log <(advice l3)
done
advice l3 | grep -q "does not carry the compiler's runtime" \
    || fail "L3: the dependency's runtime copy was not stated as a packaging fault" <(advice l3)
[[ "$(advice l3 | grep -c "ships the MSVC C++ runtime")" -eq 1 ]] \
    || fail "L3: the packaging fault was not stated exactly once" <(advice l3)
echo "ok: L3 the toolset's whole set is placed over an older one, stated once"

# L4
leg l4
place l4 --crt static --toolset-crt "$TOOLSET" app.exe "$PWD/dep-new"
for n in $SET; do
    cmp -s "l4/$n" "dep-new/$n" || fail "L4: $n is not the newer set's copy" l4/place.log <(advice l4)
done
advice l4 | grep -q "newer than the toolset's" \
    || fail "L4: the newer set was placed without saying so" <(advice l4)
echo "ok: L4 a newer complete set is placed, and said"

# L5
leg l5
place l5 --crt static --toolset-crt "$TOOLSET" app.exe "$PWD/dep-part"
cmp -s l5/vcruntime140.dll toolset/vcruntime140.dll \
    || fail "L5: an incomplete set replaced the toolset's" l5/place.log <(advice l5)
echo "ok: L5 an incomplete newer set does not replace the toolset's"

# L6
leg l6
place l6 app.exe "$PWD/dep-old"
cmp -s l6/vcruntime140.dll dep-old/vcruntime140.dll \
    || fail "L6: a graph without --crt no longer placed by search order" l6/place.log
echo "ok: L6 a graph written before the rule keeps search order"

echo "PASS: 819_the_crt_is_placed_by_its_rule_not_by_search_order"
