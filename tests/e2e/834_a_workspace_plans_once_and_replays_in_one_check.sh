#!/usr/bin/env bash
# requires: unix-shell python3
# 834_a_workspace_plans_once_and_replays_in_one_check.sh -- workspace design
# 2026-09-29 §14.
#
# 2026.9.28.3 planned a shared member as the root of a nested build, and the
# nested build met the same condition again: a chain of n members cost 2^n
# plans. Before it, every member was planned in full on every command. A
# workspace is now one plan per configuration and one fast-path record per
# selection and configuration.
#
#   P1  `--workspace` over a chain of five libraries and a program resolves
#       the toolchain once;
#   P2  `-p app`, at the end of the chain, resolves it once;
#   P3  with nothing changed, `--workspace`, `-p app` and a build inside the
#       member are each replayed without a plan (no toolchain resolution) in
#       well under a second.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"
PY=python3

cat > mcpp.toml <<'EOF'
[workspace]
members = ["lib1", "lib2", "lib3", "lib4", "lib5", "app"]
EOF
prev=""
for i in 1 2 3 4 5; do
    mkdir -p lib$i/src
    {
        printf '[package]\nname = "lib%s"\nversion = "0.1.0"\n\n[targets.lib%s]\nkind = "lib"\n' "$i" "$i"
        [ -n "$prev" ] && printf '\n[dependencies]\n%s = { path = "../%s" }\n' "$prev" "$prev"
    } > lib$i/mcpp.toml
    if [ -n "$prev" ]; then
        printf 'export module lib%s;\nimport %s;\nexport int v%s() { return v%s() + 1; }\n' \
            "$i" "$prev" "$i" "${prev#lib}" > lib$i/src/lib$i.cppm
    else
        printf 'export module lib1;\nexport int v1() { return 1; }\n' > lib1/src/lib1.cppm
    fi
    prev=lib$i
done
mkdir -p app/src
printf '[package]\nname = "app"\nversion = "0.1.0"\n\n[dependencies]\nlib5 = { path = "../lib5" }\n' > app/mcpp.toml
printf 'import lib5;\nint main() { return v5() == 5 ? 0 : 1; }\n' > app/src/main.cpp

resolutions() { grep -c "Resolving toolchain" "$1" || true; }

# P1
"$MCPP" build --workspace > p1.log 2>&1 || fail "P1: the workspace build failed" p1.log
[ "$(resolutions p1.log)" = 1 ] || fail "P1: --workspace resolved the toolchain $(resolutions p1.log) times" p1.log

# P2
rm -rf target
"$MCPP" build -p app > p2.log 2>&1 || fail "P2: -p app failed" p2.log
[ "$(resolutions p2.log)" = 1 ] || fail "P2: -p app resolved the toolchain $(resolutions p2.log) times" p2.log

# P3: each command once to record it, then once more with nothing changed.
timed() {  # timed <log> <dir> <args...>: runs mcpp, prints the wall time in ms
    local log="$1" dir="$2"; shift 2
    "$PY" - "$MCPP" "$dir" "$log" "$@" <<'EOF'
import subprocess, sys, time
mcpp, cwd, log, args = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
t = time.monotonic()
with open(log, "w") as out:
    rc = subprocess.call([mcpp] + args, cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
print(int((time.monotonic() - t) * 1000) if rc == 0 else -1)
EOF
}
for sel in ws p in; do
    case $sel in
        ws) dir=.;   args="build --workspace" ;;
        p)  dir=.;   args="build -p app" ;;
        in) dir=app; args="build" ;;
    esac
    "$MCPP" $args > /dev/null 2>&1 || true
    (cd "$dir" && "$MCPP" $args > "$TMP/p3-$sel-first.log" 2>&1) || fail "P3: $args failed" "p3-$sel-first.log"
    ms=$(timed "$TMP/p3-$sel.log" "$dir" $args)
    [ "$ms" -ge 0 ] || fail "P3: the replay of '$args' failed" "p3-$sel.log"
    [ "$(resolutions "p3-$sel.log")" = 0 ] || fail "P3: '$args' with nothing changed was planned" "p3-$sel.log"
    [ "$ms" -lt 1000 ] || fail "P3: '$args' with nothing changed took ${ms} ms" "p3-$sel.log"
done

echo "PASS: 834_a_workspace_plans_once_and_replays_in_one_check"
