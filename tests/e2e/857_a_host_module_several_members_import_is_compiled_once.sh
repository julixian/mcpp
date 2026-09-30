#!/usr/bin/env bash
# requires:
# 857 -- a host module that several members import is compiled once for all of
# them, and a host module of a path dependency is never kept globally.
#
# #748 (B1, D4 of the plan for member selection and build programs). Every
# member of a workspace has a build program, and each imports the same host
# module, which imports the bundled `mcpp` module. Until this change each
# program compiled the `mcpp` module and every host module it imports into its
# own `target/.build-mcpp`, before its own compile and again for every program
# and every invocation: 7.8 s per program on #748's runner.
#
# The host module here is a path dependency, so its sources can change without
# its name and version changing, and it is kept where the project keeps
# everything that is its own: under the workspace's `target/`. The engine's own
# `mcpp` module is identical for every project and is kept in the global cache.
#
# Criteria:
#   A. `m1` and `m2` import one host module and agree on every flag; the module
#      is compiled once for the two, and the second program reuses the entry.
#      (The compile commands are logged under `buildmcpp-host`.)
#   B. `m3` states another standard. Its flags differ, so it gets an entry of
#      its own and never takes the BMI of the others.
#   C. The entries are under the workspace root's `target/.build-mcpp/
#      host-modules/`, each recording the inputs it was compiled from, and not
#      under any member's own `target/`.
#   D. With `--cache global`, no entry of the host module is written below the
#      global cache root; the engine's `mcpp` module is.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

export MCPP_HOME="$TMP/mcpp-home"
source "$(dirname "$0")/_inherit_toolchain.sh"

mkdir -p "$TMP/ws"
cd "$TMP/ws"

cat > mcpp.toml <<'EOF'
[workspace]
members = ["m1", "m2", "m3"]
EOF

# The rule package is a path dependency beside the members and is not one of
# them: it is built only for the build programs that import it.
mkdir -p rules/src
cat > rules/mcpp.toml <<'EOF'
[package]
name    = "rule857"
version = "0.1.0"

[targets.rule857]
kind = "lib"
EOF
cat > rules/src/rule857.cppm <<'EOF'
export module t857.rules;
import mcpp;
export int rule_value() { return 7; }
export void apply(const char* define) { mcpp::define(define); }
EOF

for m in m1 m2 m3; do
    mkdir -p $m/src
    {
        echo '[package]'
        echo "name    = \"$m\""
        echo 'version = "0.1.0"'
        [ "$m" = m3 ] && echo 'standard = "c++20"'
        echo
        echo '[build-dependencies]'
        echo 'rule857 = { path = "../rules", host-module = true }'
        echo
        echo "[targets.$m]"
        echo 'kind = "bin"'
        echo 'main = "src/main.cpp"'
    } > $m/mcpp.toml
    printf 'int main() { return 0; }\n' > $m/src/main.cpp
    cat > $m/build.mcpp <<EOF
import mcpp;
import t857.rules;
int main() {
    if (rule_value() != 7) return 1;
    apply("T857_${m}=1");
    return 0;
}
EOF
done

MCPP_VERBOSE=1 "$MCPP" build --workspace --cache global -j 4 > a.log 2>&1 \
    || fail "the workspace did not build" a.log
for m in m1 m2 m3; do
    grep -qE "^ *build\.mcpp $m .* ran " a.log || fail "the program of $m did not run" a.log
done

STORE=target/.build-mcpp/host-modules

# A and B. Three programs, two flag sets: two compiles, and one reuse.
compiles=$(grep -cE "host module 't857\.rules' (precompile|compile):" a.log || true)
[ "$compiles" = 2 ] || fail "A/B: the host module was compiled $compiles times for three programs of two flag sets (want 2)" a.log
reused=$(grep -cE "host module 't857\.rules': entry [0-9a-f]+ workspace \(reused\)" a.log || true)
[ "$reused" = 1 ] || fail "A: $reused programs reused the host module's entry (want 1: m1 and m2 share one)" a.log
entries=$(find "$STORE" -name entry.json | wc -l | tr -d ' ')
[ "$entries" = 2 ] || fail "B: the store holds $entries entries of the host module (want 2: one per standard)" a.log
# The two entries state different standards, which is what keeps them apart.
flags=$(for e in $(find "$STORE" -name entry.json); do
            grep -h '"std_flag"' "$e"; done | sort -u | wc -l | tr -d ' ')
[ "$flags" = 2 ] || fail "B: the two entries record $flags distinct standard flags (want 2)"

# C. One store for the workspace, below the workspace root; none per member.
for m in m1 m2 m3; do
    [ ! -d "$m/target/.build-mcpp/host-modules" ] \
        || fail "C: $m keeps its own host-module store beside the workspace's"
done
for e in $(find "$STORE" -name entry.json); do
    grep -q '"module": "t857.rules"' "$e" || fail "C: an entry does not record its module name" "$e"
    grep -q '"interface_sha256"' "$e"      || fail "C: an entry does not record the digest of its interface" "$e"
    grep -q '"imports"' "$e"               || fail "C: an entry does not record the BMIs it imports" "$e"
done

# D. The path dependency's module is not below the global cache root; the
# engine's module is.
CACHE_ROOT="$("$MCPP" cache dir | head -1)"
if [ -d "$CACHE_ROOT/pkg" ] && grep -rl 't857.rules' "$CACHE_ROOT/pkg" --include=entry.json 2>/dev/null | grep -q .; then
    fail "D: a host module of a path dependency was written below the global cache root"
fi
[ -n "$(find "$CACHE_ROOT/pkg/_engine" -name entry.json 2>/dev/null)" ] \
    || fail "D: the engine's bundled module is not in the global cache" a.log

echo "PASS: 857_a_host_module_several_members_import_is_compiled_once"
