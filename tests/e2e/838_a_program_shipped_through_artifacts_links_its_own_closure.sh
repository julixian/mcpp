#!/usr/bin/env bash
# requires: elf
# 838_a_program_shipped_through_artifacts_links_its_own_closure.sh --
# workspace design 2026-09-29 §15 and §17.1.
#
# A program a member ships through `artifacts` is linked with the link line of
# its own package's closure. Its package's build program states a library the
# program needs (`mcpp::link_lib`); in a workspace plan that statement belongs
# to the package, not to the plan, and 2026.9.29.3 linked the program with
# the plan's line, so the library was missing ("undefined reference").
#
#   A1  `mcpp build --workspace` links the program shipped through `artifacts`;
#   A2  the program in the shipping member's product directory runs;
#   A3  the program as its own member's product runs too;
#   A4  `${mcpp.bin_dir}` in an action the helper's build program declares is
#       the helper's product directory, where its binaries land (2026.9.29.3
#       expanded it to the plan's `bin/`, so a file named after the program
#       was looked for where the program is not).
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

# A library the helper links through its build program, built as an archive
# by a project of its own and copied into the helper's package.
mkdir -p vend/src
cat > vend/mcpp.toml <<'EOF'
[package]
name = "vend"
version = "0.1.0"

[targets.vend]
kind = "lib"
EOF
printf 'extern "C" int vend_v() { return 7; }\n' > vend/src/vend.cpp
(cd vend && "$MCPP" build > ../vend.log 2>&1) || fail "the archive project did not build" vend.log
archive=$(find vend/target -name 'libvend.a' | head -1)
[ -n "$archive" ] || fail "no libvend.a" vend.log

mkdir -p ws && cd ws
cat > mcpp.toml <<'EOF'
[workspace]
members = ["gui", "helper"]
EOF
mkdir -p gui/src helper/src helper/vendor/lib
cp "../$archive" helper/vendor/lib/
cat > helper/mcpp.toml <<'EOF'
[package]
name = "helper"
version = "0.1.0"

[targets.helper]
kind = "bin"
main = "src/main.cpp"
EOF
cat > helper/build.mcpp <<'EOF'
import mcpp;
int main() {
    mcpp::link_search("vendor/lib");
    mcpp::link_lib("vend");
    mcpp::action a;
    a.id = "copy-helper";
    a.role = mcpp::roles::artifact;
    a.arg("${mcpp.self}").arg("stage").arg("--verify").arg("content")
     .arg("--output").arg("${mcpp.bin_dir}/helper.copy")
     .arg("${mcpp.target_file:helper}")
     .input("${mcpp.target_file:helper}")
     .output("${mcpp.bin_dir}/helper.copy")
     .submit();
    return 0;
}
EOF
printf 'extern "C" int vend_v();\nint main() { return vend_v() == 7 ? 0 : 1; }\n' > helper/src/main.cpp
cat > gui/mcpp.toml <<'EOF'
[package]
name = "gui"
version = "0.1.0"

[dependencies.helper]
path = "../helper"
artifacts = ["helper"]

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > gui/src/main.cpp

# A1
"$MCPP" build --workspace > b.log 2>&1 || fail "A1: the workspace build failed" b.log

# A2, A3
shipped=$(find target -path '*/bin/gui/helper' -type f | head -1)
[ -n "$shipped" ] || fail "A2: bin/gui/helper is missing" b.log
"$shipped" || fail "A2: bin/gui/helper did not run"
own=$(find target -path '*/bin/helper/helper' -type f | head -1)
[ -n "$own" ] || fail "A3: bin/helper/helper is missing" b.log
"$own" || fail "A3: bin/helper/helper did not run"

# A4
[ -f "$(dirname "$own")/helper.copy" ] \
    || { find target -name 'helper.copy'; fail "A4: \${mcpp.bin_dir} is not the helper's product directory" b.log; }

echo "PASS: 838_a_program_shipped_through_artifacts_links_its_own_closure"
