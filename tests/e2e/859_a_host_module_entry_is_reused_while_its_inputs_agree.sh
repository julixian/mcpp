#!/usr/bin/env bash
# requires:
# 859 -- an entry of a host module is reused across invocations, and only while
# the inputs it records agree with the ones the compile would have.
#
# #748 (B1). A host module from a path dependency is kept in the workspace's
# store, keyed by what it is compiled from. A hit compares the recorded inputs
# field by field (mcpp.bmi_cache::probe_cached), and never the hash alone. The
# sources of such a package can change without its name and version changing,
# so the key holds the interface's digest and the digest of the tree the
# interface sits in: an included header beside it is part of what was compiled.
#
# Criteria:
#   A. A second run of a stale program reuses the entry: nothing is compiled.
#   B. An edit to the interface compiles a new entry.
#   C. An edit to a header the interface includes, and not to the interface
#      itself, compiles a new entry. The program of the project that imports
#      the module re-runs either way; what must not happen is that it takes the
#      old BMI.
#   D. An entry whose recorded inputs disagree with the compile's (here, its
#      standard flag edited in place) is a miss, and it is replaced.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

export MCPP_HOME="$TMP/mcpp-home"
source "$(dirname "$0")/_inherit_toolchain.sh"

mkdir -p "$TMP/app/src" "$TMP/rules/src"
cd "$TMP/app"

cat > "$TMP/rules/mcpp.toml" <<'EOF'
[package]
name    = "rule859"
version = "0.1.0"

[targets.rule859]
kind = "lib"
EOF
printf '#define T859_VALUE 1\n' > "$TMP/rules/src/value.h"
cat > "$TMP/rules/src/rule859.cppm" <<'EOF'
module;
#include "value.h"
export module t859.rules;
import mcpp;
export int rule_value() { return T859_VALUE; }
EOF
cat > mcpp.toml <<'EOF'
[package]
name    = "app859"
version = "0.1.0"

[build-dependencies]
rule859 = { path = "../rules", host-module = true }

[targets.app859]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > src/main.cpp
cat > build.mcpp <<'EOF'
import mcpp;
import t859.rules;
int main() {
    mcpp::rerun_if_changed("marker.txt");
    // Said on every run, so the log states which value the BMI carried.
    mcpp::warning(rule_value() == 1 ? "T859 value one"
                : rule_value() == 2 ? "T859 value two"
                :                     "T859 value other");
    return 0;
}
EOF
echo 0 > marker.txt

STORE=target/.build-mcpp/host-modules
compile_lines() { grep -cE "host module 't859\.rules' (precompile|compile):" "$1" || true; }
entry_count()   { find "$STORE" -name entry.json | wc -l | tr -d ' '; }
# A stale program: its declared input changed, the sources of its imports did not.
rerun() { echo "$1" > marker.txt; MCPP_VERBOSE=1 "$MCPP" build > "$2" 2>&1 || fail "the build failed ($2)" "$2"; }

MCPP_VERBOSE=1 "$MCPP" build > first.log 2>&1 || fail "the first build failed" first.log
[ "$(compile_lines first.log)" = 1 ] || fail "the first build compiled the host module $(compile_lines first.log) times" first.log
[ "$(entry_count)" = 1 ] || fail "the first build left $(entry_count) entries"
grep -q "T859 value one" first.log || fail "the program did not read the host module's value" first.log

# A
rerun 1 a.log
[ "$(compile_lines a.log)" = 0 ] || fail "A: a stale program compiled its host module again" a.log
grep -qE "host module 't859\.rules': entry [0-9a-f]+ workspace \(reused\)" a.log \
    || fail "A: the log does not say the entry was reused" a.log
grep -q "T859 value one" a.log || fail "A: the reused BMI gave another value" a.log

# B
sleep 1
cat > "$TMP/rules/src/rule859.cppm" <<'EOF'
module;
#include "value.h"
export module t859.rules;
import mcpp;
export int rule_value() { return T859_VALUE + 0; }
EOF
rerun 2 b.log
[ "$(compile_lines b.log)" = 1 ] || fail "B: an edit to the interface did not compile a new entry" b.log
[ "$(entry_count)" = 2 ] || fail "B: expected two entries, found $(entry_count)" b.log

# C
sleep 1
printf '#define T859_VALUE 2\n' > "$TMP/rules/src/value.h"
rerun 3 c.log
[ "$(compile_lines c.log)" = 1 ] || fail "C: an edit to an included header did not compile a new entry" c.log
[ "$(entry_count)" = 3 ] || fail "C: expected three entries, found $(entry_count)" c.log
grep -q "T859 value two" c.log || fail "C: the program took the BMI compiled before the header changed" c.log

# D
# The entry the last build used is the one the tree as it stands maps to: the
# most recently written or read. Its recorded standard flag is edited in place,
# so that the key still names it and what it records no longer agrees.
entry=$(ls -t $(find "$STORE" -name entry.json) | head -1)
grep -q '"std_flag"' "$entry" || fail "D: the entry records no standard flag" "$entry"
sed 's/"std_flag": "[^"]*"/"std_flag": "-std=c++98"/' "$entry" > "$entry.edited"
mv "$entry.edited" "$entry"
rerun 4 d.log
[ "$(compile_lines d.log)" = 1 ] || fail "D: an entry whose recorded inputs disagree was used without compiling" d.log
if grep -q -- '-std=c++98' "$entry"; then
    fail "D: the entry that disagreed was not replaced" "$entry"
fi
[ "$(entry_count)" = 3 ] || fail "D: expected the entry to be replaced in place, found $(entry_count) entries" d.log
grep -q "T859 value two" d.log || fail "D: the replaced entry gave another value" d.log

echo "PASS: 859_a_host_module_entry_is_reused_while_its_inputs_agree"
