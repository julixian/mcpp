#!/usr/bin/env bash
# requires: unix-shell
# 828_a_library_states_its_interface.sh -- #734 E6, SPEC-008 phase 1.
#
# A library's interface is its root module and what the root re-exports with
# `export import`, transitively. `mcpp pack` ships that closure. Phase 1 warns
# and never refuses: a library may implement itself in modules and publish only
# headers, and only its author can say which it means.
#
#   I1  a library exporting `Alpha` and `Beta` with no root: the build states
#       W1 with its impact line; the pack exits 0, names both modules (W2), and
#       its "Withheld" row lists both units instead of "(nothing)";
#   I2  the same library with a facade root re-exporting both: both are shipped
#       and no W2 appears;
#   I3  a consumer importing `Beta` from a library whose root re-exports only
#       `Alpha` is warned (W3); importing `Alpha` is not.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p lib/src
cd lib
cat > mcpp.toml <<'EOF'
[package]
namespace = "probe"
name      = "flatlib"
version   = "0.1.0"

[build]
sources = ["src/alpha.cppm", "src/beta.cppm"]

[targets.flatlib]
kind = "lib"
EOF
printf 'export module Alpha;\nexport int alpha() { return 1; }\n' > src/alpha.cppm
printf 'export module Beta;\nimport Alpha;\nexport int beta() { return alpha() + 1; }\n' > src/beta.cppm

# I1
"$MCPP" build > i1b.log 2>&1 || fail "I1: the build failed" i1b.log
grep -q "without conventional lib root" i1b.log || fail "I1: W1 is missing" i1b.log
grep -q "impact: \`mcpp pack\` publishes this library without a module interface" i1b.log \
    || fail "I1: W1 has no impact line" i1b.log
"$MCPP" pack flatlib > i1.log 2>&1 || fail "I1: the pack failed" i1.log
grep -q "exports modules Alpha, Beta that the packed form does not ship" i1.log \
    || grep -q "exports modules Beta, Alpha that the packed form does not ship" i1.log \
    || fail "I1: W2 does not name both modules" i1.log
grep -q "Withheld.*alpha.cppm" i1.log && grep -q "Withheld.*beta.cppm" i1.log \
    || fail "I1: the Withheld row does not list both units" i1.log
grep -q "Withheld (nothing)" i1.log && fail "I1: the Withheld row still reads (nothing)" i1.log

# I2
cat >> mcpp.toml <<'EOF'

[lib]
path = "src/flatlib.cppm"
EOF
sed -i 's|sources = \["src/alpha.cppm", "src/beta.cppm"\]|sources = ["src/flatlib.cppm", "src/alpha.cppm", "src/beta.cppm"]|' mcpp.toml
printf 'export module probe.flatlib;\nexport import Alpha;\nexport import Beta;\n' > src/flatlib.cppm
"$MCPP" pack flatlib > i2.log 2>&1 || fail "I2: the pack failed" i2.log
grep -q "does not ship" i2.log && fail "I2: W2 appeared with a facade" i2.log
grep -q "Interface.*alpha.cppm" i2.log && grep -q "Interface.*beta.cppm" i2.log \
    || fail "I2: the facade's modules are not both shipped" i2.log

# I3
printf 'export module probe.flatlib;\nexport import Alpha;\nimport Beta;\n' > src/flatlib.cppm
cd "$TMP"
mkdir -p app/src
cat > app/mcpp.toml <<'EOF'
[package]
name    = "app828"
version = "0.1.0"

[dependencies]
"probe.flatlib" = { path = "../lib" }
EOF
printf 'import Alpha;\nint main() { return alpha() == 1 ? 0 : 1; }\n' > app/src/main.cpp
cd app
"$MCPP" build > i3a.log 2>&1 || fail "I3: the build importing a public module failed" i3a.log
grep -q "not one of that package's public modules" i3a.log && fail "I3: a public module was warned about" i3a.log
printf 'import Beta;\nint main() { return beta() == 2 ? 0 : 1; }\n' > src/main.cpp
"$MCPP" build > i3b.log 2>&1 || fail "I3: the build importing a non-public module failed" i3b.log
grep -q "imports 'Beta' of 'probe.flatlib', which is not one of that package's public modules" i3b.log \
    || fail "I3: W3 is missing" i3b.log
grep -q "import a public module of 'probe.flatlib': Alpha" i3b.log || fail "I3: W3 does not name the public modules" i3b.log

echo "OK"
