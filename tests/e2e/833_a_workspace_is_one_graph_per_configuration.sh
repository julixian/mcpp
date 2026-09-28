#!/usr/bin/env bash
# requires: unix-shell
# 833_a_workspace_is_one_graph_per_configuration.sh -- workspace design
# 2026-09-29 (§4, §5, §7, §11).
#
# A workspace is planned as one graph per configuration, with a virtual root
# that depends on the selected members. The criteria of §11:
#
#   G1  a member two others use is compiled once in a workspace build;
#   G2  `-p X` after `--workspace` compiles nothing: one build directory;
#   G3  an edit to one member's flags recompiles that member only;
#   G4  a member of another configuration (another C++ standard) builds, in
#       its own build directory;
#   G5  two members with a program of the same name both build, each in its
#       product directory `bin/<package name>/`;
#   G6  two members with one package name in two namespaces get qualified
#       product directories;
#   G7  no member directory receives a build directory, and `clean --stale`
#       removes the ones members held before, keeping `target/.build-mcpp/`.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac

cat > mcpp.toml <<'EOF'
[workspace]
members = ["core", "cli", "gui", "ns1/tool", "ns2/tool", "modern"]
EOF

mkdir -p core/src cli/src gui/src ns1/tool/src ns2/tool/src modern/src
cat > core/mcpp.toml <<'EOF'
[package]
name = "core"
version = "0.1.0"

[targets.core]
kind = "lib"
EOF
printf 'export module shared_core;\nexport int core_v() { return 40; }\n' > core/src/core.cppm

for m in cli gui; do
    cat > $m/mcpp.toml <<EOF
[package]
name = "$m"
version = "0.1.0"

[dependencies]
core = { path = "../core" }

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
done
printf '#include <cstdio>\nimport shared_core;\nint main() { std::printf("%%d\\n", core_v() + 1); return 0; }\n' > cli/src/main.cpp
printf '#include <cstdio>\nimport shared_core;\nint main() { std::printf("%%d\\n", core_v() + 2); return 0; }\n' > gui/src/main.cpp

for ns in ns1 ns2; do
    cat > $ns/tool/mcpp.toml <<EOF
[package]
namespace = "$ns"
name = "tool"
version = "0.1.0"

[targets.tool]
kind = "bin"
main = "src/main.cpp"
EOF
    printf 'int main() { return 0; }\n' > $ns/tool/src/main.cpp
done

cat > modern/mcpp.toml <<'EOF'
[package]
name = "modern"
version = "0.1.0"
standard = "c++26"

[targets.modern]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > modern/src/main.cpp

"$MCPP" build --workspace > ws.log 2>&1 || fail "the workspace build failed" ws.log

# G4: two configurations, two build directories.
dirs=$(find target -mindepth 2 -maxdepth 2 -type d -path 'target/*/*' ! -name '.*' | sort)
[ "$(echo "$dirs" | wc -l | tr -d ' ')" = 2 ] || fail "G4: expected two build directories, got: $dirs" ws.log
main_dir=""
for d in $dirs; do
    [ -f "$d/bin/cli/app$EXE" ] && main_dir="$d"
done
[ -n "$main_dir" ] || fail "G5: bin/cli/app not found in any build directory" ws.log
[ -f "$main_dir/bin/gui/app$EXE" ] || fail "G5: bin/gui/app not beside bin/cli/app" ws.log
other=$(echo "$dirs" | grep -v "^$main_dir$")
[ -f "$other/bin/modern/modern$EXE" ] || fail "G4: the c++26 member is not in its own build directory" ws.log

# G5: each program is its member's.
[ "$("$main_dir/bin/cli/app$EXE")" = 41 ] || fail "G5: bin/cli/app is not cli's program"
[ "$("$main_dir/bin/gui/app$EXE")" = 42 ] || fail "G5: bin/gui/app is not gui's program"

# G6: qualified product directories for one name in two namespaces.
[ -f "$main_dir/bin/ns1.tool/tool$EXE" ] || fail "G6: bin/ns1.tool/tool missing" ws.log
[ -f "$main_dir/bin/ns2.tool/tool$EXE" ] || fail "G6: bin/ns2.tool/tool missing" ws.log

# G1: the core module is compiled once, in the one graph.
log="$main_dir/.ninja_log"
n=$(grep -c 'core\.m\.o' "$log" || true)
[ "$n" = 1 ] || fail "G1: core's object was built $n times" "$log"

# G2: `-p cli` after `--workspace` compiles nothing.
before=$(wc -l < "$log")
"$MCPP" build -p cli > p.log 2>&1 || fail "G2: -p cli failed" p.log
after=$(wc -l < "$log")
[ "$before" = "$after" ] || fail "G2: -p cli recompiled after --workspace" "$log"

# G3: an edit to gui's flags recompiles gui only.
sleep 1.1
cat >> gui/mcpp.toml <<'EOF'

[build]
cxxflags = ["-DGUI_EXTRA=1"]
EOF
before=$(wc -l < "$log")
"$MCPP" build --workspace > g3.log 2>&1 || fail "G3: the build after the edit failed" g3.log
tail -n +$((before + 1)) "$log" | cut -f4 > g3.edges
grep -q 'gui' g3.edges || fail "G3: gui was not rebuilt after its flags changed" g3.edges
if grep -Eq 'core|/cli/|_cli|ns1|ns2' g3.edges; then
    fail "G3: an edit to gui's flags rebuilt another member" g3.edges
fi
[ -d "$main_dir" ] || fail "G3: the flag edit moved the build directory"

# G7: no member directory holds a build directory; the old ones are stale.
for m in core cli gui ns1/tool ns2/tool modern; do
    [ ! -d "$m/target/$(basename "$(dirname "$main_dir")")" ] \
        || fail "G7: $m received a build directory"
done
mkdir -p cli/target/x86_64-linux-gnu/0123456789abcdef cli/target/.build-mcpp
echo keep > cli/target/.build-mcpp/keep
"$MCPP" clean --stale > clean.log 2>&1 || fail "G7: clean --stale failed" clean.log
[ ! -d cli/target/x86_64-linux-gnu/0123456789abcdef ] \
    || fail "G7: clean --stale kept a member's old build directory" clean.log
[ -f cli/target/.build-mcpp/keep ] || fail "G7: clean --stale removed a member's build-program outputs" clean.log

echo "PASS: 833_a_workspace_is_one_graph_per_configuration"
