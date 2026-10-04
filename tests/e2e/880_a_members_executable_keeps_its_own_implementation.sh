#!/usr/bin/env bash
# requires:
# 880 -- mcpp#760: a member with shared and bin targets builds an independent
# executable from its own implementation, not from its sibling shared library.
#
# A module interface, a separate implementation unit and a static dependency
# must all reach the executable. Another member still consumes the shared
# target. Both --workspace and an explicit member selection keep this rule.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

# Share the installed tool payloads, but keep the build and package caches
# private. The fixture has no registry package dependencies; the default host
# toolchain is resolved through the existing registry, as in the E2E runner.
REGISTRY_HOST=$(host_path "${MCPP_HOME:-$HOME/.mcpp}/registry")
export MCPP_HOME="$TMP/mcpp-home"
mkdir -p "$MCPP_HOME" "$TMP/ws"
cat > "$MCPP_HOME/config.toml" <<EOF
[xlings]
home = "$REGISTRY_HOST"
EOF

EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
bin_of() { find target -path '*/bin/*' -name "$1$EXE" -type f | head -1; }

cd "$TMP/ws"
cat > mcpp.toml <<'EOF'
[workspace]
members = ["dual", "client", "support"]
EOF
mkdir -p common/src dual/src client/src support/src
cat > support/mcpp.toml <<'EOF'
[package]
name = "support"
version = "0.1.0"

[targets.support]
kind = "shared"
EOF
printf 'export module t880_support;\nexport int delta() { return 1; }\n' > support/src/support.cppm
cat > common/mcpp.toml <<'EOF'
[package]
name = "common"
version = "0.1.0"

[targets.common]
kind = "lib"
EOF
printf 'export module t880_base;\nexport int base_value() { return 41; }\n' > common/src/base.cppm
cat > dual/mcpp.toml <<'EOF'
[package]
name = "dual"
version = "0.1.0"

[dependencies]
common = { path = "../common" }
support = { path = "../support" }

[targets.dual_dll]
kind = "shared"

[targets.dual]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'export module t880_dual;\nexport int interface_value() { return 1; }\nexport int answer();\n' > dual/src/dual.cppm
printf 'module t880_dual;\nimport t880_base;\nimport t880_support;\nint answer() { return base_value() + delta(); }\n' > dual/src/impl.cpp
printf 'import t880_dual;\nint main() { return answer() == 42 && interface_value() == 1 ? 0 : 1; }\n' > dual/src/main.cpp
cat > client/mcpp.toml <<'EOF'
[package]
name = "client"
version = "0.1.0"

[dependencies]
dual = { path = "../dual" }

[targets.client]
kind = "bin"
main = "src/main.cpp"
EOF
cp dual/src/main.cpp client/src/main.cpp

"$MCPP" build --workspace > workspace.log 2>&1 || fail "the workspace did not build" workspace.log
dual=$(bin_of dual)
client=$(bin_of client)
[ -n "$dual" ] && [ -n "$client" ] || fail "a member's executable is missing" workspace.log
"$dual" || fail "the owner's executable did not run"
"$client" || fail "the shared-library consumer did not run"

# Remove every placement of the sibling library. A client that imports it must
# fail, while the owner's executable must still run: mere success with both
# products beside it would also accept the import-library workaround for #760.
mkdir hidden
n=0
while IFS= read -r library; do
    n=$((n + 1))
    mv "$library" "hidden/$n"
done < <(find target -path '*/bin/*' \( -name '*dual_dll.dll' -o -name '*dual_dll.so*' -o -name '*dual_dll.dylib' \))
[ "$n" -gt 0 ] || fail "the sibling shared library was not built" workspace.log
"$dual" || fail "the owner's executable depends on its sibling shared library"
if "$client" > client.log 2>&1; then
    fail "the external consumer did not use the shared library" client.log
fi

"$MCPP" build -p dual > selected.log 2>&1 || fail "the selected member did not build" selected.log
"$(bin_of dual)" || fail "the selected member's executable did not run"
echo "PASS: 880_a_members_executable_keeps_its_own_implementation"
