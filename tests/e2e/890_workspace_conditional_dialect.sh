#!/usr/bin/env bash
# requires: unix-shell
# #786: a selected member's architecture-specific dialect reaches the virtual
# workspace root, std precompilation and every TU. Different declarations must
# separate workspace plans; an ordinary dependency cannot change the dialect.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"
REGISTRY_HOST=$(host_path "${MCPP_HOME:-$HOME/.mcpp}/registry")
export MCPP_HOME="$TMP/mcpp-home"
mkdir -p "$MCPP_HOME"
cat > "$MCPP_HOME/config.toml" <<EOF
[xlings]
home = "$REGISTRY_HOST"
EOF

case "$(uname -m)" in
    x86_64|amd64) ARCH=x86_64; OTHER=aarch64 ;;
    aarch64|arm64) ARCH=aarch64; OTHER=x86_64 ;;
    *) fail "unsupported fixture host architecture: $(uname -m)" ;;
esac

mkdir -p "$TMP/ws/app" "$TMP/ws/other" "$TMP/ws/dep"
cd "$TMP/ws"
cat > mcpp.toml <<'TOML'
[workspace]
members = ["app", "other"]
[workspace.build]
dialect_cxxflags = ["-DBASE786"]
TOML
cat > dep/mcpp.toml <<EOF
[package]
name = "dep786"
version = "0.1.0"
standard = "c++23"
[build]
sources = ["dep.cppm"]
[targets.dep786]
kind = "lib"
[target.'cfg(arch = "$ARCH")'.build]
dialect_cxxflags = ["-DDEP_ONLY786"]
EOF
printf 'export module dep786;\nexport int value786() { return 42; }\n' > dep/dep.cppm
write_member() {
    local name="$1" value="$2"
    cat > "$name/mcpp.toml" <<EOF
[package]
name = "$name"
version = "0.1.0"
standard = "c++23"
[build]
sources = ["main.cpp"]
[targets.$name]
kind = "bin"
main = "main.cpp"
[dependencies]
dep786 = { path = "../dep" }
[target.'cfg(arch = "$ARCH")'.build]
dialect_cxxflags = ["-DARCH786=$value"]
[target.'cfg(arch = "$OTHER")'.build]
dialect_cxxflags = ["-DNONMATCH786"]
EOF
    cat > "$name/main.cpp" <<EOF
#ifndef BASE786
#error workspace dialect missing
#endif
#if ARCH786 != $value
#error member architecture dialect missing or taken from another member
#endif
#if defined(NONMATCH786) || defined(DEP_ONLY786)
#error nonmatching or dependency dialect leaked
#endif
import std;
import dep786;
int main() { std::println("{}", value786()); return 0; }
EOF
}
write_member app 1
write_member other 2

"$MCPP" build -p app > selected.log 2>&1 || fail "selected-member build lost its dialect" selected.log
grep -q -- '-DARCH786=1' compile_commands.json || fail "CDB lost the conditional dialect" compile_commands.json
grep -rq -- '-DARCH786=1' "$MCPP_HOME/build-cache/v1/std" || fail "std precompile lost the conditional dialect" selected.log
if grep -q -- '-DNONMATCH786\|-DDEP_ONLY786' compile_commands.json; then
    fail "nonmatching/dependency dialect reached compiler commands" compile_commands.json
fi

cd app
"$MCPP" build > inside.log 2>&1 || fail "building inside a member lost its dialect" inside.log
cd ..
"$MCPP" build --workspace > workspace.log 2>&1 || fail "workspace grouping conflated distinct dialects" workspace.log
graphs=$(find target -name build.ninja | wc -l | tr -d ' ')
[ "$graphs" = 2 ] || fail "expected two configurations for different member dialects, got $graphs" workspace.log
grep -rq -- '-DARCH786=2' "$MCPP_HOME/build-cache/v1/std" || fail "second member reused the first member's std configuration" workspace.log

echo "PASS: 890 workspace conditional dialect"
