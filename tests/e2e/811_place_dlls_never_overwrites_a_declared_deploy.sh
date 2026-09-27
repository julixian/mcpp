#!/usr/bin/env bash
# requires: windows
# 811_place_dlls_never_overwrites_a_declared_deploy.sh -- mcpp#723 self-review,
# SPEC-007 R4.2/R4.3: one destination, one writer.
#
# `mcpp place-dlls` (SPEC-007 R4.3) places, beside a linked PE program, every
# non-system DLL it imports transitively from a runtime search directory. It
# used to do this unconditionally, so a declared deploy (`[runtime] deploy`)
# naming the same file could be overwritten by whichever edge ran last. This
# fixture gives `app`'s runtime search directory the REAL `libmathkit.dll`
# (which `app.exe` imports) and, through a plain declared deploy, a DIFFERENT
# file under the very same name. The deploy list is the single authority for
# that destination: `place-dlls` must leave the declared file alone and warn
# about the difference instead of silently choosing one or the other.
#
# Modelled on 794_a_windows_program_finds_a_dll_through_runtime_search_dir.sh,
# which this reuses for the "how does a program come to import a DLL it did
# not declare as a dependency" half of the fixture.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"

mkdir -p mathkit/src
cat > mathkit/src/mathkit.cppm <<'EOF'
export module mathkit;
export namespace mk { int answer(); }
EOF
cat > mathkit/src/impl.cpp <<'EOF'
module mathkit;
namespace mk { int answer() { return 42; } }
extern "C" __declspec(dllexport) int mk_answer_extern() { return mk::answer(); }
EOF
cat > mathkit/mcpp.toml <<'EOF'
[package]
name    = "mathkit"
version = "0.1.0"
[toolchain]
windows = "gcc@16.1.0"
[build]
sources = ["src/*.cppm", "src/*.cpp"]
[targets.mathkit]
kind = "shared"
EOF

MCPP="${MCPP:-mcpp}"
( cd mathkit && "$MCPP" build > build.log 2>&1 ) || {
    cat mathkit/build.log; echo "FAIL: mathkit build failed"; exit 1; }
DLL="$(find mathkit/target -name 'libmathkit.dll' | head -1)"
IMP="$(find mathkit/target -name 'libmathkit.dll.a' | head -1)"
[[ -n "$DLL" && -n "$IMP" ]] || { find mathkit/target -type f; echo "FAIL: mathkit did not produce a DLL + import library"; exit 1; }
DLLDIR_HOST="$(host_path "$(cd "$(dirname "$DLL")" && pwd)")"
LIBDIR_HOST="$(host_path "$(cd "$(dirname "$IMP")" && pwd)")"

mkdir -p app/src
cat > app/src/main.cpp <<'EOF'
extern "C" int mk_answer_extern();
int main() { return mk_answer_extern() == 42 ? 0 : 1; }
EOF

# A file named EXACTLY like the real DLL app.exe will import, with different
# bytes -- `deploy`'s destination filename is the source's own filename
# (docs/04 SS2.11), so the source itself must be named `libmathkit.dll`.
mkdir -p app/deploy
printf 'not the real DLL\n' > app/deploy/libmathkit.dll

cat > app/mcpp.toml <<'EOF'
[package]
name    = "app"
version = "0.1.0"
[toolchain]
windows = "gcc@16.1.0"
[targets.app]
kind = "bin"
main = "src/main.cpp"

[runtime]
deploy = [ { from = "deploy/libmathkit.dll", to = "." } ]
EOF

cat > app/build.mcpp <<EOF
import mcpp;
int main() {
    // Same reasoning as 794: static-mode default needs the dynamic-mode
    // switch immediately before the -l it enables (docs/12).
    mcpp::link_flag("-L$LIBDIR_HOST");
    mcpp::link_flag("-Wl,-Bdynamic");
    mcpp::link_lib("mathkit");
    mcpp::runtime_search_dir("$DLLDIR_HOST");
    return 0;
}
EOF

cd app
"$MCPP" build > build.log 2>&1 || { cat build.log; echo "FAIL: app build failed"; exit 1; }
EXE="$(find target -name 'app.exe' | head -1)"
[[ -n "$EXE" ]] || { cat build.log; echo "FAIL: no app.exe produced"; exit 1; }
BINDIR="$(dirname "$EXE")"

# ── 1. the declared deploy's file stays; place-dlls did not overwrite it ───
[[ -f "$BINDIR/libmathkit.dll" ]] || { echo "FAIL: libmathkit.dll is missing beside app.exe"; exit 1; }
CONTENT="$(cat "$BINDIR/libmathkit.dll")"
[[ "$CONTENT" == "not the real DLL" ]] || {
    echo "FAIL: libmathkit.dll beside app.exe is not the declared deploy's file (got: $CONTENT)"
    exit 1; }

# ── 2. the build reports the difference ─────────────────────────────────────
grep -qi "libmathkit.dll" build.log || {
    cat build.log
    echo "FAIL: the build does not mention libmathkit.dll at all"
    exit 1; }
grep -q "warning:" build.log || {
    cat build.log
    echo "FAIL: the build does not warn about the difference"
    exit 1; }

# ── 3. a second build (place-dlls re-running) leaves the same file in place ─
"$MCPP" build > build2.log 2>&1 || { cat build2.log; echo "FAIL: second build failed"; exit 1; }
CONTENT2="$(cat "$BINDIR/libmathkit.dll")"
[[ "$CONTENT2" == "not the real DLL" ]] || {
    echo "FAIL: a second build replaced the declared deploy's file (got: $CONTENT2)"
    exit 1; }

echo "PASS: 811_place_dlls_never_overwrites_a_declared_deploy"
