#!/usr/bin/env bash
# requires: gcc
# 800_a_feature_provides_its_host_tools.sh — mcpp#709.
#
# `[features.<f>] tools = ["<bin>"]` states that enabling `f` needs the
# package's own program `<bin>` on the build machine. A consumer that enables
# the feature receives the tool exactly as if its dependency edge had written
# `tools = [...]` (e2e 187): built once for the host, reachable through
# `mcpp::dep_bin()`. Before this, every consumer of a rule package had to name
# the rule's tools on its edge as well as the feature.
#
# Criteria:
#   1. a consumer that writes only `features = ["codegen"]` gets the tool, and
#      the source it generates is compiled and linked;
#   2. a consumer that does not enable the feature gets nothing built;
#   3. a `tools` entry that names no bin target of the package is refused at
#      load, naming the package's bin targets.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"

export MCPP_HOME="$TMP/mcpphome"
mkdir -p "$MCPP_HOME"
if [ -d "$HOME/.mcpp/registry" ]; then
    ln -s "$HOME/.mcpp/registry" "$MCPP_HOME/registry"
fi

# ── the tool package ────────────────────────────────────────────────────────
mkdir -p toolpkg/src
cat > toolpkg/mcpp.toml <<'EOF'
[package]
name    = "toolpkg"
version = "0.1.0"

[build]
sources = ["src/lib.cpp"]

[features.codegen]
tools = ["codegen"]

[targets.codegen]
kind = "bin"
main = "src/codegen.cpp"

[targets.toolpkg]
kind = "lib"
EOF
echo 'int toolpkg_lib() { return 1; }' > toolpkg/src/lib.cpp
cat > toolpkg/src/codegen.cpp <<'EOF'
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = std::fopen(argv[1], "w");
    if (!f) return 3;
    std::fprintf(f, "int generated_answer() { return 42; }\n");
    std::fclose(f);
    return 0;
}
EOF

# ── 1. the consumer names the feature, not the tool ─────────────────────────
mkdir -p app/src
cat > app/mcpp.toml <<'EOF'
[package]
name    = "app"
version = "0.1.0"

[dependencies]
toolpkg = { path = "../toolpkg", features = ["codegen"] }
EOF
cat > app/src/main.cpp <<'EOF'
#include <cstdio>
int generated_answer();
int main() { std::printf("ANSWER=%d\n", generated_answer()); }
EOF
cat > app/build.mcpp <<'EOF'
#include <cstdio>
#include <cstdlib>
#include <string>
import mcpp;
int main() {
    const char* tool = mcpp::dep_bin("toolpkg", "codegen");
    if (!tool || !*tool) { std::fprintf(stderr, "no tool path\n"); return 1; }
    std::string out = std::string(mcpp::out_dir()) + "/gen.cpp";
    std::string cmd = std::string("\"") + tool + "\" \"" + out + "\"";
    if (std::system(cmd.c_str()) != 0) { std::fprintf(stderr, "tool failed\n"); return 1; }
    mcpp::generated(out.c_str());
}
EOF
cd app
"$MCPP" build > b1.log 2>&1 || { cat b1.log; echo "FAIL: 1: build failed"; exit 1; }
grep -q "host tool toolpkg:codegen" b1.log || {
    cat b1.log; echo "FAIL: 1: the feature's tool was not built"; exit 1; }
out="$("$MCPP" run 2>&1 | grep '^ANSWER=' | tail -1)"
[[ "$out" == "ANSWER=42" ]] || { echo "FAIL: 1: generated source not linked: $out"; exit 1; }
echo "ok: 1"

# ── 2. without the feature, nothing is built ────────────────────────────────
cd "$TMP"
mkdir -p plain/src
cat > plain/mcpp.toml <<'EOF'
[package]
name    = "plain"
version = "0.1.0"

[dependencies]
toolpkg = { path = "../toolpkg" }
EOF
echo 'int main() {}' > plain/src/main.cpp
cd plain
"$MCPP" build > b2.log 2>&1 || { cat b2.log; echo "FAIL: 2: build failed"; exit 1; }
if grep -q "host tool" b2.log; then
    cat b2.log; echo "FAIL: 2: a tool was built for a consumer that did not enable the feature"; exit 1
fi
echo "ok: 2"

# ── 3. a tools entry that names no bin target is refused ────────────────────
cd "$TMP"
sed 's/tools = \["codegen"\]/tools = ["nosuch"]/' toolpkg/mcpp.toml > toolpkg/mcpp.toml.new
mv toolpkg/mcpp.toml.new toolpkg/mcpp.toml
cd app && rm -rf target
if "$MCPP" build > b3.log 2>&1; then
    cat b3.log; echo "FAIL: 3: an unknown tool name was accepted"; exit 1
fi
grep -q "nosuch" b3.log && grep -q "codegen" b3.log || {
    cat b3.log; echo "FAIL: 3: the refusal does not name the entry and the bin targets"; exit 1; }
echo "ok: 3"

echo "PASS: 800_a_feature_provides_its_host_tools"
