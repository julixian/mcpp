#!/usr/bin/env bash
# requires: unix-shell
# 856 -- `mcpp test -p a -p b` tests both members (mcpp#750), and a member that
# fails does so alone.
#
# `mcpp test -p a -p b` built and tested `b` alone, reported one member, and
# exited 0 (mcpp#750). Now every member a `-p` names is tested, in one plan,
# and the report counts each. A test over several members continues past a
# failing member, whatever failed in it: a test, its package's build, or its
# plan (member selection design 2026-09-30, D1 and S4); and a member's tests
# run with that member's own runtime, never another member's.
#
# Criteria:
#   I. #750's reproduction: `mcpp test -p a -p b` runs the tests of `a` and of
#      `b`, in either order, the workspace summary counts both members, and
#      `c` is untouched.
#   K. A member whose test fails is reported FAILED; the others run, pass, and
#      are reported; the command exits 1.
#   L. A member whose package does not compile is reported FAILED, naming
#      why; the others still run and pass.
#   M. A member that fails to plan is reported FAILED; the others are planned
#      without it and run.
#   N. (Linux) A member's tests see the runtime directories of that member's
#      closure, and not another member's: two members in one plan, each
#      declaring a `[runtime] library_dirs`, each test seeing its own
#      directory in LD_LIBRARY_PATH and not the other's.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

# A workspace of libraries a, b and c, each with one test.
mk() {   # mk <workspace dir> <members...>
    local ws=$1; shift
    mkdir -p "$ws"
    local list="" m
    for m in "$@"; do list="$list${list:+, }\"$m\""; done
    printf '[workspace]\nmembers = [%s]\n' "$list" > "$ws/mcpp.toml"
    for m in "$@"; do
        mkdir -p "$ws/$m/src" "$ws/$m/tests"
        printf '[package]\nname    = "%s"\nversion = "0.1.0"\n\n[targets.%s]\nkind = "lib"\n' $m $m > "$ws/$m/mcpp.toml"
        printf 'export module sel856_%s;\nexport int %s_v() { return 1; }\n' $m $m > "$ws/$m/src/$m.cppm"
        printf 'import sel856_%s;\nint main() { return %s_v() == 1 ? 0 : 1; }\n' $m $m > "$ws/$m/tests/test_$m.cpp"
    done
}

# ── I ──────────────────────────────────────────────────────────────────────
mk w1 a b c
cd w1
"$MCPP" test -p a -p b > i1.log 2>&1 || fail "I: test -p a -p b failed" i1.log
grep -q "test_a ... ok" i1.log || fail "I: a's test did not run" i1.log
grep -q "test_b ... ok" i1.log || fail "I: b's test did not run (the repeated -p kept one value)" i1.log
! grep -q "test_c" i1.log || fail "I: c's test ran although no -p named it" i1.log
grep -q "workspace result ok. 2 member(s); 2 passed; 0 failed" i1.log || fail "I: the workspace summary does not count both members" i1.log
"$MCPP" test -p b -p a > i2.log 2>&1 || fail "I: test -p b -p a failed" i2.log
grep -q "workspace result ok. 2 member(s); 2 passed; 0 failed" i2.log || fail "I: the reverse order counts differently" i2.log
"$MCPP" test -p a -p b --message-format json > i3.json 2> i3.err || fail "I: json test -p a -p b failed" i3.json i3.err
grep -q '"workspace_summary":{"members":2,"passed":2,"failed":0' i3.json || fail "I: the JSON summary does not count both members" i3.json
[ -z "$(find target -path '*/obj/c*' 2>/dev/null)" ] || fail "I: c was built" i1.log
cd ..
echo "ok: I, test -p a -p b tests both members and counts both"

# ── K ──────────────────────────────────────────────────────────────────────
mk w2 a b c
printf 'int main() { return 1; }\n' > w2/c/tests/test_c.cpp
cd w2
rc=0
"$MCPP" test --workspace > k.log 2>&1 || rc=$?
[ "$rc" = 1 ] || fail "K: a failing test did not exit 1 (got $rc)" k.log
grep -q "member 'a' (1/3) ok" k.log && grep -q "member 'b' (2/3) ok" k.log || fail "K: the passing members were not reported" k.log
grep -q "member 'c' (3/3) FAILED" k.log || fail "K: the failing member was not reported" k.log
grep -q "failed members: c" k.log || fail "K: the failing member is not named in the summary" k.log
[ "$(grep -c 'Workspace building' k.log)" = 1 ] || fail "K: the members were planned more than once" k.log
cd ..
echo "ok: K, a failing test fails its member alone, and the others are reported"

# ── L ──────────────────────────────────────────────────────────────────────
mk w3 a b c
printf 'export module sel856_c;\nexport int c_v() { return "not an int"; }\n' > w3/c/src/c.cppm
cd w3
rc=0
"$MCPP" test --workspace > l.log 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "L: a member whose package does not compile exited 0" l.log
grep -q "test_a ... ok" l.log && grep -q "test_b ... ok" l.log || fail "L: the members whose packages compile did not run" l.log
grep -q "member 'c' (3/3) FAILED" l.log || fail "L: the member whose package does not compile was not reported" l.log
grep -q "member 'c': its package did not build" l.log || fail "L: the report does not say why c did not run" l.log
grep -q "failed members: c" l.log || fail "L: c is not named in the summary" l.log
! grep -q "test_c ... ok" l.log || fail "L: c's test ran" l.log
cd ..
echo "ok: L, a member whose package does not compile fails alone, and the others run"

# ── M ──────────────────────────────────────────────────────────────────────
mk w4 a b d c
cat > w4/d/mcpp.toml <<'EOF'
[package]
name    = "d"
version = "0.1.0"

[dependencies]
absent = { path = "../no-such-directory" }

[targets.d]
kind = "lib"
EOF
cd w4
rc=0
"$MCPP" test --workspace > m.log 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "M: a member that cannot be planned exited 0" m.log
for m in a b c; do
    grep -q "test_$m ... ok" m.log || fail "M: member $m did not run although only d cannot be planned" m.log
done
grep -q "member 'd' (3/4) FAILED" m.log || fail "M: d was not reported as failed" m.log
grep -q "failed members: d" m.log || fail "M: d is not named in the summary" m.log
grep -q "workspace test: 1/4 member(s) failed; 3 passed" m.log || fail "M: the summary does not count one failed member" m.log
cd ..
echo "ok: M, a member that cannot be planned fails alone, and the rest are planned without it"

# ── N ──────────────────────────────────────────────────────────────────────
if [ "$(uname -s)" = Linux ]; then
    mkdir -p w5
    printf '[workspace]\nmembers = ["x", "y"]\n' > w5/mcpp.toml
    for m in x y; do
        other=$([ $m = x ] && echo y || echo x)
        mkdir -p w5/$m/src w5/$m/tests w5/$m/rt856$m
        : > w5/$m/rt856$m/keep
        printf '[package]\nname    = "%s"\nversion = "0.1.0"\n\n[runtime]\nlibrary_dirs = ["rt856%s"]\n\n[targets.%s]\nkind = "lib"\n' $m $m $m > w5/$m/mcpp.toml
        printf 'export module sel856_%s;\nexport int %s_v() { return 1; }\n' $m $m > w5/$m/src/$m.cppm
        cat > w5/$m/tests/env.cpp <<EOF
#include <cstdlib>
#include <cstring>
int main() {
    const char* p = std::getenv("LD_LIBRARY_PATH");
    if (!p) return 2;
    if (!std::strstr(p, "rt856$m")) return 3;    // its own runtime directory
    if (std::strstr(p, "rt856$other")) return 4; // another member's
    return 0;
}
EOF
    done
    cd w5
    "$MCPP" test --workspace > n.log 2>&1 || fail "N: a member's test saw the wrong runtime directories (exit 3: own missing, 4: another's present)" n.log
    [ "$(grep -c 'Workspace building' n.log)" = 1 ] || fail "N: the two members were planned more than once" n.log
    grep -q "workspace result ok. 2 member(s); 2 passed" n.log || fail "N: both members' tests did not pass" n.log
    cd ..
    echo "ok: N, each member's tests run with that member's runtime directories alone"
else
    echo "skip: N, LD_LIBRARY_PATH is the Linux runtime search variable"
fi

echo "PASS: 856_a_test_over_several_members_continues_past_a_failing_one"
