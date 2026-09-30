#!/usr/bin/env bash
# Sandbox verification of the release that implements
# .agents/docs/2026-09-30-member-selection-and-build-program-cost-plan.md
# (#748, #749, #750), run against the PUBLISHED release inside an xlings
# sandbox:
#
#   B64=$(base64 -w0 .agents/docs/2026-10-01-member-selection-verify.sh)
#   xlings subos new v1001 2>/dev/null || true
#   xlings subos use v1001 --sandbox --cmd "echo $B64 | base64 -d > /tmp/v.sh && VER=<version> bash /tmp/v.sh"
#
# VER selects the release under test. Running it with VER=2026.9.30.2 is the
# control: every section marked CHANGE must fail there, and every other
# section must pass on both.
#
# Every probe directory is removed at the start of its section, because the
# sandbox's $HOME persists between runs of the same subos. A section that does
# not run is reported as SKIP and counted apart from a pass.
set -u
VER="${VER:?VER=<version under test>}"
W="${W:-$HOME/v1001}"
fails=0; passes=0; skips=0
pass() { echo "PASS  $1"; passes=$((passes+1)); }
fail() { echo "FAIL  $1"; [ -n "${2:-}" ] && [ -f "$2" ] && tail -20 "$2"; fails=$((fails+1)); }
skip() { echo "SKIP  $1"; skips=$((skips+1)); }

# 0. The release under test, from the published channel, with the CN mirror.
if [ -n "${MCPP_OVERRIDE:-}" ]; then
    MCPP="$MCPP_OVERRIDE"
else
    xlings config --mirror CN >/dev/null 2>&1 || true
    xlings update >/dev/null 2>&1 || true
    xlings install "mcpp@$VER" -y > /tmp/v1001-install.log 2>&1 || true
    MCPP="$HOME/.xlings/data/xpkgs/xim-x-mcpp/$VER/bin/mcpp"
fi
if [ ! -x "$MCPP" ]; then
    echo "FATAL: mcpp $VER is not installable from the index"; tail -20 /tmp/v1001-install.log; exit 2
fi
got=$("$MCPP" --version 2>&1 | head -1)
case "$got" in *"$VER"*) pass "0 installed: $got";; *) fail "0 version: $got";; esac
# The sandbox's home is empty, so the mirror is set inside it. A run on a host
# with MCPP_OVERRIDE leaves the host's configuration as it is.
[ -n "${MCPP_OVERRIDE:-}" ] || "$MCPP" self config --mirror CN >/dev/null 2>&1 || true
mkdir -p "$W"

lib_member() {   # <dir> <name> <value>: a library member with one module and one test
    mkdir -p "$1/src" "$1/tests"
    printf '[package]\nname = "%s"\nversion = "0.1.0"\n\n[targets.%s]\nkind = "lib"\n' "$2" "$2" > "$1/mcpp.toml"
    printf 'export module %s;\nexport int %s_value() { return %s; }\n' "$2" "$2" "$3" > "$1/src/$2.cppm"
    printf 'import %s;\nint main() { return %s_value() == %s ? 0 : 1; }\n' "$2" "$2" "$3" > "$1/tests/test_$2.cpp"
}

# 1. CHANGE (#750): a repeated -p selects every member it names.
rm -rf "$W/s1"; mkdir -p "$W/s1"; cd "$W/s1"
printf '[workspace]\nmembers = ["a", "b", "c"]\n' > mcpp.toml
lib_member a a 1; lib_member b b 2; lib_member c c 3
if "$MCPP" test -p a -p b > s1.log 2>&1 \
   && grep -q 'test_a' s1.log && grep -q 'test_b' s1.log && ! grep -q 'test_c' s1.log; then
    pass "1 CHANGE: mcpp test -p a -p b tests a and b, and not c"
else fail "1 CHANGE: a repeated -p" s1.log; fi

# 2. CHANGE: --exclude, and a member name that matches nothing.
cd "$W/s1"
if "$MCPP" test --workspace --exclude c > s2a.log 2>&1 \
   && grep -q 'test_a' s2a.log && grep -q 'test_b' s2a.log && ! grep -q 'test_c' s2a.log; then
    pass "2a CHANGE: --workspace --exclude c tests a and b"
else fail "2a CHANGE: --exclude" s2a.log; fi
if "$MCPP" build -p nosuch > s2b.log 2>&1; then fail "2b an unknown member was accepted" s2b.log
elif grep -q 'nosuch' s2b.log; then pass "2b an unknown member is refused by name"
else fail "2b the refusal does not name the member" s2b.log; fi

# 3. CHANGE (R1, R2): run -q writes exactly the program's output, and a
# failed build exits 101.
rm -rf "$W/s3"; mkdir -p "$W/s3/src"; cd "$W/s3"
printf '[package]\nname = "p3"\nversion = "0.1.0"\n\n[targets.p3]\nkind = "bin"\nmain = "src/main.cpp"\n' > mcpp.toml
printf '#include <cstdio>\nint main() { std::puts("OUT"); std::fputs("ERR\\n", stderr); return 0; }\n' > src/main.cpp
"$MCPP" run -q > s3.out 2> s3.err; rc=$?
if [ "$rc" = 0 ] && [ "$(od -c s3.out | head -1)" = "$(printf 'OUT\n' | od -c | head -1)" ]; then
    pass "3a CHANGE: run -q writes exactly the program's stdout"
else fail "3a CHANGE: run -q stdout (rc=$rc)" s3.out; fi
printf 'int main() { x }\n' > src/main.cpp
"$MCPP" run -q > s3b.log 2>&1; rc=$?
if [ "$rc" = 101 ]; then pass "3b CHANGE: a run whose build failed exits 101"
else fail "3b CHANGE: exit status $rc for a failed build" s3b.log; fi

# 4. CHANGE (R3): a build narrates on stderr.
cd "$W/s1"
out=$("$MCPP" build --workspace 2>/dev/null)
if [ -z "$out" ] && "$MCPP" build --workspace 2>&1 >/dev/null | grep -q 'Finished'; then
    pass "4 CHANGE: status lines are on stderr and stdout is empty"
else fail "4 CHANGE: status stream"; echo "$out" | head -5; fi

# 5. index packages build and run with the release.
rm -rf "$W/s5"; mkdir -p "$W/s5/src"; cd "$W/s5"
printf '[package]\nname = "eco"\nversion = "0.1.0"\n\n[dependencies]\n"compat.zlib" = "*"\n"mcpplibs.cmdline" = "*"\n\n[targets.eco]\nkind = "bin"\nmain = "src/main.cpp"\n' > mcpp.toml
cat > src/main.cpp <<'EOF'
#include <cstdio>
#include <zlib.h>
import mcpplibs.cmdline;
int main() { std::printf("zlib %s\n", zlibVersion()); return 0; }
EOF
if "$MCPP" run > s5.log 2>&1 && grep -q '^zlib ' s5.log; then
    pass "5 compat.zlib and mcpplibs.cmdline from the index build and run"
else fail "5 index packages" s5.log; fi

# 6. CHANGE (#748): four members' build programs import one host module of a
# path dependency; the module is compiled once for the four, and the engine's
# own module is kept in the global cache.
rm -rf "$W/s6"; mkdir -p "$W/s6/rules/src"; cd "$W/s6"
printf '[workspace]\nmembers = ["m1", "m2", "m3", "m4"]\n' > mcpp.toml
printf '[package]\nname = "rule6"\nversion = "0.1.0"\n\n[targets.rule6]\nkind = "lib"\n' > rules/mcpp.toml
printf 'export module v6.rules;\nimport mcpp;\nexport void apply(const char* d) { mcpp::define(d); }\n' > rules/src/rule6.cppm
for m in m1 m2 m3 m4; do
    mkdir -p $m/src
    printf '[package]\nname = "%s"\nversion = "0.1.0"\n\n[build-dependencies]\nrule6 = { path = "../rules", host-module = true }\n\n[targets.%s]\nkind = "bin"\nmain = "src/main.cpp"\n' $m $m > $m/mcpp.toml
    printf 'int main() { return 0; }\n' > $m/src/main.cpp
    printf 'import mcpp;\nimport v6.rules;\nint main() { apply("V6_%s=1"); return 0; }\n' $m > $m/build.mcpp
done
if MCPP_VERBOSE=1 "$MCPP" build --workspace > s6.log 2>&1; then
    compiles=$(grep -cE "host module 'v6\.rules' (precompile|compile):" s6.log || true)
    ran=$(grep -cE "^ *build\.mcpp m[1-4] .* ran " s6.log || true)
    if [ "$compiles" = 1 ] && [ "$ran" = 4 ]; then
        pass "6 CHANGE: one host module compile for four build programs"
    else fail "6 CHANGE: $compiles host module compiles for $ran programs" s6.log; fi
else fail "6 the workspace of four build programs did not build" s6.log; fi

# 7. CHANGE (#749): one pack of two program members that share a member with a
# build program: one package per member, and the shared program runs once.
rm -rf "$W/s7"; mkdir -p "$W/s7/core/src"; cd "$W/s7"
printf '[workspace]\nmembers = ["core", "cli", "gui"]\n' > mcpp.toml
printf '[package]\nname = "core"\nversion = "0.1.0"\n\n[targets.core]\nkind = "lib"\n' > core/mcpp.toml
printf 'export module v7core;\nexport int core_answer() { return 42; }\n' > core/src/core.cppm
cat > core/build.mcpp <<'EOF'
import mcpp;
#include <fstream>
#include <string>
int main() {
    std::ofstream log(std::string(mcpp::out_dir()) + "/runs.log", std::ios::app);
    log << "ran\n";
    return 0;
}
EOF
for m in cli gui; do
    mkdir -p $m/src
    printf '[package]\nname = "%s"\nversion = "0.1.0"\n\n[dependencies]\ncore = { path = "../core" }\n\n[targets.%s]\nkind = "bin"\nmain = "src/main.cpp"\n' $m $m > $m/mcpp.toml
    printf '#include <cstdio>\nimport v7core;\nint main() { std::printf("%s %%d\\n", core_answer()); }\n' $m > $m/src/main.cpp
done
if "$MCPP" pack --workspace --format dir > s7.log 2>&1; then
    packed=$(grep -cE '^ *Packed ' s7.log || true)
    runs=$(find core -name runs.log -exec cat {} + 2>/dev/null | grep -c ran || true)
    if [ "$packed" = 2 ] && [ "$runs" = 1 ]; then
        pass "7 CHANGE: one pack of two members, and the shared build program ran once"
    else fail "7 CHANGE: $packed packages, the shared program ran $runs times" s7.log; fi
else fail "7 CHANGE: mcpp pack --workspace" s7.log; fi

echo "---- $passes passed, $fails failed, $skips skipped"
[ "$fails" = 0 ]
