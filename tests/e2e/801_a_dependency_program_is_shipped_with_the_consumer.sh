#!/usr/bin/env bash
# requires: gcc elf
# 801_a_dependency_program_is_shipped_with_the_consumer.sh — mcpp#711.
#
# `x = { path = "...", artifacts = ["updater"] }` asks for the dependency's
# `bin` target built for the CONSUMER's target and profile, as a link unit of
# the consumer's own plan, beside the consumer's programs in `bin/`. The only
# edge that exposed another package's executable before was `tools`, which
# builds it for the build machine in a nested sub-build: a cross build shipped
# a program for the wrong architecture, built a second time.
#
# Criteria:
#   1. the dependency's program is built into `bin/` and runs;
#   2. none of its code is linked into the consumer (an artifact edge takes the
#      program, not the package's code);
#   3. `${mcpp.artifact:updater/updater}` names it in an action;
#   4. `mcpp pack` stages it beside the program;
#   5. under `--target x86_64-linux-musl` it is built for that target (static,
#      no PT_INTERP) -- run where a musl toolchain is available.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"

mkdir -p updater/src app/src
cat > updater/mcpp.toml <<'EOF'
[package]
name    = "updater"
version = "0.1.0"

[targets.updater]
kind = "bin"
main = "src/main.cpp"
EOF
cat > updater/src/helper.cpp <<'EOF'
int updater_only_helper() { return 7; }
EOF
cat > updater/src/main.cpp <<'EOF'
#include <cstdio>
int updater_only_helper();
int main() { std::printf("UPDATER=%d\n", updater_only_helper()); }
EOF

cat > app/mcpp.toml <<'EOF'
[package]
name    = "app"
version = "0.1.0"

[dependencies]
updater = { path = "../updater", artifacts = ["updater"] }

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
cat > app/src/main.cpp <<'EOF'
#include <cstdio>
int main() { std::printf("APP_OK\n"); }
EOF
cat > app/build.mcpp <<'EOF'
#include <string>
import mcpp;
int main() {
    const std::string out = std::string(mcpp::out_dir()) + "/updater.copy";
    mcpp::action a;
    a.id   = "copy-updater";
    a.role = mcpp::roles::artifact;
    a.arg("cp").arg("${mcpp.artifact:updater/updater}").arg(out.c_str())
     .input("${mcpp.artifact:updater/updater}")
     .output(out.c_str())
     .submit();
}
EOF

cd app
"$MCPP" build > b1.log 2>&1 || { cat b1.log; echo "FAIL: build failed"; exit 1; }

# ── 1 ──
upd="$(find target -path '*/bin/updater' -type f | head -1)"
[[ -n "$upd" ]] || { cat b1.log; find target -name 'updater*'; echo "FAIL: 1: no bin/updater"; exit 1; }
[[ "$("$upd")" == "UPDATER=7" ]] || { echo "FAIL: 1: the artifact does not run"; exit 1; }
out="$("$MCPP" run 2>&1 | tail -1)"
[[ "$out" == "APP_OK" ]] || { echo "FAIL: 1: mcpp run chose '$out', not the package's program"; exit 1; }
echo "ok: 1"

# ── 2 ──
app="$(dirname "$upd")/app"
if nm "$app" 2>/dev/null | grep -q updater_only_helper; then
    echo "FAIL: 2: the dependency's code was linked into the consumer"; exit 1
fi
echo "ok: 2"

# ── 3 ──
copy="$(find target -name updater.copy -type f | head -1)"
[[ -n "$copy" ]] && cmp -s "$copy" "$upd" || {
    echo "FAIL: 3: \${mcpp.artifact:} did not reach the action"; exit 1; }
echo "ok: 3"

# ── 4 ──
"$MCPP" pack > p.log 2>&1 || { cat p.log; echo "FAIL: 4: pack failed"; exit 1; }
tarball="$(find target/dist -name '*.tar.gz' | head -1)"
[[ -n "$tarball" ]] || { cat p.log; echo "FAIL: 4: no archive"; exit 1; }
tar -tzf "$tarball" | grep -q '/updater$' || {
    tar -tzf "$tarball"; echo "FAIL: 4: the artifact is not in the archive"; exit 1; }
echo "ok: 4"

# ── 5 ──
if [[ "$(uname -m)" == x86_64 ]] && { command -v x86_64-linux-musl-g++ >/dev/null 2>&1 \
     || ls "${MCPP_HOME:-$HOME/.mcpp}"/registry/data/xpkgs/xim-x-musl-gcc/*/bin/x86_64-linux-musl-g++ >/dev/null 2>&1; }; then
    "$MCPP" build --target x86_64-linux-musl > b5.log 2>&1 || { cat b5.log; echo "FAIL: 5: cross build failed"; exit 1; }
    cu="$(find target/x86_64-linux-musl -path '*/bin/updater' -type f | head -1)"
    [[ -n "$cu" ]] || { echo "FAIL: 5: no cross-built updater"; exit 1; }
    if readelf -l "$cu" | grep -q INTERP; then
        echo "FAIL: 5: the artifact was not built for the musl target"; exit 1
    fi
    echo "ok: 5"
else
    echo "skip: 5 (no musl toolchain on this machine)"
fi

echo "PASS: 801_a_dependency_program_is_shipped_with_the_consumer"
