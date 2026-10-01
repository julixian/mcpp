#!/usr/bin/env bash
# requires: llvm unix-shell symlink
# A toolchain named by path (mcpp#755): `[toolchain] default = { path = ... }`
# and `MCPP_TOOLCHAIN=path:<dir>`. mcpp probes the drivers in the tree, drives
# them with its own link model, writes nothing into the tree, and reports the
# source.
#
# THE TREE HAS NO CFG. It is the installed LLVM payload seen through symlinks,
# without the `.cfg` files mcpp generates for a payload it manages -- the shape
# of an LLVM release package extracted by hand.
set -e

MCPP="${MCPP:-mcpp}"
source "$(dirname "$0")/_llvm_env.sh"
if [[ ! -x "$LLVM_ROOT/bin/clang++" ]]; then
    echo "SKIP: no llvm payload installed ($LLVM_ROOT)"; exit 0
fi
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export NO_COLOR=1

T="$work/llvm"
mkdir -p "$T/bin"
for f in "$LLVM_ROOT"/bin/*; do
    case "$f" in *.cfg) ;; *) ln -s "$f" "$T/bin/" ;; esac
done
for d in include lib share libexec; do [ -e "$LLVM_ROOT/$d" ] && ln -s "$LLVM_ROOT/$d" "$T/$d"; done
# A linker stated by role, as a wrapper the test can change in place.
printf '#!/bin/sh\nexec "%s/bin/ld.lld" "$@"\n' "$LLVM_ROOT" > "$work/ld-wrapper"
chmod +x "$work/ld-wrapper"

mkdir -p "$work/app/src"
cat > "$work/app/src/main.cpp" <<'CPP'
#include <cstdio>
int main() { std::puts("built by a toolchain named by path"); return 0; }
CPP
write_manifest() {
    cat > "$work/app/mcpp.toml" <<TOML
[package]
name    = "app"
version = "0.1.0"

[toolchain]
default = { path = "$1"$2 }
TOML
}
cd "$work/app"
fail() { echo "FAIL: $*"; echo "----"; echo "$out"; exit 1; }

write_manifest "$T" ', launcher = "/usr/bin/env", tools = { ld = "'"$work/ld-wrapper"'" }'
rm -rf target
out="$("$MCPP" build 2>&1)" || fail "the build failed"
grep -q "Using toolchain clang .* ← $T  \[custom · mcpp.toml:[0-9]*\]" <<<"$out" || fail "no Using line for the toolchain"
grep -q "Finished .* · custom: toolchain" <<<"$out" || fail "Finished does not summarise the source"
run="$(./target/*/*/bin/app)"
[[ "$run" == "built by a toolchain named by path" ]] || fail "the program did not run: $run"
ninja="$(cat target/*/*/build.ninja)"
grep -q "^cxx *= /usr/bin/env $T/bin/clang++" <<<"$ninja" || fail "the launcher does not prefix the compiler"
grep -q -- "--ld-path=$work/ld-wrapper" <<<"$ninja" || fail "the stated linker is not used"
[[ -z "$(find "$T/" -maxdepth 2 -name '*.cfg' -print -quit)" ]] || fail "mcpp wrote a cfg into the tree"

# The fast path serves an unchanged tree, and declines once a program of it changed.
out="$("$MCPP" build -v 2>&1)"
grep -q "fast-path: build declined" <<<"$out" && fail "an unchanged build declined the fast path"
touch -d '+1 minute' "$work/ld-wrapper" 2>/dev/null || { sleep 1; touch "$work/ld-wrapper"; }
out="$("$MCPP" build -v 2>&1)"
grep -q "a program of the toolchain named by path changed" <<<"$out" || fail "a changed linker was not noticed"

# MCPP_TOOLCHAIN=path:<dir>, with no table.
cat > "$work/app/mcpp.toml" <<'TOML'
[package]
name    = "app"
version = "0.1.0"
TOML
rm -rf target
out="$(MCPP_TOOLCHAIN="path:$T" "$MCPP" build 2>&1)" || fail "MCPP_TOOLCHAIN=path: failed"
grep -q "\[custom · env MCPP_TOOLCHAIN\]" <<<"$out" || fail "the env-named toolchain is not reported"

# A stated family the drivers contradict is refused.
write_manifest "$T" ', family = "gcc", tools = { cxx = "'"$T/bin/clang++"'" }'
rm -rf target
out="$("$MCPP" build 2>&1 || true)"
grep -q 'stated as family "gcc"' <<<"$out" || fail "a contradicting family was accepted"

# A tree without a driver is refused at the declaration.
mkdir -p "$work/empty/bin"
write_manifest "$work/empty" ''
out="$("$MCPP" build 2>&1 || true)"
grep -q "no C++ driver in bin/" <<<"$out" || fail "a tree without a driver was accepted"

echo "PASS: a toolchain named by path builds, is reported, and is identified"
