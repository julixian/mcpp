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
#   C. changing the variable's value re-runs the action.
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

echo "PASS: 799_an_action_runs_with_its_env_and_cwd"
