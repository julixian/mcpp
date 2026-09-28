#!/usr/bin/env bash
# tests/release/verify-published.sh -- the verification of a PUBLISHED mcpp,
# and of the xlings it drives, as a consumer receives them (the 2026-09-28
# ecosystem design, WS10).
#
# It runs inside a fresh SubOS sandbox. There it sees what a consumer sees --
# the index entries, the released archives, the xlings that mcpp bootstraps --
# and no working tree. Each released item adds a section with its own
# assertions, and the sections of earlier releases stay, so a release that
# breaks an earlier item fails here. A section that this host cannot run
# prints NOT RUN, and the summary counts those separately: zero failures is
# never reported for sections that did not execute.
#
# Usage, from the host. The sandbox has its own $HOME contents and /tmp, so the
# script is passed in rather than read from the checkout:
#
#   xlings subos new verify-new
#   B64=$(base64 -w0 tests/release/verify-published.sh)
#   xlings subos use verify-new --sandbox --cmd \
#     "echo $B64 | base64 -d > /tmp/v.sh && VER=<mcpp> XVER=<xlings> bash /tmp/v.sh"
#
# Run it for the new release and for the previous one, each in a fresh SubOS.
# The previous release's failures in the new sections are their before-
# readings; both summaries go into the release record.
#
# Environment:
#   VER     the mcpp version (required)
#   XVER    the xlings version; the xlings sections are NOT RUN without it
#   PVER    the mcpp.plugins version; its section is NOT RUN without it
#   MIRROR  the mirror both tools use (default CN)
#   W       the probe directory, recreated on every run (default
#           /tmp/verify-published)
#   MCPP_HOME  read as mcpp reads it (default ~/.mcpp)
#   M, XS   an mcpp or xlings binary to verify instead of the published one,
#           for rehearsing the script before a release. A run with either set
#           says so first and last: it does not verify a published package.
set -u
VER="${VER:?VER is required}"
XVER="${XVER:-}"
MIRROR="${MIRROR:-CN}"
REHEARSAL=""
[ -n "${M:-}" ] && REHEARSAL="M=$M"
[ -n "${XS:-}" ] && REHEARSAL="$REHEARSAL XS=$XS"
[ -n "$REHEARSAL" ] && echo "REHEARSAL: $REHEARSAL; this run does not verify a published package"
M="${M:-$HOME/.xlings/data/xpkgs/xim-x-mcpp/$VER/bin/mcpp}"
MH="${MCPP_HOME:-$HOME/.mcpp}"
XS_GIVEN="${XS:-}"
W="${W:-/tmp/verify-published}"
ok=0; failed=0; notrun=0
pass() { echo "ok: $1"; ok=$((ok + 1)); }
fail() { echo "FAILED: $1"; failed=$((failed + 1)); }
skip() { echo "NOT RUN: $1"; notrun=$((notrun + 1)); }
rm -rf "$W"; mkdir -p "$W"
HOST_OS=$(uname -s); HOST_ARCH=$(uname -m)
has_py() { command -v python3 > /dev/null 2>&1; }

# ════════════════════════════════════════════════════════════════════════════
echo "== install through the release path, with the $MIRROR mirror for both tools"
xlings config --mirror "$MIRROR" > /dev/null 2>&1 || true
xlings update > /dev/null 2>&1 || true
case "$REHEARSAL" in
    *M=*) ;;
    *) xlings install "mcpp@$VER" -y > "$W/install.log" 2>&1 || tail -5 "$W/install.log" ;;
esac
[ -x "$M" ] || { echo "FATAL: $M is not installed"; exit 2; }
if "$M" --version | grep -qx "mcpp $VER"; then pass "the binary at the store path reports $VER"
else fail "the binary does not report $VER ($("$M" --version))"; fi
"$M" self config --mirror "$MIRROR" > /dev/null 2>&1 || true
"$M" self env > "$W/env.log" 2>&1 || true
RX="$MH/registry/bin/xlings"
if [ -n "$XVER" ]; then
    if [ -x "$RX" ] && "$RX" --version 2>/dev/null | grep -q "$XVER"; then pass "mcpp bootstrapped its pinned xlings $XVER"
    else fail "the registry xlings is not $XVER ($("$RX" --version 2>&1 | head -1))"; fi
fi

# ════════════════════════════════════════════════════════════════════════════
# xlings
# ════════════════════════════════════════════════════════════════════════════
XS=""
if [ -n "$XVER" ]; then
    echo "== xlings $XVER: the released package"
    if [ -n "$XS_GIVEN" ]; then
        XS="$XS_GIVEN"
    else
        xlings install "xlings@$XVER" -y > "$W/xinstall.log" 2>&1 || true
        XS="$HOME/.xlings/data/xpkgs/xim-x-xlings/$XVER/bin/xlings"
    fi
    if [ -x "$XS" ] && "$XS" --version 2>/dev/null | grep -q "$XVER"; then
        pass "xlings $XVER installs from the index and reports its version"
    else
        fail "xlings $XVER is not installed at $XS ($(tail -1 "$W/xinstall.log" 2>/dev/null))"
        XS=""
    fi
else
    skip "the xlings sections (XVER unset)"
fi

# A home at $1 over the index at $2 (a directory, or empty for the default
# indexes), initialised by the released xlings.
xhome() {
    local home="$1" index="$2"
    mkdir -p "$home/subos/default/bin"
    cp "$XS" "$home/xlings"
    if [ -n "$index" ]; then
        printf '{ "mirror": "%s", "index_repos": [{ "name": "xim", "url": "%s" }] }\n' \
            "$MIRROR" "$index" > "$home/.xlings.json"
    else
        printf '{ "mirror": "%s" }\n' "$MIRROR" > "$home/.xlings.json"
    fi
    xrun "$home" self init > "$home.init.log" 2>&1
}
xrun() {  # $1=home, rest=arguments
    local home="$1"; shift
    ( cd /tmp && env -i HOME="$HOME" PATH=/usr/bin:/bin XLINGS_HOME="$home" \
        XLINGS_NON_INTERACTIVE=1 XLINGS_LOCK_TIMEOUT=60 "$XS" "$@" )
}

if [ -n "$XS" ]; then
    # An offline fixture index: two versions of a package, a script package,
    # and an index build script that records each of its runs.
    XI="$W/xindex"
    mkdir -p "$XI/pkgs/u" "$XI/pkgs/n"
    printf 'xim_indexrepos = {}\n' > "$XI/xim-indexrepos.lua"
    cat > "$XI/pkgs/u/upgrade-fixture.lua" <<'LUA'
package = {
    spec = "1", name = "upgrade-fixture", description = "verify-published fixture",
    type = "package", status = "stable",
    xpm = {
        linux   = { ["1.0.0"] = {}, ["2.0.0"] = {} },
        macosx  = { ["1.0.0"] = {}, ["2.0.0"] = {} },
        windows = { ["1.0.0"] = {}, ["2.0.0"] = {} },
    },
}
import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
function install()
    local dir = pkginfo.install_dir()
    os.tryrm(dir); os.mkdir(dir)
    io.writefile(path.join(dir, "VERSION"), pkginfo.version())
    return true
end
function config() xvm.add("upgrade-fixture", { bindir = pkginfo.install_dir() }); return true end
function uninstall() xvm.remove("upgrade-fixture"); return true end
LUA
    cat > "$XI/pkgs/n/nested-tool.lua" <<'LUA'
package = {
    spec = "1", name = "nested-tool", description = "verify-published fixture",
    type = "script", programs = {"nested-tool"}, status = "stable",
    xpm = {
        linux   = { ["0.0.1"] = {} },
        macosx  = { ["0.0.1"] = {} },
        windows = { ["0.0.1"] = {} },
    },
}
function xpkg_main(...)
    print("NESTED_TOOL_RAN")
    return true
end
LUA
    RUNS="$W/pkgindex-build-runs.log"
    cat > "$XI/pkgindex-build.lua" <<LUA
package = { name = "pkgindex-update", namespace = "fixture" }
function installed() return false end
function install()
    local f = io.open("$RUNS", "a")
    if f then f:write("run\\n") f:close() end
    return true
end
function uninstall() return true end
LUA

    echo "== xlings #617/#624 (WS5): a home is declared"
    H="$W/xh1"
    xhome "$H" "$XI"
    if [ -f "$H/.xlings-home" ]; then pass "self init writes <home>/.xlings-home"
    else fail "self init wrote no .xlings-home"; fi
    xrun "$H" subos new s1 > /dev/null 2>&1 || true
    if [ -d "$H/subos/s1" ] && [ ! -e "$H/subos/s1/.xlings-home" ]; then pass "a SubOS carries no home marker"
    else fail "the SubOS s1 is missing or carries a home marker"; fi
    OUTER="$W/nest/.xlings"
    xhome "$OUTER" "$XI"
    INNER="$OUTER/subos/eco/work/mcpphome/registry"
    xhome "$INNER" "$XI"
    xrun "$INNER" install nested-tool@0.0.1 -y > "$W/h4.log" 2>&1 || true
    out=$( (cd /tmp && env -i HOME="$HOME" PATH=/usr/bin:/bin XLINGS_HOME="$INNER" \
            "$INNER/subos/default/bin/nested-tool") 2>&1 || true)
    if printf '%s' "$out" | grep -q NESTED_TOOL_RAN; then
        pass "#624 a home nested under another home's SubOS runs its own script package"
    else fail "#624 the nested home's script did not run ($(printf '%s' "$out" | tail -1))"; fi

    echo "== xlings WS6: update does what it says, once"
    H="$W/xh6"
    xhome "$H" "$XI"
    xrun "$H" install upgrade-fixture@1.0.0 -y > /dev/null 2>&1 || true
    xrun "$H" install upgrade-fixture@2.0.0 -y > /dev/null 2>&1 || true
    xrun "$H" use upgrade-fixture 1.0.0 > /dev/null 2>&1 || true
    out=$(xrun "$H" update upgrade-fixture -y 2>&1 || true)
    if printf '%s' "$out" | grep -qF "xim:upgrade-fixture@2.0.0 is in the store" \
       && printf '%s' "$out" | grep -qF "active: 1.0.0 -> 2.0.0"; then
        pass "a store hit reads 'is in the store' and 'active: 1.0.0 -> 2.0.0'"
    else fail "a store hit reads: $(printf '%s' "$out" | tr '\n' '|' | cut -c1-200)"; fi
    rm -f "$RUNS" "$XI/.xlings-index-cache.json"
    xrun "$H" update > "$W/xupdate.log" 2>&1 || true
    runs=$(grep -c '^run$' "$RUNS" 2>/dev/null || true)
    if [ "${runs:-0}" = 1 ]; then pass "update runs the index build script once"
    else fail "update ran the index build script ${runs:-0} times"; fi

    echo "== xlings WS4 (protocol 1.3): progress is data"
    H="$W/xhnet"
    xhome "$H" ""
    xrun "$H" update > "$W/xnet-update.log" 2>&1 || true
    n=$(LC_ALL=C grep -cE "^    ($(printf '\xe2\x86\x93')|$(printf '\xe2\x9c\x93')|$(printf '\xe2\x9c\x97')) xim(  .*)?\$" \
        "$W/xnet-update.log" || true)
    if [ "${n:-0}" = 2 ]; then pass "off a terminal the xim index download prints two lines"
    else fail "off a terminal the xim index download printed ${n:-0} progress lines, expected 2"; fi
    # The download lines carry no terminal control. The sub-index build
    # scripts' own [i/n] frames still do (openxlings/xlings#629, open); they
    # are read, not asserted, until that issue is closed.
    DL="$(printf '\xe2\x86\x93')|$(printf '\xe2\x9c\x93')|$(printf '\xe2\x9c\x97')"
    # Unanchored: a frame begins with a carriage return, and an anchored
    # pattern would pass a release that still draws frames.
    dl_lines=$(LC_ALL=C grep -cE "($DL) (xim|awesome|scode|d2x)( |\$)" "$W/xnet-update.log" || true)
    if LC_ALL=C grep -E "($DL) (xim|awesome|scode|d2x)( |\$)" "$W/xnet-update.log" \
         | LC_ALL=C grep -q $'[\r\033]'; then
        fail "a download progress line off a terminal carries a control sequence"
    elif [ "${dl_lines:-0}" -gt 0 ]; then
        pass "the download progress lines off a terminal carry no control sequence ($dl_lines lines)"
    else fail "no download progress line to read"; fi
    frames=$(LC_ALL=C grep -c $'\033\\[K' "$W/xnet-update.log" || true)
    echo "READING: ${frames:-0} line(s) of index build frames off a terminal (openxlings/xlings#629)"
    H="$W/xhif"
    xhome "$H" ""
    if xrun "$H" interface update_packages --args '{}' < /dev/null > "$W/iface.ndjson" 2> "$W/iface.err"; then
        if ! grep -qv '^{' "$W/iface.ndjson"; then pass "interface update_packages writes nothing but NDJSON"
        else fail "interface update_packages wrote a line that is not NDJSON ($(grep -v '^{' "$W/iface.ndjson" | head -1))"; fi
        if has_py; then
            if python3 - "$W/iface.ndjson" <<'PY'
import json, sys
ev = [json.loads(l)["payload"] for l in open(sys.argv[1]) if l.strip()
      and json.loads(l).get("dataKind") == "download_progress"]
sys.exit(0 if ev and all(p.get("stream") for p in ev) else 1)
PY
            then pass "every download_progress event names its stream"
            else fail "a download_progress event names no stream, or there is none"; fi
        else skip "download_progress stream check (no python3)"; fi
    else fail "interface update_packages failed ($(tail -1 "$W/iface.err"))"; fi
fi

# ════════════════════════════════════════════════════════════════════════════
# mcpp, 2026-09-28 design
# ════════════════════════════════════════════════════════════════════════════
echo "== mcpp WS5: the registry mcpp bootstraps is a declared home"
"$M" index update > "$W/iu.log" 2>&1 || true
if [ -f "$MH/registry/.xlings-home" ]; then pass "the mcpp registry carries .xlings-home"
else fail "the mcpp registry ($MH/registry) carries no .xlings-home"; fi

echo "== mcpp WS8: the host's default toolchain has one answer"
if has_py; then
    dt=$("$M" self env --format json 2>/dev/null \
         | python3 -c 'import json,sys; print(json.load(sys.stdin).get("data",{}).get("defaultToolchain",""))' 2>/dev/null)
    if printf '%s' "$dt" | grep -qE '^[a-z]+@[0-9]'; then pass "self env reports defaultToolchain = $dt"
    else fail "self env reports no defaultToolchain ('$dt')"; fi
else skip "WS8 (no python3)"; fi

echo "== mcpp #728 (D7): a more specific conditional table applies later"
if [ "$HOST_OS" = Linux ] && [ "$HOST_ARCH" = x86_64 ]; then
    d="$W/s728"; mkdir -p "$d/src"
    cat > "$d/mcpp.toml" <<'EOF'
[package]
name    = "order728"
version = "0.1.0"

[target.linux.build]
cxxflags = ["-DPICK=1"]

[target.'cfg(all(os = "linux", arch = "x86_64"))'.build]
cxxflags = ["-DPICK=2"]

[targets.order728]
kind = "bin"
main = "src/main.cpp"
EOF
    printf '#if PICK != 2\n#error "the less specific table applied last"\n#endif\nint main() { return 0; }\n' > "$d/src/main.cpp"
    if (cd "$d" && "$M" build > build.log 2>&1); then
        pass "#728 cfg(all(os, arch)) applies after linux, whatever the selector text"
    else fail "#728 ($(grep -m1 -E 'error|#error' "$d/build.log"))"; fi
else skip "#728 on $HOST_OS $HOST_ARCH (the fixture's selector names linux x86_64)"; fi

echo "== mcpp WS1: the MSVC C++ runtime is placed by its rule (place-dlls over synthesised PE files)"
if has_py; then
    # The same synthesiser as tests/e2e/_synth_pe.py (the sandbox sees no checkout).
    cat > "$W/mkpe.py" <<'PY'
import struct
import sys


def pe(imports=(), version=None, linker=(14, 44)):
    b = bytearray(0x400)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    nt = 0x80
    b[nt:nt + 4] = b"PE\0\0"
    struct.pack_into("<HH", b, nt + 4, 0x8664, 1)
    struct.pack_into("<H", b, nt + 20, 240)
    opt = nt + 24
    struct.pack_into("<H", b, opt, 0x20B)
    b[opt + 2], b[opt + 3] = linker
    struct.pack_into("<I", b, opt + 108, 16)
    dirs = opt + 112
    sec = opt + 240
    b[sec:sec + 8] = b".data\0\0\0"
    va, raw, size = 0x1000, 0x400, 0x400
    struct.pack_into("<IIII", b, sec + 8, size, va, size, raw)
    data = bytearray(size)
    if imports:
        names = 0x100
        for i, name in enumerate(imports):
            data[names:names + len(name) + 1] = name.encode() + b"\0"
            struct.pack_into("<IIIII", data, i * 20, va + 0x80, 0, 0, va + names, va + 0x80)
            names += len(name) + 1
        struct.pack_into("<II", b, dirs + 8, va, (len(imports) + 1) * 20)
    if version:
        r = 0x200
        struct.pack_into("<II", b, dirs + 16, va + r, 0x100)
        struct.pack_into("<H", data, r + 14, 1)
        struct.pack_into("<II", data, r + 16, 16, 0x80000000 | 0x18)
        struct.pack_into("<H", data, r + 0x18 + 14, 1)
        struct.pack_into("<II", data, r + 0x18 + 16, 1, 0x80000000 | 0x30)
        struct.pack_into("<H", data, r + 0x30 + 14, 1)
        struct.pack_into("<II", data, r + 0x30 + 16, 1033, 0x48)
        struct.pack_into("<II", data, r + 0x48, va + r + 0x60, 92)
        v = r + 0x60
        struct.pack_into("<HHH", data, v, 92, 52, 0)
        key = "VS_VERSION_INFO".encode("utf-16-le")
        data[v + 6:v + 6 + len(key)] = key
        ma, mi, bu, rv = version
        struct.pack_into("<IIII", data, v + 40, 0xFEEF04BD, 0x00010000,
                         (ma << 16) | mi, (bu << 16) | rv)
    return bytes(b) + bytes(data)


def main(argv):
    out, kind = argv[1], argv[2]
    if kind == "program":
        # Lower case, as the files are named: the closure matches a name
        # exactly on a case-sensitive host file system, as a Windows one does not.
        image = pe(imports=("vcruntime140.dll", "KERNEL32.dll"))
    else:
        image = pe(version=tuple(int(x) for x in kind.split(".")))
    with open(out, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main(sys.argv)
PY
    SET="vcruntime140.dll vcruntime140_1.dll msvcp140.dll"
    d="$W/ws1"; mkdir -p "$d/toolset" "$d/dep-old"
    for n in $SET; do
        python3 "$W/mkpe.py" "$d/toolset/$n" 14.44.35112.1
        python3 "$W/mkpe.py" "$d/dep-old/$n" 14.29.30139.0
    done
    for leg in system static; do
        rm -rf "$d/$leg"; mkdir -p "$d/$leg"
        python3 "$W/mkpe.py" "$d/$leg/app.exe" program
        (cd "$d/$leg" && "$M" place-dlls --output app.exe.dlls --depfile app.exe.dlls.d \
            --crt "$leg" --toolset-crt "$d/toolset" app.exe "$d/dep-old" > place.log 2>&1) || true
    done
    if [ ! -e "$d/system/vcruntime140.dll" ] && [ -f "$d/system/app.exe.dlls" ]; then
        pass "host-coupled (--crt system) places no copy of the runtime"
    else fail "host-coupled placed a copy of the runtime, or the edge failed ($(tail -1 "$d/system/place.log"))"; fi
    if cmp -s "$d/static/vcruntime140.dll" "$d/toolset/vcruntime140.dll" \
       && cmp -s "$d/static/msvcp140.dll" "$d/toolset/msvcp140.dll"; then
        pass "a runtime name the plan did not see takes the toolset's set over an older one"
    else fail "the toolset's set was not placed over the older one ($(tail -1 "$d/static/place.log"))"; fi
    adv="$d/static/.mcpp-advice/app.exe.dlls.advice"
    if [ "$(grep -c "ships the MSVC C++ runtime" "$adv" 2>/dev/null || true)" = 1 ]; then
        pass "the dependency's copy is stated once, through the edge's advice file"
    else fail "the packaging fault is not stated once in $adv"; fi
else skip "WS1 place-dlls (no python3 to synthesise the PE files)"; fi

# ════════════════════════════════════════════════════════════════════════════
# mcpp, earlier releases (2026.9.28.1)
# ════════════════════════════════════════════════════════════════════════════
echo "== #725: a rooted workspace reaches its own path dependency"
d="$W/s725/root"; mkdir -p "$d/src" "$d/a/src"
cat > "$d/mcpp.toml" <<'EOF'
[package]
name    = "root725"
version = "0.1.0"

[workspace]
members = ["a"]

[workspace.package]
version = "0.2.0"

[workspace.dependencies]
cmdline = "0.0.1"

[workspace.build]
cxxflags = ["-DWS_FLAG=1"]

[dependencies]
wsa = { path = "a" }

[targets.root725]
kind = "bin"
main = "src/main.cpp"
EOF
cat > "$d/a/mcpp.toml" <<'EOF'
[package]
name = "wsa"

[dependencies]
cmdline = { workspace = true }

[targets.wsa]
kind = "lib"

[build]
sources = ["src/wsa.cppm"]
EOF
printf 'export module wsa;\n#if !defined(WS_FLAG)\n#error "[workspace.build] did not reach the member"\n#endif\nexport int wsa_value() { return WS_FLAG; }\n' > "$d/a/src/wsa.cppm"
printf 'import wsa;\nint main() { return wsa_value() == 1 ? 0 : 1; }\n' > "$d/src/main.cpp"
if (cd "$d" && "$M" build > build.log 2>&1); then
    if grep -q 'version = "0.0.1"' "$d/mcpp.lock" && ! grep -q 'version = "0.0.2"' "$d/mcpp.lock"; then
        pass "#725 the member builds with the workspace's flags, and the lock records cmdline 0.0.1"
    else fail "#725 the lock does not record cmdline 0.0.1 alone"; fi
else fail "#725 the rooted workspace does not build ($(grep -m1 -i error "$d/build.log"))"; fi
if (cd "$d" && "$M" build -p wsa > p.log 2>&1); then pass "#725 -p takes the package name wsa (directory a)"
else fail "#725 -p wsa is refused ($(grep -m1 -i error "$d/p.log"))"; fi

echo "== #720: a host-module lib root imports its own package"
d="$W/s720"; mkdir -p "$d/app" "$d/rules/src"
cat > "$d/rules/mcpp.toml" <<'EOF'
[package]
namespace = "repro"
name = "rules"
version = "0.1.0"

[lib]
path = "src/rules.cppm"

[build]
sources = ["src/*.cppm"]

[targets.rules]
kind = "lib"
EOF
printf 'export module repro.rules;\nimport repro.helper;\nexport int answer() { return helper_answer(); }\n' > "$d/rules/src/rules.cppm"
printf 'export module repro.helper;\nexport int helper_answer() { return 42; }\n' > "$d/rules/src/helper.cppm"
cat > "$d/app/mcpp.toml" <<'EOF'
[package]
namespace = "repro"
name = "app"
version = "0.1.0"

[build-dependencies]
"repro.rules" = { path = "../rules", host-module = true }

[build]
sources = ["main.cpp"]

[targets.app]
kind = "bin"
main = "main.cpp"
EOF
printf 'import repro.rules;\nint main() { return answer() == 42 ? 0 : 1; }\n' > "$d/app/build.mcpp"
printf 'int main() { return 0; }\n' > "$d/app/main.cpp"
if (cd "$d/app" && "$M" build > build.log 2>&1); then pass "#720 the lib root compiles after the unit it imports"
else fail "#720 ($(grep -m1 -i 'error' "$d/app/build.log"))"; fi

echo "== #717: a graph-wide dialect flag under a target condition"
if [ "$HOST_OS" = Linux ]; then
    d="$W/s717"; mkdir -p "$d/src"
    cat > "$d/mcpp.toml" <<'EOF'
[package]
name    = "dialect717"
version = "0.1.0"

[target.linux.build]
dialect_cxxflags = ["-DX717=1"]

[targets.dialect717]
kind = "bin"
main = "src/main.cpp"
EOF
    printf 'import std;\n#ifndef X717\n#error "the conditional dialect flag did not arrive"\n#endif\nint main() { std::println("ok"); }\n' > "$d/src/main.cpp"
    if (cd "$d" && "$M" build > build.log 2>&1); then
        if grep -q "unsupported key 'dialect_cxxflags'" "$d/build.log"; then fail "#717 the key is still reported unsupported"
        else pass "#717 [target.linux.build] dialect_cxxflags reaches the compile"; fi
    else fail "#717 ($(grep -m1 -i 'error' "$d/build.log"))"; fi
else skip "#717 on $HOST_OS (the fixture's selector names linux)"; fi

echo "== #724: the build database names what a rule generates"
d="$W/s724"; mkdir -p "$d/src" "$d/templates"
printf '[package]\nname    = "gendb"\nversion = "0.1.0"\n' > "$d/mcpp.toml"
printf '#pragma once\ninline int generated_answer() { return 42; }\n' > "$d/templates/answer.h.in"
printf '#include "answer.h"\nint main() { return generated_answer() == 42 ? 0 : 1; }\n' > "$d/src/main.cpp"
cat > "$d/build.mcpp" <<'EOF'
import std;
import mcpp;
int main() {
    const std::string gen = std::string(mcpp::out_dir()) + "/gen";
    const std::string in  = std::string(mcpp::manifest_dir()) + "/templates/answer.h.in";
    const std::string out = gen + "/answer.h";
    mcpp::action a;
    a.id   = "gen:answer";
    a.role = mcpp::roles::source;
    a.arg("cp").arg(in.c_str()).arg(out.c_str()).input(in.c_str()).output(out.c_str()).submit();
    mcpp::include_dir(gen.c_str());
}
EOF
if has_py && (cd "$d" && "$M" emit build-database --format json > db.json 2> db.err); then
    if python3 -c "import json,sys; d=json.load(open('$d/db.json')); g=[x for s in d['data']['database']['sets'] for x in s.get('ide',{}).get('generated',[])]; sys.exit(0 if any(x['kind']=='header' and x['generator']['id']=='gen:answer' for x in g) else 1)"; then
        pass "#724 ide.generated names the header and its step"
    else fail "#724 no ide.generated entry for the header"; fi
elif has_py; then fail "#724 the plan failed ($(grep -m1 -i error "$d/db.err"))"
else skip "#724 database check (no python3)"; fi
if (cd "$d" && "$M" build > build.log 2>&1) && find "$d/target" -path '*gen/answer.h' | grep -q .; then
    pass "#724 a build writes the header the database names"
else fail "#724 the build did not write the generated header"; fi

echo "== #723: two packages deploy the same bytes to one name"
d="$W/s723"; mkdir -p "$d/app/src" "$d/dep/src"
for p in app dep; do
    printf 'shared payload\n' > "$d/$p/res.txt"
    cat > "$d/$p/build.mcpp" <<EOF
import std;
import mcpp;
int main() {
    const std::string root = mcpp::manifest_dir();
    const std::string src  = root + "/res.txt";
    const std::string out  = std::string(mcpp::out_dir()) + "/gen/shared.bin";
    mcpp::action a;
    a.id   = "$p-gen-shared";
    a.role = "source";
    a.arg("cp").arg(src.c_str()).arg(out.c_str()).input(src.c_str()).output(out.c_str()).submit();
    mcpp::deploy(out.c_str(), "shared");
    return 0;
}
EOF
done
printf '[package]\nname    = "dep"\nversion = "0.1.0"\n\n[targets.dep]\nkind = "lib"\n' > "$d/dep/mcpp.toml"
printf 'export module dep;\nexport int dep_value() { return 3; }\n' > "$d/dep/src/dep.cppm"
printf '[package]\nname    = "app"\nversion = "0.1.0"\n\n[dependencies]\ndep = { path = "../dep" }\n' > "$d/app/mcpp.toml"
printf 'import dep;\nint main() { return dep_value() == 3 ? 0 : 1; }\n' > "$d/app/src/main.cpp"
if (cd "$d/app" && "$M" build > build.log 2>&1); then
    f=$(find "$d/app/target" -path '*/bin/shared/shared.bin' | head -1)
    if [ -n "$f" ] && grep -qx 'shared payload' "$f"; then pass "#723 identical bytes from two packages are placed once"
    else fail "#723 bin/shared/shared.bin is missing"; fi
else fail "#723 ($(grep -m1 -i 'error' "$d/app/build.log"))"; fi
printf 'other payload\n' > "$d/dep/res.txt"
if (cd "$d/app" && "$M" build > diff.log 2>&1); then fail "#723 different bytes were placed without a refusal"
elif grep -q 'shared.bin' "$d/app/diff.log"; then pass "#723 different bytes are refused, naming the destination"
else fail "#723 the refusal does not name the destination ($(grep -m1 -i error "$d/app/diff.log"))"; fi

echo "== progress: an index refresh off a terminal"
if "$M" index update > "$W/iu2.log" 2>&1; then
    if LC_ALL=C grep -q $'\r' "$W/iu2.log"; then fail "progress: the refresh output carries a carriage return"
    elif grep -q 'Updating package index' "$W/iu2.log"; then pass "progress: the refresh reports its steps in plain lines"
    else fail "progress: the refresh printed no step ($(tail -1 "$W/iu2.log"))"; fi
else fail "progress: mcpp index update failed"; fi

echo "== #734 E8/E2/E11: mcpp.core states the build information and renders a diagnostic"
d="$W/s734core"; mkdir -p "$d/src"
printf '[package]\nname    = "core734"\nversion = "0.1.0"\n' > "$d/mcpp.toml"
cat > "$d/build.mcpp" <<'EOF'
import std;
import mcpp.core;
int main() {
    const std::string id = mcpp::toolset_identity();
    const std::string cxx = mcpp::tool("cxx");
    const std::string msg = "toolset identity: " + id;
    mcpp::report({.severity = "note", .message = msg.c_str()});
    return id.empty() || cxx.empty() ? 1 : 0;
}
EOF
printf 'int main() { return 0; }\n' > "$d/src/main.cpp"
if (cd "$d" && "$M" build > build.log 2>&1); then
    if grep -q 'toolset identity: [a-z]' "$d/build.log"; then pass "#734 import mcpp.core reads the toolset identity and reports it ($(grep -o 'toolset identity: [^ ]* [^ ]*' "$d/build.log" | head -1))"
    else fail "#734 the build program's note is not rendered"; fi
else fail "#734 a build program importing mcpp.core does not build ($(grep -m1 -i error "$d/build.log"))"; fi

echo "== #734 E9: [package] mcpp is a floor"
d="$W/s734floor"; mkdir -p "$d/src"
printf 'int main() { return 0; }\n' > "$d/src/main.cpp"
printf '[package]\nname    = "floor734"\nversion = "0.1.0"\nmcpp    = ">=%s"\n' "$VER" > "$d/mcpp.toml"
if (cd "$d" && "$M" build > at.log 2>&1); then pass "#734 a package whose floor is this release builds"
else fail "#734 a floor equal to this release is refused ($(grep -m1 -i error "$d/at.log"))"; fi
printf '[package]\nname    = "floor734"\nversion = "0.1.0"\nmcpp    = ">=2099.1.1.1"\n' > "$d/mcpp.toml"
if (cd "$d" && "$M" build > above.log 2>&1); then fail "#734 a floor above this release was accepted"
elif grep -q 'requires mcpp >= 2099.1.1.1' "$d/above.log"; then pass "#734 a floor above this release stops the build, naming the floor"
else fail "#734 the refusal does not name the floor ($(grep -m1 -i error "$d/above.log"))"; fi

echo "== #734 E5: the fast path resumes after an edit"
d="$W/s734fast"; mkdir -p "$d/src"
printf '[package]\nname    = "fast734"\nversion = "0.1.0"\n' > "$d/mcpp.toml"
printf 'int main() { return 1; }\n' > "$d/src/main.cpp"
(cd "$d" && "$M" build > b0.log 2>&1); sleep 1.1
printf 'int main() { return 0; }\n' > "$d/src/main.cpp"
(cd "$d" && "$M" build > b1.log 2>&1)
if (cd "$d" && "$M" build -v > b2.log 2>&1) && ! grep -q 'declined' "$d/b2.log" && ! grep -q 'Resolving toolchain' "$d/b2.log"; then
    pass "#734 the build after a confirmed edit is replayed by the fast path"
else fail "#734 the fast path did not resume ($(grep -m1 'declined' "$d/b2.log"))"; fi

echo "== #734 E3/E6: pack -p at a workspace root; W3 leaves the package's own members alone"
d="$W/s734pack"; mkdir -p "$d/lib/src" "$d/app/src"
printf '[workspace]\nmembers = ["lib", "app"]\n' > "$d/mcpp.toml"
printf '[package]\nnamespace = "probe"\nname      = "lib734"\nversion   = "0.1.0"\n\n[targets.lib734]\nkind = "lib"\n\n[build]\nsources = ["src/lib734.cppm", "src/inner.cppm"]\n' > "$d/lib/mcpp.toml"
printf 'export module probe.lib734;\nexport int one() { return 1; }\n' > "$d/lib/src/lib734.cppm"
printf 'export module probe.lib734.inner;\nexport int two() { return 2; }\n' > "$d/lib/src/inner.cppm"
printf '[package]\nname    = "app734"\nversion = "0.1.0"\n\n[dependencies.probe]\nlib734 = { path = "../lib" }\n' > "$d/app/mcpp.toml"
printf 'import probe.lib734.inner;\nint main() { return two() == 2 ? 0 : 1; }\n' > "$d/app/src/main.cpp"
if (cd "$d" && "$M" build --workspace > ws.log 2>&1); then
    if grep -q "not one of that package's public modules" "$d/ws.log"; then fail "#734 W3 warned on a member of the package's own workspace"
    else pass "#734 a member's non-public module, imported inside its workspace, draws no W3"; fi
else fail "#734 the workspace does not build ($(grep -m1 -i error "$d/ws.log"))"; fi
if (cd "$d" && "$M" pack -p lib > pack.log 2>&1) && ls "$d"/lib/target/dist/*.tar.gz > /dev/null 2>&1; then
    pass "#734 mcpp pack -p lib at the workspace root writes the member's archive"
else fail "#734 pack -p failed ($(grep -m1 -i error "$d/pack.log"))"; fi

echo "== #734 plugins: a consumer of the published mcpp.plugins reaches L3 and L2"
if [ -z "${PVER:-}" ]; then skip "#734 plugins: PVER (the mcpp.plugins version) is not given"
else
    d="$W/s734plugins"; mkdir -p "$d/src" "$d/data"
    printf '[package]\nname    = "plug734"\nversion = "0.1.0"\n\n[build-dependencies.mcpp]\nplugins = { version = "%s", features = ["tools-embed"], host-module = true }\n' "$PVER" > "$d/mcpp.toml"
    printf 'hello734' > "$d/data/msg.txt"
    cat > "$d/build.mcpp" <<'EOF'
import std;
import mcpp;
import mcpp.tools.embed;
import mcpp.plugins.fs;
int main() {
    mcpp::tools::embed::options o;
    o.name_space = "fx";
    o.null_terminate = true;
    mcpp::plugins::fs::write_if_changed(std::filesystem::path(mcpp::out_dir()) / "l2.txt", "l2\n");
    return mcpp::tools::embed::file("data/msg.txt", o) ? 0 : 1;
}
EOF
    printf '#include <cstdio>\n#include "msg_txt.h"\nint main() { std::puts(reinterpret_cast<const char*>(fx::msg_txt)); return 0; }\n' > "$d/src/main.cpp"
    if (cd "$d" && "$M" run > run.log 2>&1) && grep -qx hello734 "$d/run.log"; then
        pass "#734 mcpp.plugins $PVER from the index: tools-embed (L3) and mcpp.plugins.fs (L2) in one build program"
    else fail "#734 the plugins consumer failed ($(grep -m1 -i error "$d/run.log"))"; fi
fi

# ════════════════════════════════════════════════════════════════════════════
echo "== not run on this kind of host"
skip "Windows behaviour: e2e 820 (the runtime placement over real toolsets, the action PATH, pack, the workspace statement), e2e 811 and 814, and the moc.exe measurement run on the Windows CI rows"
skip "the GNU depfile on Windows (e2e 118's Windows legs) runs on the Windows CI rows"
skip "#734 on Windows and macOS: e2e 825 (the MSVC build information), the fast path on PE and Mach-O (e2e 645, 831, 832), and the deps members' mechanisms (mcpp-plugins' CI rows)"

echo
[ -n "$REHEARSAL" ] && echo "REHEARSAL: $REHEARSAL; this run did not verify a published package"
echo "summary: $ok ok, $failed failed, $notrun not run"
[ "$failed" = 0 ]
