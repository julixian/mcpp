#!/usr/bin/env bash
# requires:
# A sibling input directory must invalidate both build.mcpp's cache and the
# project fast path. The generated executable is the observable result.
set -euo pipefail
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
MCPP="${MCPP:-mcpp}"
REGISTRY_HOST=$(host_path "${MCPP_HOME:-$HOME/.mcpp}/registry")
export MCPP_HOME="$TMP/mcpp-home"
mkdir -p "$MCPP_HOME" "$TMP/ws/app/src" "$TMP/logs"
cat > "$MCPP_HOME/config.toml" <<EOF
[xlings]
home = "$REGISTRY_HOST"
EOF

cd "$TMP/ws/app"
cat > mcpp.toml <<'EOF'
[package]
name = "parent-glob"
version = "0.1.0"

[targets.parent-glob]
kind = "bin"
main = "src/main.cpp"
EOF
cat > src/main.cpp <<'EOF'
#include <cstdio>
int count_inputs();
int main() { std::printf("COUNT=%d\n", count_inputs()); }
EOF
cat > build.mcpp <<'EOF'
import std;
import mcpp;
int main() {
    namespace fs = std::filesystem;
    const fs::path inputs = fs::path(mcpp::manifest_dir()) / "../inputs";
    mcpp::rerun_if_changed_glob("../inputs/**/*.in");
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(inputs, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".in") ++count;
    const std::string output = std::string(mcpp::out_dir()) + "/count.cpp";
    { std::ofstream out(output); out << "int count_inputs() { return " << count << "; }\n"; }
    mcpp::generated(output.c_str());
    return 0;
}
EOF

fail() { echo "FAIL: $1"; cat "$TMP/logs"/*.log; exit 1; }
build_count() {
    local step=$1 expected=$2
    "$MCPP" build > "$TMP/logs/$step.log" 2>&1 || fail "build $step failed"
    local output
    output=$("$MCPP" run 2>&1 | grep '^COUNT=' | tail -1)
    [[ "$output" == "COUNT=$expected" ]] || fail "$step expected COUNT=$expected, got $output"
}

# The literal prefix initially does not exist. Creating the sibling directory
# and its first input must rerun without touching a source or manifest.
build_count missing 0
mkdir -p ../inputs
printf 'a\n' > ../inputs/a.in
build_count appeared 1
mkdir -p ../inputs/nested
printf 'b\n' > ../inputs/nested/b.in
build_count added 2
rm ../inputs/nested/b.in
build_count removed 1

# Glob inputs track membership only. A content edit, a nonmatching file, and
# an unchanged build must preserve the fast path instead of always rerunning.
printf 'different content\n' > ../inputs/a.in
printf 'not an input\n' > ../inputs/ignored.txt
build_count content 1
build_count unchanged 1
for step in content unchanged; do
    if grep -qE '^ *build\.mcpp .* ran [0-9]' "$TMP/logs/$step.log"; then
        fail "$step reran the build program despite unchanged membership"
    fi
done

echo "PASS: parent-directory glob inputs invalidate builds on membership changes"
