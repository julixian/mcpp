#!/usr/bin/env bash
# 799_an_action_runs_with_its_env_and_cwd.sh — mcpp#708, protocol 13.
#
# An action's command is an argv with no shell assumed (SPEC-007 R3.1), so a
# generator configured through environment variables, or one that must run in
# a particular directory, had no portable way to be declared: `NAME=value cmd`
# and `cd dir &&` are shell syntax. `mcpp::action::env(name, value)` and
# `mcpp::action::cwd(dir)` state both, and the engine's wrapper applies them.
#
# Criteria:
#   A. the command sees the variable, and runs in the package-relative
#      directory `cwd` names;
#   B. the declared output lands where it was declared, not under `cwd`;
#   C. changing the variable's value re-runs the action;
#   D. a directory the engine prepends to PATH (`--path-prepend`, which every
#      action of an MSVC-ABI build receives since 2026.9.28.2, the 2026-09-28
#      design D3) goes before the PATH the action declares with `env`, and
#      before the inherited PATH when it declares none. The graph half, that
#      the flag is emitted, is unit-tested (NinjaBackendPeRuntime); this is
#      the wrapper's half, where it meets R3.8.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
mkdir -p src tools

cat > mcpp.toml <<'EOF'
[package]
name    = "envcwd"
version = "0.1.0"
EOF
cat > src/main.cpp <<'EOF'
#include <cstdio>
int main() { std::printf("ENV_CWD_OK\n"); }
EOF

write_program() {   # $1 = the value of GREETING
    cat > build.mcpp <<EOF
#include <string>
import mcpp;
int main() {
    const std::string out = std::string(mcpp::out_dir()) + "/probe.txt";
    mcpp::action a;
    a.id   = "probe";
    a.role = mcpp::roles::check;
    a.env("GREETING", "$1")
     .cwd("tools")
     .arg("sh").arg("-c")
     .arg("printf '%s\\\\n' \"\$GREETING\" > \"\$1\"; pwd -P >> \"\$1\"")
     .arg("sh").arg(out.c_str())
     .output(out.c_str())
     .submit();
}
EOF
}

probe() { find target -name probe.txt | head -1; }

write_program hello
"$MCPP" build > b1.log 2>&1 || { cat b1.log; echo "FAIL: build failed"; exit 1; }
p="$(probe)"
[[ -n "$p" ]] || { cat b1.log; echo "FAIL: B: the declared output was not written"; exit 1; }
[[ "$(sed -n 1p "$p")" == "hello" ]] || {
    cat "$p"; echo "FAIL: A: the command did not see GREETING"; exit 1; }
want="$(cd tools && pwd -P)"
[[ "$(sed -n 2p "$p")" == "$want" ]] || {
    cat "$p"; echo "FAIL: A: the command did not run in $want"; exit 1; }
[[ ! -e tools/probe.txt ]] || { echo "FAIL: B: the output landed under cwd"; exit 1; }
echo "ok: A, B"

write_program goodbye
"$MCPP" build > b2.log 2>&1 || { cat b2.log; echo "FAIL: rebuild failed"; exit 1; }
[[ "$(sed -n 1p "$(probe)")" == "goodbye" ]] || {
    cat "$(probe)"; echo "FAIL: C: a changed value did not re-run the action"; exit 1; }
echo "ok: C"

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        # The separator is ';' there, and an msys shell rewrites PATH on entry.
        echo "ok: D not read on this host; the graph half is unit-tested" ;;
    *)
        "$MCPP" __action --env PATH=/declared/bin --path-prepend /first/bin \
            -- /bin/sh -c 'printf "%s" "$PATH" > "$1"' sh "$TMP/d1.txt" \
            || { echo "FAIL: D: the wrapper failed"; exit 1; }
        [[ "$(cat "$TMP/d1.txt")" == "/first/bin:/declared/bin" ]] || {
            echo "FAIL: D: with a declared PATH the command saw '$(cat "$TMP/d1.txt")'"; exit 1; }
        "$MCPP" __action --path-prepend /first/bin \
            -- /bin/sh -c 'printf "%s" "$PATH" > "$1"' sh "$TMP/d2.txt" \
            || { echo "FAIL: D: the wrapper failed"; exit 1; }
        [[ "$(cat "$TMP/d2.txt")" == "/first/bin:$PATH" ]] || {
            echo "FAIL: D: without a declared PATH the command saw '$(cat "$TMP/d2.txt")'"; exit 1; }
        echo "ok: D" ;;
esac

echo "PASS: 799_an_action_runs_with_its_env_and_cwd"
