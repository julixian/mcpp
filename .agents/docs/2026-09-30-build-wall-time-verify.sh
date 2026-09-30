#!/usr/bin/env bash
# Sandbox verification of mcpp 2026.9.30.2 (.agents/docs/2026-09-30-build-
# wall-time-progress-count-and-hang-plan.md), run against the PUBLISHED release
# inside an xlings sandbox:
#
#   B64=$(base64 -w0 .agents/docs/2026-09-30-build-wall-time-verify.sh)
#   xlings subos new v9302 2>/dev/null || true
#   xlings subos use v9302 --sandbox --cmd "echo $B64 | base64 -d > /tmp/v.sh && VER=2026.9.30.2 bash /tmp/v.sh"
#
# VER selects the release under test. Running it with VER=2026.9.30.1 is the
# control: every section marked CHANGE must fail there, and every other section
# must pass on both.
#
# Every probe directory is removed at the start of its section, because the
# sandbox's $HOME persists between runs of the same subos. A section that does
# not run is reported as SKIP and counted separately from a pass.
set -u
VER="${VER:-2026.9.30.2}"
W="$HOME/v9302"
fails=0; passes=0; skips=0
pass() { echo "PASS  $1"; passes=$((passes+1)); }
fail() { echo "FAIL  $1"; [ -n "${2:-}" ] && [ -f "$2" ] && tail -15 "$2"; fails=$((fails+1)); }
skip() { echo "SKIP  $1"; skips=$((skips+1)); }

# ── 0. the release under test, from the published channel ──────────────────
if [ -n "${MCPP_OVERRIDE:-}" ]; then
    MCPP="$MCPP_OVERRIDE"
else
    xlings config --mirror CN >/dev/null 2>&1 || true
    xlings update >/dev/null 2>&1 || true
    xlings install "mcpp@$VER" -y > /tmp/v9302-install.log 2>&1 || true
    MCPP="$HOME/.xlings/data/xpkgs/xim-x-mcpp/$VER/bin/mcpp"
fi
if [ ! -x "$MCPP" ]; then
    echo "FATAL: mcpp $VER is not installable from the index"; tail -20 /tmp/v9302-install.log; exit 2
fi
got=$("$MCPP" --version 2>&1 | head -1)
case "$got" in *"$VER"*) pass "0 installed: $got";; *) fail "0 version: $got";; esac
"$MCPP" self config --mirror CN >/dev/null 2>&1 || true
mkdir -p "$W"

module_file() { printf 'export module boost;\nexport int value() { return %s; }\n' "$2" > "$1"; }
main_file() { printf '#include <cstdio>\nimport boost;\nint main() { std::printf("%%d\\n", value()); }\n' > "$1"; }
bin_of() { find "$1" -path '*/bin/*' -name "$2" -type f | head -1; }

# ── 1. CHANGE: #732, an artifacts program with its own module of one name ──
rm -rf "$W/s1"; mkdir -p "$W/s1/app/src" "$W/s1/updater/src"; cd "$W/s1"
module_file app/src/boost.cppm 1; main_file app/src/main.cpp
module_file updater/src/boost.cppm 2; main_file updater/src/main.cpp
printf '[package]\nname = "updater"\nversion = "0.1.0"\n\n[targets.updater]\nkind = "bin"\nmain = "src/main.cpp"\n' > updater/mcpp.toml
printf '[package]\nname = "app"\nversion = "0.1.0"\n\n[dependencies]\nupdater = { path = "../updater", artifacts = ["updater"] }\n\n[targets.app]\nkind = "bin"\nmain = "src/main.cpp"\n' > app/mcpp.toml
if (cd app && "$MCPP" build > ../s1.log 2>&1) \
   && [ "$("$(bin_of app/target app)")" = 1 ] && [ "$("$(bin_of app/target updater)")" = 2 ]; then
    pass "1 CHANGE: the app and its artifacts updater each have their own module boost"
else fail "1 CHANGE: two programs, one module name" s1.log; fi

# ── 2. #732, two providers in one program are refused ───────────────────────
rm -rf "$W/s2"; mkdir -p "$W/s2/lib1/src" "$W/s2/lib2/src" "$W/s2/prog/src"; cd "$W/s2"
module_file lib1/src/boost.cppm 1; module_file lib2/src/boost.cppm 2
for l in lib1 lib2; do printf '[package]\nname = "%s"\nversion = "0.1.0"\n' "$l" > $l/mcpp.toml; done
printf 'int main() { return 0; }\n' > prog/src/main.cpp
printf '[package]\nname = "prog"\nversion = "0.1.0"\n\n[dependencies]\nlib1 = { path = "../lib1" }\nlib2 = { path = "../lib2" }\n\n[targets.prog]\nkind = "bin"\nmain = "src/main.cpp"\n' > prog/mcpp.toml
if (cd prog && "$MCPP" build > ../s2.log 2>&1); then fail "2 two providers in one program were accepted" s2.log
elif grep -q "module 'boost' is provided by package" s2.log; then pass "2 two providers in one program are refused"
else fail "2 the refusal does not name the module" s2.log; fi

# ── 3. CHANGE: #732, two workspace members with one module name ─────────────
rm -rf "$W/s3"; mkdir -p "$W/s3/one/src" "$W/s3/two/src"; cd "$W/s3"
module_file one/src/boost.cppm 5; main_file one/src/main.cpp
module_file two/src/boost.cppm 6; main_file two/src/main.cpp
printf '[workspace]\nmembers = ["one", "two"]\n' > mcpp.toml
for m in one two; do printf '[package]\nname = "%s"\nversion = "0.1.0"\n\n[targets.%s]\nkind = "bin"\nmain = "src/main.cpp"\n' "$m" "$m" > $m/mcpp.toml; done
if "$MCPP" build --workspace > s3.log 2>&1 \
   && [ "$("$(bin_of target one)")" = 5 ] && [ "$("$(bin_of target two)")" = 6 ]; then
    pass "3 CHANGE: two workspace members each have their own module boost"
else fail "3 CHANGE: two workspace members, one module name" s3.log; fi

# ── 4. index packages build and run with the release ───────────────────────
rm -rf "$W/s4"; mkdir -p "$W/s4/src"; cd "$W/s4"
printf '[package]\nname = "eco"\nversion = "0.1.0"\n\n[dependencies]\n"compat.zlib" = "*"\n"mcpplibs.cmdline" = "*"\n\n[targets.eco]\nkind = "bin"\nmain = "src/main.cpp"\n' > mcpp.toml
cat > src/main.cpp <<'EOF'
#include <cstdio>
#include <zlib.h>
import mcpplibs.cmdline;
int heavy();
int main() { std::printf("zlib %s\n", zlibVersion()); return heavy() == 0; }
EOF
# One unit that takes a few seconds to compile, so that section 5's build
# outlives the half second before the status row is first drawn.
cat > src/heavy.cpp <<'EOF'
#include <format>
#include <regex>
#include <string>
int heavy() {
    std::regex r("([a-z]+)-([0-9]+)");
    std::smatch m;
    std::string s = std::format("{}-{}", "mcpp", 2026);
    return std::regex_match(s, m, r) ? static_cast<int>(m.size()) : 0;
}
EOF
if "$MCPP" build > s4.log 2>&1 && "$(bin_of target eco)" | grep -q '^zlib '; then
    pass "4 compat.zlib and mcpplibs.cmdline from the index build and run"
else fail "4 index packages" s4.log; fi

# ── 5. CHANGE: the status row counts the work of the build ─────────────────
# A pty makes the status row appear; the project of section 4 has a
# dependency the global cache serves after its first build.
cd "$W/s4"
if command -v script > /dev/null 2>&1 && [ -f s4.log ] && grep -q 'Finished' s4.log; then
    "$MCPP" clean > /dev/null 2>&1
    MCPP_PROGRESS=plain script -qefc "stty cols 160 rows 40; $MCPP build" s5.pty > /dev/null 2>&1
    rows=$(sed 's/\x1b\[[0-9;?]*[A-Za-z]//g' s5.pty | tr '\r' '\n')
    # The units the cache placed, from the `Cached ... (N units)` lines; the
    # Building total must not include them (2026.9.30.1 counted each one).
    placed=$(echo "$rows" | grep -a '^ *Cached ' | grep -ao '[0-9]* units\?)' | grep -o '^[0-9]*' \
             | awk '{s += $1} END {print s + 0}')
    building=$(echo "$rows" | grep -ao 'Building [0-9]*/[0-9]*' | head -1)
    total=${building##*/}
    if [ -n "$total" ] && [ "$placed" -gt 0 ] && [ "$total" -lt "$placed" ]; then
        pass "5 CHANGE: Building counts $total steps, not the $placed units placed from the cache"
    else fail "5 CHANGE: the status row (first Building: '$building', units placed: $placed)" s5.pty; fi
else skip "5 no script(1) or section 4 did not build"; fi

# ── 6. CHANGE: #744, the vendored xlings comes from the newest source ──────
rm -rf "$W/s6"; mkdir -p "$W/s6/rel/bin" "$W/s6/rel/registry/bin" "$W/s6/pathbin"; cd "$W/s6"
NINJA=$(ls "$HOME"/.mcpp/registry/data/xpkgs/xim-x-ninja/*/ninja 2>/dev/null | head -1)
REAL="$HOME/.mcpp/registry/bin/xlings"
if [ -n "$NINJA" ] && [ -x "$REAL" ]; then
    older=$("$NINJA" --version | head -1)
    export_home="$W/s6/home"
    cp "$MCPP" rel/bin/mcpp
    cp "$NINJA" rel/registry/bin/xlings
    cp "$REAL" pathbin/xlings
    MCPP_HOME="$export_home" MCPP_OFFLINE=1 rel/bin/mcpp self env > setup.log 2>&1 || true
    mkdir -p "$export_home/registry/bin"; rm -f "$export_home/registry/bin/xlings"; cp "$NINJA" "$export_home/registry/bin/xlings"
    env -u MCPP_VENDORED_XLINGS MCPP_HOME="$export_home" MCPP_OFFLINE=1 PATH="$W/s6/pathbin:/usr/bin:/bin" \
        rel/bin/mcpp self env > s6.out 2> s6.err || true
    if grep -q "vendored xlings $older -> .* from PATH" s6.err; then
        pass "6 CHANGE: a newer xlings on PATH replaced the vendored one past an older released copy"
    else fail "6 CHANGE: #744" s6.err; fi
else skip "6 no ninja payload or vendored xlings to stand in"; fi

# ── 7. planning states its phases ──────────────────────────────────────────
cd "$W/s4" && touch src/main.cpp
LOGFILE="$HOME/.mcpp/log/mcpp.log"
before=$(wc -c < "$LOGFILE" 2>/dev/null || echo 0)
if MCPP_LOG_LEVEL=info "$MCPP" build > s7.log 2>&1 \
   && tail -c +$((before + 1)) "$LOGFILE" 2>/dev/null | grep -q 'build/stage: plan scan'; then
    pass "7 CHANGE: planning states its phases under build/stage"
else fail "7 CHANGE: the planning phase timers" s7.log; fi

# ── 8. the largest consumer: xlings builds from its main branch ────────────
rm -rf "$W/s8"; mkdir -p "$W/s8"; cd "$W/s8"
if git clone -q --depth 1 https://github.com/openxlings/xlings.git xlings > s8-clone.log 2>&1; then
    cd xlings
    if "$MCPP" build > ../s8.log 2>&1 && "$(bin_of target xlings)" --version 2>/dev/null | grep -q '^xlings '; then
        pass "8 xlings builds from its main branch and runs: $(grep -a 'Finished' ../s8.log | tail -1 | sed 's/\x1b\[[0-9;]*m//g; s/^ *//')"
    else fail "8 xlings from its main branch" ../s8.log; fi
else skip "8 xlings could not be cloned"; fi

# ── 9. CHANGE: a rooted workspace's own path override wins ────────────────
# The shape of the release canary on mcpp-language-server: the workspace's
# own package declares `framework` by `path`, a library it uses asks for it by
# `git`. 2026.9.30.1 refuses it ("Pick one").
rm -rf "$W/s9"; mkdir -p "$W/s9/fw/src" "$W/s9/libg/src" "$W/s9/ws/src"; cd "$W/s9"
git init -q fw && git -C fw config user.email t@l && git -C fw config user.name t
printf '[package]\nname = "framework"\nversion = "0.1.0"\n\n[build]\nsources = ["src/*.c"]\n\n[targets.framework]\nkind = "lib"\n' > fw/mcpp.toml
printf 'int framework_marker(void) { return 101; }\n' > fw/src/framework.c
git -C fw add -A && git -C fw commit -qm A && rev=$(git -C fw rev-parse HEAD)
printf 'int framework_marker(void) { return 199; }\n' > fw/src/framework.c
printf '[package]\nname = "libg"\nversion = "0.1.0"\n\n[build]\nsources = ["src/*.c"]\n\n[targets.libg]\nkind = "lib"\n\n[dependencies.framework]\ngit = "%s"\nrev = "%s"\n' "$W/s9/fw" "$rev" > libg/mcpp.toml
printf 'extern int framework_marker(void);\nint libg_marker(void) { return framework_marker(); }\n' > libg/src/libg.c
printf '[package]\nname = "app"\nversion = "0.1.0"\n\n[dependencies]\nframework = { path = "%s" }\nlibg = { path = "%s" }\n\n[targets.app]\nkind = "bin"\nmain = "src/main.cpp"\n\n[workspace]\nmembers = ["."]\n' "$W/s9/fw" "$W/s9/libg" > ws/mcpp.toml
printf '#include <cstdio>\nextern "C" int libg_marker(void);\nint main() { std::printf("%%d\\n", libg_marker()); }\n' > ws/src/main.cpp
if (cd ws && "$MCPP" build > ../s9.log 2>&1) && [ "$("$(bin_of ws/target app)")" = 199 ]; then
    pass "9 CHANGE: a rooted workspace's own path override wins over a library's git declaration"
else fail "9 CHANGE: the rooted workspace's override" s9.log; fi

echo
echo "RESULT: $passes passed, $fails failed, $skips skipped (mcpp $VER)"
[ "$fails" -eq 0 ]
