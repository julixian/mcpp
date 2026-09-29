#!/usr/bin/env bash
# 836_a_members_own_statements_reach_its_build.sh -- workspace design
# 2026-09-29 §15 and §17.
#
# A workspace plan's root is a virtual root that holds the plan's values. What
# a member states about itself is read from the member, never from that root:
#
#   M1  a member whose only sources are its tests, and whose tests `import
#       std`, is tested with the std module built (2026.9.29.1 read the entry
#       files of the root's targets only, and the virtual root has none);
#   M2  a capability under the reserved `mcpp:` prefix that no layer names is
#       refused in a member's manifest as in a root's;
#   M3  a cfg() predicate a member writes and mcpp cannot evaluate is
#       reported as in a root's manifest.
#
# A member's own relative [indices].path is e2e 120; a member's [resources]
# is e2e 837.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

# ── M1 ──────────────────────────────────────────────────────────────────────
mkdir -p ws1/probe/tests
cat > ws1/mcpp.toml <<'EOF'
[workspace]
members = ["probe"]
EOF
cat > ws1/probe/mcpp.toml <<'EOF'
[package]
name = "probe-tests"
version = "0.1.0"
EOF
cat > ws1/probe/tests/hello.cpp <<'EOF'
import std;
int main() {
    std::println("probe");
    return 0;
}
EOF
(cd ws1 && "$MCPP" test -p probe > ../m1.log 2>&1) \
    || fail "M1 a member whose tests import std is not tested" m1.log
(cd ws1/probe && "$MCPP" test > ../../m1b.log 2>&1) \
    || fail "M1 the same member, tested from its directory" m1b.log

# ── M2 ──────────────────────────────────────────────────────────────────────
mkdir -p ws2/a/src
cat > ws2/mcpp.toml <<'EOF'
[workspace]
members = ["a"]
EOF
cat > ws2/a/mcpp.toml <<'EOF'
[package]
name = "a"
version = "0.1.0"
provides = ["mcpp:no-such-layer"]
EOF
printf 'int main() { return 0; }\n' > ws2/a/src/main.cpp
if (cd ws2 && "$MCPP" build --workspace > ../m2.log 2>&1); then
    fail "M2 a member's unknown mcpp: capability was not refused" m2.log
fi
grep -qE 'ws2[/\\]a[/\\]mcpp\.toml' m2.log && grep -q 'mcpp:no-such-layer' m2.log \
    || fail "M2 the refusal names the member's manifest and the capability" m2.log

# ── M3 ──────────────────────────────────────────────────────────────────────
mkdir -p ws3/b/src
cat > ws3/mcpp.toml <<'EOF'
[workspace]
members = ["b"]
EOF
cat > ws3/b/mcpp.toml <<'EOF'
[package]
name = "b"
version = "0.1.0"

[target.'cfg(no_such_key = "x")'.build]
cxxflags = ["-DNEVER=1"]
EOF
printf 'int main() { return 0; }\n' > ws3/b/src/main.cpp
(cd ws3 && "$MCPP" build --workspace > ../m3.log 2>&1) || fail "M3 build" m3.log
grep -q "no_such_key" m3.log || fail "M3 a member's unknown cfg() key is not reported" m3.log

echo "PASS: 836_a_members_own_statements_reach_its_build"
