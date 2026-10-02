#!/usr/bin/env bash
# requires: unix-shell
# 879_a_providers_module_extension_reaches_the_fast_path.sh -- #756.
#
# A host module that a `path` build-dependency provides is compiled into the
# consumer's build program, which no edge of the consumer's build.ninja names,
# so the fast path has to see an edit to it itself. It sweeps the dependency's
# tree and decides which files can change the graph with an extension table. It
# used the CONSUMER's table, so a `.ixx` that only the provider declares was
# classified as a file of no interest, the edit was replayed as "no work", and
# the build program kept the old module. The consumer cannot be asked to declare
# `.ixx` as a workaround: it has no `.ixx` source, and the dead-entry warning it
# would then receive is correct.
#
# Classification belongs to the package that owns the file. Each root the build
# cache records now carries its own package's `module_extensions` and
# `device_extensions`, and the sweep classifies the files below a root with that
# root's table.
#
# The same provider and consumer are built as a project (the project fast path
# and `mcpp run`'s) and as a workspace member (the workspace fast path).
#
#   A  after a build and a warm build, an edit to `rules.ixx` re-runs the
#      consumer's build program: the program prints the new value, in all three
#      forms;
#   B  after one build that confirmed the edit the next build is replayed again
#      (under -v: no "declined", no "Resolving toolchain"), and the next edit is
#      seen as well;
#   C  the consumer declares nothing about `.ixx`, and no build prints the
#      dead-entry warning;
#   D  a record that lists the roots without their tables declines once, and
#      the build after it is replayed.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"
export NO_COLOR=1

EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
bin_of() { find "$1" -path "*/bin/*" -name "$2$EXE" -type f | head -1; }

# A provider that declares `.ixx` and has one source; a consumer with only a
# `.cpp` source whose build program imports the provider's module.
provider() {
    mkdir -p "$1"
    cat > "$1/mcpp.toml" <<'TOML'
[package]
namespace = "repro"
name      = "rules"
version   = "0.1.0"
standard  = "c++23"

[lib]
path = "rules.ixx"

[build]
sources           = ["rules.ixx"]
module_extensions = [".ixx"]
TOML
    value "$1" 1
}
value() {
    printf 'export module repro.rules;\n\nexport const char* repro_definition() {\n    return "REPRO_VALUE=%s";\n}\n' "$2" > "$1/rules.ixx"
}
consumer() {
    local dir="$1"
    mkdir -p "$dir"
    cat > "$dir/mcpp.toml" <<'TOML'
[package]
namespace = "repro"
name      = "app"
version   = "0.1.0"
standard  = "c++23"

[build]
sources = ["main.cpp"]

[build-dependencies]
"repro.rules" = { path = "../rules", host-module = true }

[targets.app]
kind = "bin"
main = "main.cpp"
TOML
    cat > "$dir/build.mcpp" <<'CPP'
import mcpp;
import repro.rules;

int main() {
    mcpp::define(repro_definition());
    return 0;
}
CPP
    cat > "$dir/main.cpp" <<'CPP'
#include <cstdio>

int main() {
    std::printf("REPRO_VALUE=%d\n", REPRO_VALUE);
}
CPP
}

replayed() {   # replayed <log>: the build was served by the fast path
    ! grep -q "declined" "$1" && ! grep -q "Resolving toolchain" "$1"
}

cd "$TMP"

# ── A project: the consumer builds alone, `mcpp run` runs its program ───────
provider proj/rules
consumer proj/app
cd proj/app
run_value() { "$MCPP" run 2>/dev/null | grep '^REPRO_VALUE=' | tail -1; }

"$MCPP" build > p0.log 2>&1 || fail "project: the first build failed" p0.log
[ "$(run_value)" = "REPRO_VALUE=1" ] || fail "project: the program did not print the provider's value" p0.log
"$MCPP" build -v > p1.log 2>&1 || fail "project: the warm build failed" p1.log
replayed p1.log || fail "project: the warm build was not replayed, so the next assertion measures nothing" p1.log

# A, project build
sleep 1.1
value ../rules 2
"$MCPP" build > p2.log 2>&1 || fail "project: the build after the edit failed" p2.log
bin="$(bin_of target app)"
[ -n "$bin" ] || fail "project: the program was not found" p2.log
[ "$("$bin")" = "REPRO_VALUE=2" ] || fail "A: the edit of the provider's rules.ixx did not reach the program (build)" p2.log

# B
"$MCPP" build -v > p3.log 2>&1 || fail "project: the build after the confirmed edit failed" p3.log
replayed p3.log || fail "B: the build after a confirmed edit was not replayed by the fast path" p3.log

# A, project run: the fast path of `mcpp run`
sleep 1.1
value ../rules 3
out="$("$MCPP" run 2>&1)" || fail "project: the run after the second edit failed"$'\n'"$out"
grep -q '^REPRO_VALUE=3$' <<<"$out" || fail "A: the edit of the provider's rules.ixx did not reach the program (run)"$'\n'"$out"

# D. A record that listed the roots without their tables (an engine before this
# one wrote that) declines once, and the record written after it is replayed.
"$MCPP" build > p4.log 2>&1 || fail "project: the build before ageing the record failed" p4.log
grep -q '^depSources=1$' target/.build_cache || fail "D: the record does not carry the root with its tables" target/.build_cache
awk '/^depSources=/ { n = substr($0, 12) + 0; print "depSourceRoots=" n
                      while (n-- > 0) { getline; split($0, f, "\t"); print f[1] }
                      next }
     { print }' target/.build_cache > target/.build_cache.aged
mv target/.build_cache.aged target/.build_cache
grep -q '^depSourceRoots=1$' target/.build_cache || fail "D: the record was not aged" target/.build_cache
"$MCPP" build -v > d1.log 2>&1 || fail "D: the build with an aged record failed" d1.log
grep -q "predates the list of path-dependency roots" d1.log || fail "D: the decline did not name the missing tables" d1.log
"$MCPP" build -v > d2.log 2>&1 || fail "D: the build after the aged record failed" d2.log
replayed d2.log || fail "D: the record written after the decline was not replayed" d2.log
cd "$TMP"

# ── A workspace member: the workspace fast path ─────────────────────────────
mkdir ws
printf '[workspace]\nmembers = ["app"]\n' > ws/mcpp.toml
provider ws/rules
consumer ws/app
cd ws

"$MCPP" build -p app > w0.log 2>&1 || fail "workspace: the first build failed" w0.log
"$MCPP" build -p app -v > w1.log 2>&1 || fail "workspace: the warm build failed" w1.log
replayed w1.log || fail "workspace: the warm build was not replayed, so the next assertion measures nothing" w1.log
bin="$(bin_of target app)"
[ -n "$bin" ] && [ "$("$bin")" = "REPRO_VALUE=1" ] || fail "workspace: the program did not print the provider's value" w0.log

# A, workspace
sleep 1.1
value rules 2
"$MCPP" build -p app > w2.log 2>&1 || fail "workspace: the build after the edit failed" w2.log
[ "$("$bin")" = "REPRO_VALUE=2" ] || fail "A: the edit of the provider's rules.ixx did not reach the program (workspace)" w2.log

# B
"$MCPP" build -p app -v > w3.log 2>&1 || fail "workspace: the build after the confirmed edit failed" w3.log
replayed w3.log || fail "B: the workspace build after a confirmed edit was not replayed by the fast path" w3.log

# C
for f in "$TMP"/proj/app/*.log "$TMP"/ws/*.log; do
    if grep -q "dead entry" "$f"; then fail "C: a build printed the dead-entry warning" "$f"; fi
done
grep -q "module_extensions" "$TMP/proj/app/mcpp.toml" && fail "C: the consumer declares module_extensions, which the criterion forbids"

echo "OK"
