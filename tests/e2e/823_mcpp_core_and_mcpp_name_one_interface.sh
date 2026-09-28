#!/usr/bin/env bash
# requires: unix-shell
# 823_mcpp_core_and_mcpp_name_one_interface.sh -- #734 E8.
#
# The engine's build-program interface is named `mcpp.core`, the layer name the
# specification uses (SPEC-007), and `mcpp` is its permanent equivalent: the
# engine embeds `mcpp` and a second unit whose body is `export import mcpp;`.
# A build program may use either spelling, or both.
#
#   C1  `import mcpp.core;` builds and the program's directive takes effect;
#   C2  `import mcpp;` behaves the same;
#   C3  a program importing both compiles and behaves the same.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

leg() {  # $1 = label, $2 = the import lines
    rm -rf "$TMP/$1"; mkdir -p "$TMP/$1/src"; cd "$TMP/$1"
    printf '[package]\nname = "core823"\nversion = "0.1.0"\n' > mcpp.toml
    printf '#include <cstdio>\nint main() { std::printf("%%d\\n", CORE823); return 0; }\n' > src/main.cpp
    printf '%s\nint main() {\n    mcpp::cxxflag("-DCORE823=823");\n    return 0;\n}\n' "$2" > build.mcpp
    "$MCPP" build > build.log 2>&1 || fail "$1: the build failed" build.log build.mcpp
    "$MCPP" run > run.log 2>&1 || fail "$1: the program did not run" run.log
    grep -qx "823" run.log || fail "$1: the directive did not take effect" run.log
}

leg C1 'import mcpp.core;'
leg C2 'import mcpp;'
leg C3 'import mcpp.core;
import mcpp;'

echo "OK"
