#!/usr/bin/env bash
# requires:
# 858 -- the bundled `mcpp` module is compiled once per mcpp version and host
# compiler, for every project that shares a home.
#
# #748 (B1). The module is the engine's own text: identical in every project
# for one mcpp version and one host compiler. Each build program used to compile
# it into its own directory. It is now an entry of the global cache, in the
# layout of a dependency's entry, so `mcpp cache list`, `info`, `verify` and `gc`
# treat it as they treat one.
#
# Criteria:
#   A. The first project compiles the module (the compile commands are logged
#      under `buildmcpp-host`); a second project with another name in another
#      directory, sharing the home, compiles nothing of it.
#   B. The entry is one of `mcpp cache list`, is complete to `mcpp cache verify`,
#      and records inputs that name no project: the flags it was compiled with
#      are the toolchain's, so they hold for every project.
#   C. `mcpp cache gc` collects it, and the next build compiles it again.
#   D. `--cache local` writes nothing below the global cache's package tree; the
#      module is then kept in the project's own store.
#   E. A host module of an index package, whose sources are in the immutable
#      store, is kept in the global cache too, under its package address, and a
#      second project that imports it compiles nothing of it.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

export MCPP_HOME="$TMP/mcpp-home"
source "$(dirname "$0")/_inherit_toolchain.sh"

project() {
    local dir="$1" name="$2"
    mkdir -p "$dir/src"
    cat > "$dir/mcpp.toml" <<EOF
[package]
name    = "$name"
version = "0.1.0"

[targets.$name]
kind = "bin"
main = "src/main.cpp"
EOF
    printf 'int main() { return 0; }\n' > "$dir/src/main.cpp"
    cat > "$dir/build.mcpp" <<EOF
import mcpp;
int main() { mcpp::define("T858_${name}=1"); return 0; }
EOF
}
project "$TMP/alpha" alpha858
project "$TMP/beta"  beta858
ALPHA_HOST="$(host_path "$TMP/alpha")"
BETA_HOST="$(host_path "$TMP/beta")"

compiled_module() { grep -cE "mcpp module (precompile|compile|object|mcpp\.core (precompile|compile|object)):" "$1" || true; }

# A
( cd "$TMP/alpha" && MCPP_VERBOSE=1 "$MCPP" build > "$TMP/alpha.log" 2>&1 ) \
    || fail "the first project did not build" "$TMP/alpha.log"
[ "$(compiled_module "$TMP/alpha.log")" -ge 2 ] \
    || fail "A: the first project did not compile the bundled module" "$TMP/alpha.log"
( cd "$TMP/beta" && MCPP_VERBOSE=1 "$MCPP" build > "$TMP/beta.log" 2>&1 ) \
    || fail "the second project did not build" "$TMP/beta.log"
[ "$(compiled_module "$TMP/beta.log")" = 0 ] \
    || fail "A: the second project compiled the bundled module again" "$TMP/beta.log"
grep -qE "bundled module mcpp: entry [0-9a-f]+ global \(reused\)" "$TMP/beta.log" \
    || fail "A: the second project's log does not say it reused the entry" "$TMP/beta.log"

# B
"$MCPP" cache list > "$TMP/list.log" 2>&1
[ "$(grep -c '_engine/mcpp-build-module@' "$TMP/list.log")" = 1 ] \
    || fail "B: expected one entry of the bundled module in the cache" "$TMP/list.log"
"$MCPP" cache verify > "$TMP/verify.log" 2>&1 || fail "B: mcpp cache verify failed" "$TMP/verify.log"
grep -q "all complete" "$TMP/verify.log" || fail "B: the entry is not complete" "$TMP/verify.log"
"$MCPP" cache info _engine/mcpp-build-module > "$TMP/info.log" 2>&1
grep -q '"interface_sha256"' "$TMP/info.log" || fail "B: the entry does not record the digest of its text" "$TMP/info.log"
if grep -qF "$ALPHA_HOST" "$TMP/info.log" || grep -qF "$BETA_HOST" "$TMP/info.log"; then
    fail "B: the recorded inputs name a project, so the entry could not serve another" "$TMP/info.log"
fi

# C
sleep 2
"$MCPP" cache gc --older-than 1s > "$TMP/gc.log" 2>&1 || fail "C: mcpp cache gc failed" "$TMP/gc.log"
grep -q "Collected 1 entries" "$TMP/gc.log" || fail "C: gc did not collect the entry of the bundled module" "$TMP/gc.log"
( cd "$TMP/alpha" && touch build.mcpp && echo '// edited' >> build.mcpp \
    && MCPP_VERBOSE=1 "$MCPP" build > "$TMP/alpha2.log" 2>&1 ) \
    || fail "C: the build after gc failed" "$TMP/alpha2.log"
[ "$(compiled_module "$TMP/alpha2.log")" -ge 2 ] \
    || fail "C: the module was not compiled again after gc collected it" "$TMP/alpha2.log"

# D
export MCPP_HOME="$TMP/mcpp-home-local"
source "$(dirname "$0")/_inherit_toolchain.sh"
project "$TMP/gamma" gamma858
( cd "$TMP/gamma" && MCPP_VERBOSE=1 "$MCPP" build --cache local > "$TMP/gamma.log" 2>&1 ) \
    || fail "D: the --cache local build failed" "$TMP/gamma.log"
if [ -n "$(find "$MCPP_HOME/build-cache/v1/pkg" -name entry.json 2>/dev/null)" ]; then
    fail "D: --cache local wrote an entry below the global cache's package tree" "$TMP/gamma.log"
fi
[ -n "$(find "$TMP/gamma/target/.build-mcpp/host-modules" -name entry.json 2>/dev/null)" ] \
    || fail "D: the module is not in the project's own store" "$TMP/gamma.log"
grep -qE "bundled module mcpp: entry [0-9a-f]+ workspace \(compiled\)" "$TMP/gamma.log" \
    || fail "D: the log does not say the module went to the workspace store" "$TMP/gamma.log"

# E. The package is in a local index, and its payload is where the store keeps
# installed packages: the two facts the cache admits a package on.
export MCPP_HOME="$TMP/mcpp-home-index"
source "$(dirname "$0")/_inherit_toolchain.sh"
INDEX_DIR="$TMP/local-index"
INDEX_DIR_HOST="$(host_path "$INDEX_DIR")"
mkdir -p "$INDEX_DIR/pkgs/r"
cat > "$INDEX_DIR/pkgs/r/rule-dep.lua" <<'LUA'
package = {
    spec = "1",
    name = "rule-dep",
    description = "A package that offers a build rule as a host module",
    licenses = {"MIT"},
    type = "package",
    xpm = {
        linux = { ["1.0.0"] = {
            url = "https://example.invalid/rule-dep-1.0.0.tar.gz",
            sha256 = "0000000000000000000000000000000000000000000000000000000000000000" } },
        macosx = { ["1.0.0"] = {
            url = "https://example.invalid/rule-dep-1.0.0.tar.gz",
            sha256 = "0000000000000000000000000000000000000000000000000000000000000000" } },
        windows = { ["1.0.0"] = {
            url = "https://example.invalid/rule-dep-1.0.0.tar.gz",
            sha256 = "0000000000000000000000000000000000000000000000000000000000000000" } },
    },
}
LUA
indexed() {
    local dir="$1" name="$2"
    local payload="$dir/.mcpp/.xlings/data/xpkgs/local-dev.rule-dep/1.0.0"
    mkdir -p "$dir/src" "$payload/src"
    cat > "$payload/mcpp.toml" <<'TOML'
[package]
name    = "rule-dep"
version = "1.0.0"

[targets.rule-dep]
kind = "lib"
TOML
    cat > "$payload/src/rule-dep.cppm" <<'CPPM'
export module t858.rule;
import mcpp;
export int rule_v() { return 5; }
CPPM
    cat > "$dir/mcpp.toml" <<MANIFEST
[package]
name    = "$name"
version = "0.1.0"

[indices]
local-dev = { path = "$INDEX_DIR_HOST" }

[build-dependencies]
"local-dev.rule-dep" = { version = "1.0.0", host-module = true }

[targets.$name]
kind = "bin"
main = "src/main.cpp"
MANIFEST
    printf 'int main() { return 0; }\n' > "$dir/src/main.cpp"
    cat > "$dir/build.mcpp" <<'BUILDMCPP'
import mcpp;
import t858.rule;
int main() { return rule_v() == 5 ? 0 : 1; }
BUILDMCPP
}
indexed "$TMP/delta" delta858
indexed "$TMP/epsilon" epsilon858
( cd "$TMP/delta" && MCPP_VERBOSE=1 "$MCPP" build > "$TMP/delta.log" 2>&1 ) \
    || fail "E: the first project that imports the index package's host module did not build" "$TMP/delta.log"
grep -qE "host module 't858\.rule' (precompile|compile):" "$TMP/delta.log" \
    || fail "E: the first project did not compile the host module" "$TMP/delta.log"
entry=$(grep -rl '"module": "t858.rule"' "$MCPP_HOME/build-cache/v1/pkg" --include=entry.json 2>/dev/null | head -1)
[ -n "$entry" ] || fail "E: the host module of an index package is not in the global cache" "$TMP/delta.log"
case "$entry" in
    */pkg/local-dev/local-dev.rule-dep@1.0.0/*/entry.json) ;;
    *) fail "E: the entry is not at its package address: $entry" ;;
esac
if [ -d "$TMP/delta/target/.build-mcpp/host-modules" ] \
   && grep -rl 't858.rule' "$TMP/delta/target/.build-mcpp/host-modules" --include=entry.json 2>/dev/null | grep -q .; then
    fail "E: a host module of an index package was also written to the project's store"
fi
( cd "$TMP/epsilon" && MCPP_VERBOSE=1 "$MCPP" build > "$TMP/epsilon.log" 2>&1 ) \
    || fail "E: the second project did not build" "$TMP/epsilon.log"
if grep -qE "host module 't858\.rule' (precompile|compile):" "$TMP/epsilon.log"; then
    fail "E: the second project compiled the index package's host module again" "$TMP/epsilon.log"
fi
grep -qE "host module 't858\.rule': entry [0-9a-f]+ global \(reused\)" "$TMP/epsilon.log" \
    || fail "E: the second project's log does not say it reused the entry" "$TMP/epsilon.log"

echo "PASS: 858_the_engines_module_is_compiled_once_per_home"
