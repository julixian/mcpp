#!/usr/bin/env bash
# requires: msvc
# 848 -- under MSVC, two programs of one build each have their own module of
# one name (mcpp#732), bound with `/reference <name>=<path>`.
#
# e2e 847 states the rule with the default toolchains (GCC's mapper file, and
# clang's `-fmodule-file=`). cl.exe finds a BMI through `/ifcSearchDir`, and an
# explicit `/reference` is what tells one program's units which of the two
# `.ifc` files a name means; this is the leg that measures it.
#
# Criterion: an app and its `artifacts` updater each provide a different module
# `boost`; `--toolchain msvc` builds both, and each prints its own value.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

mkdir -p app/src updater/src
printf 'export module boost;\nexport int value() { return 1; }\n' > app/src/boost.cppm
printf 'export module boost;\nexport int value() { return 2; }\n' > updater/src/boost.cppm
for d in app updater; do
    printf '#include <cstdio>\nimport boost;\nint main() { std::printf("%%d\\n", value()); }\n' > $d/src/main.cpp
done
cat > updater/mcpp.toml <<'EOF'
[package]
name    = "updater"
version = "0.1.0"

[targets.updater]
kind = "bin"
main = "src/main.cpp"
EOF
cat > app/mcpp.toml <<'EOF'
[package]
name    = "app"
version = "0.1.0"

[dependencies]
updater = { path = "../updater", artifacts = ["updater"] }

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
(cd app && "$MCPP" build --toolchain msvc > "$TMP/b.log" 2>&1) || fail "the build under MSVC was refused" b.log
app=$(find app/target -path '*/bin/*' -name 'app.exe' -type f | head -1)
upd=$(find app/target -path '*/bin/*' -name 'updater.exe' -type f | head -1)
[ -n "$app" ] && [ -n "$upd" ] || fail "a program is missing" b.log
[ "$("$app" | tr -d '\r')" = 1 ] || fail "the app does not print its own module's value" b.log
[ "$("$upd" | tr -d '\r')" = 2 ] || fail "the updater does not print its own module's value" b.log
echo "PASS: 848_msvc_binds_a_module_name_per_program"
