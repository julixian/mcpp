#!/usr/bin/env bash
# requires: python3
# #778: keep the workspace's ninja log, clear only a member's generated
# outputs, and verify the generator runs again rather than compiling an empty
# scan placeholder. Configure-only must leave no placeholder for a later run;
# ordinary warm builds, including a re-prepare, must not rerun the generator.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; [ -z "${2:-}" ] || cat "$2"; exit 1; }
MCPP="${MCPP:-mcpp}"
export ACTION_PYTHON=$(python3 -c 'import sys; print(sys.executable.replace(chr(92), "/"))')
export MCPP_HOME="$TMP/mcpp-home"
source "$HERE/_inherit_toolchain.sh"

mkdir -p "$TMP/ws/app/src"
cd "$TMP/ws"
cat > mcpp.toml <<'EOF'
[workspace]
members = ["app"]
EOF
cat > app/mcpp.toml <<'EOF'
[package]
name = "app"
namespace = "repro"
version = "0.1.0"
[build]
sources = []
[targets.resource_test]
kind = "bin"
main = "src/main.cpp"
EOF
cat > app/src/main.cpp <<'EOF'
int generated_value();
int main() { return generated_value() == 42 ? 0 : 1; }
EOF
echo 'int generated_value() { return 42; }' > app/value.cpp.in
cat > app/generate.py <<'EOF'
from pathlib import Path
import sys

source, output, counter = map(Path, sys.argv[1:])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(source.read_bytes())
count = int(counter.read_text()) if counter.exists() else 0
counter.write_text(str(count + 1))
EOF
cat > app/build.mcpp <<'EOF'
import std;
import mcpp;
int main() {
    mcpp::rerun_if_env_changed("ACTION_PYTHON");
    const std::string root = mcpp::manifest_dir();
    const std::string out = std::string(mcpp::out_dir()) + "/generated.cpp";
    mcpp::action a;
    a.id = "generate:value";
    a.role = mcpp::roles::source;
    a.arg(std::getenv("ACTION_PYTHON"))
     .arg((root + "/generate.py").c_str())
     .arg((root + "/value.cpp.in").c_str()).arg(out.c_str())
     .arg((root + "/generator-count.txt").c_str())
     .input((root + "/generate.py").c_str())
     .input((root + "/value.cpp.in").c_str()).output(out.c_str()).submit();
}
EOF

build() { "$MCPP" build -p app --release > "$1" 2>&1 || fail "the build failed" "$1"; }
count_is() { [ "$(cat app/generator-count.txt)" = "$1" ] || fail "expected $1 generator calls"; }
generated="app/target/.build-mcpp/out/generated.cpp"

# A real cold build establishes the generated source and the retained log.
build cold.log
cmp app/value.cpp.in "$generated" || fail "the generated source differs from its input"
count_is 1
log=$(find target -name .ninja_log | head -1)
[ -n "$log" ] && [ -s "$log" ] || fail "the workspace has no ninja build record"

build warm.log
count_is 1
touch app/build.mcpp
build reprepare.log
count_is 1
echo "ok: warm builds and re-prepare preserve the real output"

# Retain target/.ninja_log while removing only the member's output tree.
mv app/target saved-app-target
build member-clean.log
cmp app/value.cpp.in "$generated" || fail "the cleared source was replaced by a placeholder"
count_is 2
echo "ok: clearing a member's target reruns its generator"

# A configure-only pass must not leave a newer fake output behind.
mv app/target saved-app-target-2
"$MCPP" build -p app --release --configure-only > configure.log 2>&1 || fail "configure-only failed" configure.log
[ ! -e "$generated" ] || fail "configure-only left a scan placeholder behind"
count_is 2
build after-configure.log
cmp app/value.cpp.in "$generated" || fail "the build after configure-only did not generate its source"
count_is 3
build final-warm.log
count_is 3
echo "ok: configure-only leaves the missing output visible to the next build"

# Generated module interfaces still need their declared provider in the scan,
# even though the temporary interface disappears before the actual build.
echo 'import generated; int main() { return generated_value() == 42 ? 0 : 1; }' > app/src/main.cpp
printf 'export module generated;\nexport int generated_value() { return 42; }\n' > app/value.cpp.in
python3 <<'PY'
from pathlib import Path
p = Path("app/build.mcpp")
p.write_text(p.read_text().replace('"/generated.cpp"', '"/generated.cppm"')
             .replace('a.role = mcpp::roles::source;',
                      'a.role = mcpp::roles::source; a.provides("generated");'))
PY
generated="app/target/.build-mcpp/out/generated.cppm"
build module.log
count_is 4
cmp app/value.cpp.in "$generated" || fail "the module interface was not generated"
mv app/target saved-app-target-3
build module-clean.log
count_is 5
cmp app/value.cpp.in "$generated" || fail "the cleared module interface was not regenerated"
echo "ok: generated module interfaces retain their scan declarations and regenerate"
echo "PASS: 890_source_placeholders_do_not_hide_missing_outputs"
