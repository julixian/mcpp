#!/usr/bin/env bash
# requires: mingw-cross
# 818_a_declared_deploy_outranks_a_search_dir_dll_cross.sh -- the Linux-hosted
# stand-in for 811 (SPEC-007 R4.2/R4.3, mcpp#723): one destination, one
# writer. 811 runs on a Windows runner only; the property does not depend on
# the host, because both the plan-time scan of the runtime search directories
# and the post-link `place-dlls` edge run on whatever host builds, so this
# builds the same fixture for `x86_64-windows-gnu` with the cross toolchain.
#
# `app` declares a deploy of a file named `libmathkit.dll` with bytes of its
# own, and names, as a runtime search directory, the directory that holds the
# real `libmathkit.dll` that `app.exe` imports. Criteria:
#   1. the build succeeds: the DLL the scan finds is a derived source and
#      yields to the declared destination, instead of meeting it at `mcpp
#      stage` as a second source with different bytes;
#   2. the declared file is the one beside the program;
#   3. the build warns about the difference, naming the DLL;
#   4. a second build leaves the declared file in place;
#   5. when a `prepare` action fills the search directory during the build,
#      so that only the post-link placement can see the difference, the
#      build without `-v` states it exactly once (SPEC-007 R4.5, the
#      2026-09-28 design WS3): the placement edge's output was shown only on
#      failure or under `-v`, so this warning reached nobody;
#   6. a build in which the placement edge does not run states nothing.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

TRIPLE=x86_64-windows-gnu
MCPP="${MCPP:-mcpp}"

mkdir -p mathkit/src
cat > mathkit/src/mathkit.cppm <<'EOF'
export module mathkit;
export namespace mk { int answer(); }
EOF
cat > mathkit/src/impl.cpp <<'EOF'
module mathkit;
namespace mk { int answer() { return 42; } }
extern "C" int mk_answer_extern() { return mk::answer(); }
EOF
cat > mathkit/mcpp.toml <<'EOF'
[package]
name    = "mathkit"
version = "0.1.0"
[build]
sources = ["src/*.cppm", "src/*.cpp"]
[targets.mathkit]
kind = "shared"
EOF
( cd mathkit && "$MCPP" build --target "$TRIPLE" > build.log 2>&1 ) \
    || fail "mathkit build failed" mathkit/build.log
DLL="$(find mathkit/target -name 'libmathkit.dll' | head -1)"
IMP="$(find mathkit/target -name 'libmathkit.dll.a' | head -1)"
[[ -n "$DLL" && -n "$IMP" ]] || fail "mathkit did not produce a DLL and an import library"
DLLDIR=$(realpath "$(dirname "$DLL")")
LIBDIR=$(realpath "$(dirname "$IMP")")

mkdir -p app/src app/deploy
cat > app/src/main.cpp <<'EOF'
extern "C" int mk_answer_extern();
int main() { return mk_answer_extern() == 42 ? 0 : 1; }
EOF
printf 'not the real DLL\n' > app/deploy/libmathkit.dll
cat > app/mcpp.toml <<'EOF'
[package]
name    = "app"
version = "0.1.0"
[targets.app]
kind = "bin"
main = "src/main.cpp"

[runtime]
deploy = [ { from = "deploy/libmathkit.dll", to = "." } ]
EOF
cat > app/build.mcpp <<EOF
import mcpp;
int main() {
    mcpp::link_flag("-L$(host_path "$LIBDIR")");
    mcpp::link_flag("-Wl,-Bdynamic");
    mcpp::link_lib("mathkit");
    mcpp::runtime_search_dir("$(host_path "$DLLDIR")");
    return 0;
}
EOF

cd app
"$MCPP" build --target "$TRIPLE" > build.log 2>&1 \
    || fail "1: the build failed; the search directory's DLL met the declared deploy as a second source" build.log
EXE="$(find target/$TRIPLE -name 'app.exe' | head -1)"
[[ -n "$EXE" ]] || fail "no app.exe produced" build.log
BINDIR="$(dirname "$EXE")"
echo "ok: 1. the build succeeds"

[[ "$(cat "$BINDIR/libmathkit.dll" 2>/dev/null)" == "not the real DLL" ]] \
    || fail "2: the file beside app.exe is not the declared deploy's" build.log
echo "ok: 2. the declared file is the one beside the program"

grep -q "warning:.*libmathkit.dll\|libmathkit.dll.*differ" build.log \
    || grep -A3 "warning:" build.log | grep -q "libmathkit.dll" \
    || fail "3: the build does not warn about the difference, naming libmathkit.dll" build.log
echo "ok: 3. the build warns about the difference"

"$MCPP" build --target "$TRIPLE" > build2.log 2>&1 || fail "the second build failed" build2.log
[[ "$(cat "$BINDIR/libmathkit.dll")" == "not the real DLL" ]] \
    || fail "4: a second build replaced the declared file" build2.log
echo "ok: 4. a second build leaves the declared file in place"

# 5-6. The same difference in a directory that a `prepare` action fills. At
# planning time the directory is empty, so the plan's scan finds nothing and
# only the placement edge can compare the two files.
cd "$TMP"
DLLABS="$TMP/$DLL"
RT="$TMP/rt"
mkdir -p app2/src app2/deploy
cp app/src/main.cpp app2/src/main.cpp
printf 'not the real DLL\n' > app2/deploy/libmathkit.dll
cat > app2/mcpp.toml <<'EOF'
[package]
name    = "app2"
version = "0.1.0"
[targets.app2]
kind = "bin"
main = "src/main.cpp"

[runtime]
deploy = [ { from = "deploy/libmathkit.dll", to = "." } ]
EOF
cat > app2/build.mcpp <<EOF
import std;
import mcpp;
int main() {
    mcpp::link_flag("-L$(host_path "$LIBDIR")");
    mcpp::link_flag("-Wl,-Bdynamic");
    mcpp::link_lib("mathkit");
    mcpp::runtime_search_dir("$(host_path "$RT")");
    mcpp::action a;
    a.id          = "fill-rt";
    a.role        = mcpp::roles::prepare;
    a.description = "a directory whose DLL arrives during the build";
    a.arg("\${mcpp.self}").arg("stage").arg("--output").arg("$(host_path "$RT")/libmathkit.dll")
     .arg("$(host_path "$DLLABS")")
     .input("$(host_path "$DLLABS")")
     .output((std::string(mcpp::out_dir()) + "/fill-rt.stamp").c_str())
     .output_dir("$(host_path "$RT")")
     .submit();
    return 0;
}
EOF
cd app2
"$MCPP" build --target "$TRIPLE" > build.log 2>&1 \
    || fail "5: the build with a prepare-filled search directory failed" build.log
[[ -f "$RT/libmathkit.dll" ]] || fail "5: the prepare action did not fill its directory" build.log
said=$(grep -c "is placed by this project's deploy list" build.log || true)
[[ "$said" == 1 ]] \
    || fail "5: the difference found by the placement edge was stated $said time(s) without -v, not once" build.log
grep "is placed by this project's deploy list" build.log | grep -q "libmathkit.dll" \
    || fail "5: the statement does not name libmathkit.dll" build.log
echo "ok: 5. a prepare-filled directory's differing DLL is stated once without -v"

"$MCPP" build --target "$TRIPLE" > build2.log 2>&1 || fail "the second app2 build failed" build2.log
if grep -q "is placed by this project's deploy list" build2.log; then
    fail "6: a build in which the placement edge did not run stated its difference again" build2.log
fi
echo "ok: 6. a build without the placement edge states nothing"

echo "PASS: 818_a_declared_deploy_outranks_a_search_dir_dll_cross"
