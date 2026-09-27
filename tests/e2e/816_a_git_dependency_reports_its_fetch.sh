#!/usr/bin/env bash
# requires: unix-shell
# 816_a_git_dependency_reports_its_fetch.sh — fetching a git dependency is
# reported with the renderer every other acquisition uses (W11).
#
# The clone's output used to be captured whole and shown only on failure, so a
# large repository printed nothing until it had arrived. The download phase is
# now drawn as a bar. When stdout is not a terminal the bar is one line when the
# item starts and one line when it finishes, with no carriage return and no
# erase sequence, so a CI log is not filled with repaints. Criteria:
#   A. the build prints a `Fetching <url>` start line and a finish line;
#   B. the captured output contains no carriage return and no ESC byte.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; [ -n "${2:-}" ] && cat "$2"; exit 1; }

mkdir -p "$TMP/repo/src" && cd "$TMP/repo"
cat > mcpp.toml <<'EOF'
[package]
namespace = "probe816"
name = "gdep"
version = "0.1.0"

[targets.gdep]
kind = "lib"
EOF
printf 'export module probe816.gdep;\nexport int gdep_value() { return 7; }\n' > src/gdep.cppm
git init -q -b main . && git add -A \
    && git -c user.email=e2e@mcpp -c user.name=e2e commit -qm init

mkdir -p "$TMP/consumer/src" && cd "$TMP/consumer"
cat > mcpp.toml <<EOF
[package]
name = "consumer"
version = "0.1.0"

[dependencies]
"probe816.gdep" = { git = "file://$TMP/repo", branch = "main" }
EOF
printf 'import probe816.gdep;\nint main() { return gdep_value() == 7 ? 0 : 1; }\n' > src/main.cpp

"$MCPP" build > build.log 2>&1 || fail "the build failed" build.log
grep -q "Fetching file://$TMP/repo" build.log || fail "A: no start line for the fetch" build.log
grep -q "Fetching file://$TMP/repo.* done" build.log || fail "A: no finish line for the fetch" build.log
if LC_ALL=C grep -q $'\r' build.log; then fail "B: the log carries a carriage return" build.log; fi
if LC_ALL=C grep -q $'\x1b' build.log; then fail "B: the log carries an ESC byte" build.log; fi
echo "ok: A, B"
echo "PASS: 816_a_git_dependency_reports_its_fetch"
