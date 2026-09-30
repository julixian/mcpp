#!/usr/bin/env bash
# requires: unix-shell
# 855 -- a build that several members' tests share is reported once, by
# addition.
#
# A `test` over several members builds each configuration group once, so the
# time the members' tests waited for their build is the group's, and it cannot
# be split per member. docs/50 §7 forbids changing the meaning of a field, so
# the stream only gains (member selection design 2026-09-30, D6):
#
#   - a `{"group_build": {"group": N, "members": [...], "build_ms": M}}`
#     record, before the group's first test record;
#   - a `build_group` field on each member's summary, naming the group;
#   - each member's `build_ms`, which keeps its definition, the wall time its
#     tests waited for their build, and is now its group's build time;
#   - `elapsed_ms` and each test's `duration_ms`, which keep their meaning.
#
# The human report states each group once: its members and its build time.
#
# Criteria:
#   H. The JSON stream of `mcpp test --workspace` over `a` and `b`, which are
#      one configuration, has one `group_build` record, before every test
#      record, naming both; both members' summaries carry its `build_group`
#      and the same `build_ms`; every line of the stream is one JSON object;
#      the `workspace_summary` record still ends it.
#   I. The human report has one line for the group, naming both members and
#      the build time, and each member's line states the time of its run.
#   J. A member a `-p` does not name is in no record of the stream.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

cat > mcpp.toml <<'EOF'
[workspace]
members = ["a", "b", "c"]
EOF
for m in a b c; do
    mkdir -p $m/src $m/tests
    printf '[package]\nname    = "%s"\nversion = "0.1.0"\n\n[targets.%s]\nkind = "lib"\n' $m $m > $m/mcpp.toml
    printf 'export module sel855_%s;\nexport int %s_v() { return 1; }\n' $m $m > $m/src/$m.cppm
    printf 'import sel855_%s;\nint main() { return %s_v() == 1 ? 0 : 1; }\n' $m $m > $m/tests/test_$m.cpp
done

# ── H ──────────────────────────────────────────────────────────────────────
# Records are read from stdout alone; the human narration is not part of it.
"$MCPP" test -p a -p b --message-format json > h.json 2> h.err || fail "H: test -p a -p b failed" h.json h.err
while IFS= read -r line; do
    case "$line" in
        '{'*'}') ;;
        *) fail "H: a line of the stream is not one JSON object: $line" h.json ;;
    esac
done < h.json

[ "$(grep -c '"group_build"' h.json)" = 1 ] || fail "H: the stream does not have exactly one group_build record" h.json
first=$(head -1 h.json)
case "$first" in
    '{"group_build":'*) ;;
    *) fail "H: group_build is not before the first test record" h.json ;;
esac
echo "$first" | grep -q '"group":0' || fail "H: the group is not numbered from 0" h.json
echo "$first" | grep -q '"members":\["a","b"\]' || fail "H: group_build does not name both members" h.json
gms=$(echo "$first" | sed -n 's/.*"build_ms":\([0-9][0-9]*\).*/\1/p')
[ -n "$gms" ] || fail "H: group_build has no build_ms" h.json

for m in a b; do
    s=$(grep '"summary"' h.json | grep "\"member\":\"$m\"") || fail "H: no summary for $m" h.json
    echo "$s" | grep -q '"build_group":0' || fail "H: $m's summary does not name its group" h.json
    ms=$(echo "$s" | sed -n 's/.*"build_ms":\([0-9][0-9]*\).*/\1/p')
    [ "$ms" = "$gms" ] || fail "H: $m's build_ms ($ms) is not its group's ($gms)" h.json
    echo "$s" | grep -q '"elapsed_ms":' && echo "$s" | grep -q '"run_ms":' || fail "H: $m's summary lost a field" h.json
    grep -q "\"member\":\"$m\",\"test\":\"test_$m\",\"status\":\"pass\"" h.json || fail "H: no passing record for $m's test" h.json
    # A test's duration_ms is its build and its run, so it is not below its run.
    d=$(grep "\"member\":\"$m\",\"test\":" h.json | sed -n 's/.*"duration_ms":\([0-9][0-9]*\).*/\1/p')
    [ -n "$d" ] || fail "H: no duration_ms for $m's test" h.json
done
last=$(tail -1 h.json)
case "$last" in
    '{"workspace_summary":'*) ;;
    *) fail "H: the stream does not end with workspace_summary" h.json ;;
esac
echo "$last" | grep -q '"members":2,"passed":2,"failed":0' || fail "H: workspace_summary does not count a and b" h.json
echo "ok: H, one group_build record before the tests, and the summaries name their group"

# ── J ──────────────────────────────────────────────────────────────────────
! grep -q '"member":"c"' h.json || fail "J: member c, which no -p named, is in the stream" h.json
[ -z "$(find target -path '*/obj/c*' 2>/dev/null)" ] || fail "J: c was built" h.err
echo "ok: J, a member no -p names is in no record"

# ── I ──────────────────────────────────────────────────────────────────────
"$MCPP" test -p a -p b > i.log 2>&1 || fail "I: test -p a -p b failed" i.log
[ "$(grep -cE 'Workspace +built members a, b in [0-9]+\.[0-9]+s' i.log)" = 1 ] \
    || fail "I: the report does not have one line for the group, with its members and build time" i.log
for m in a b; do
    grep -qE "member '$m' \([12]/2\) ok — 1 passed, run [0-9]+\.[0-9]+s" i.log \
        || fail "I: member $m's line does not state its run time" i.log
done
echo "ok: I, the human report states the group once and each member's run"

echo "PASS: 855_a_shared_test_build_is_reported_once_per_group"
