#!/usr/bin/env bash
# requires: unix-shell
# 830_a_shared_member_is_compiled_once.sh -- #734 E1.
#
# A workspace member that other members use as a path dependency is built once,
# in its own directory, and every consumer takes its objects and module
# interfaces from there. Its own ninja decides what is stale, including inputs
# outside its root. The fixture has the shape that measured the defect: a
# library member with module units, one unit outside its root, and a header
# outside its root; two program members use it.
#
#   W1  `mcpp build --workspace` compiles each library unit once in total
#       (counted from every build directory's .ninja_log);
#   W2  a following `mcpp build -p app2` compiles no library unit;
#   W3  a header outside the library's root changes: the next build compiles
#       its includer once and no other library unit, and both programs run the
#       new value;
#   W4  after another change, `-p app1` and `-p app2` run at the same time:
#       both succeed, the includer is compiled once, and both run the new
#       value (one build of the member directory at a time).
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p ws/lib/src ws/shared ws/app1/src ws/app2/src
cd ws
cat > mcpp.toml <<'EOF'
[workspace]
members = ["lib", "app1", "app2"]

[workspace.package]
version = "0.1.0"
EOF
cat > lib/mcpp.toml <<'EOF'
[package]
namespace = "probe"
name      = "core830"

[build]
sources = ["src/*.cppm", "src/*.cpp", "../shared/extra.cpp"]

[targets.core830]
kind = "lib"
EOF
printf '#pragma once\ninline int shared_value() { return 7; }\n' > shared/value.h
printf 'export module probe.core830;\nexport int core_one();\nexport int core_extra();\n' > lib/src/core830.cppm
printf 'module probe.core830;\nint core_one() { return 1; }\n' > lib/src/one.cpp
printf 'module;\n#include "value.h"\nmodule probe.core830;\nint core_extra() { return shared_value(); }\n' > shared/extra.cpp
for a in app1 app2; do
    printf '[package]\nname = "%s"\n\n[dependencies]\n"probe.core830" = { path = "../lib" }\n' "$a" > $a/mcpp.toml
    printf 'import probe.core830;\n#include <cstdio>\nint main() { std::printf("%%d\\n", core_one() + core_extra()); return 0; }\n' > $a/src/main.cpp
done

lib_compiles() {  # compile edges that produced a library object, in every build directory
    local n=0
    for log in $(find . -name .ninja_log -path '*target*' 2>/dev/null); do
        local dir; dir=$(dirname "$log")
        # The rule of each output the log records, read from that directory's
        # build.ninja: a library object staged from the member's directory is
        # a `stage_file` edge, a compile is a `cxx_module` / `cxx_object` edge.
        c=$(awk -F'\t' 'NR>1 {print $4}' "$log" | grep -E '(core830|one|extra)\.(m\.)?o$' |
            while read -r out; do
                grep -E "^build [^:]*${out//./\\.}[ |:]" "$dir/build.ninja" | head -1 |
                    sed -E 's/^build [^:]*: ([a-z_]+).*/\1/'
            done | grep -cE '^(cxx_module|cxx_object|c_object)$' || true)
        n=$((n + c))
    done
    echo "$n"
}

# W1
"$MCPP" build --workspace > w1.log 2>&1 || fail "W1: the workspace build failed" w1.log
units=3   # core830.cppm, one.cpp, ../shared/extra.cpp
got=$(lib_compiles)
[ "$got" -eq "$units" ] || {
    for log in $(find . -name .ninja_log -path '*target*'); do echo "== $log"; awk -F'\t' 'NR>1 {print $4}' "$log" | grep -E "core830|one\.o|extra\.o"; done > w1.edges
    fail "W1: the library's $units units were compiled $got times" w1.log w1.edges
}
grep -q "workspace member probe.core830 in its own directory" w1.log || fail "W1: the shared build was not taken" w1.log

# W2
before=$(lib_compiles)
"$MCPP" build -p app2 > w2.log 2>&1 || fail "W2: the build of app2 failed" w2.log
after=$(lib_compiles)
[ "$before" = "$after" ] || fail "W2: building app2 compiled library units again ($before -> $after)" w2.log

# W3
sleep 1.1
printf '#pragma once\ninline int shared_value() { return 40; }\n' > shared/value.h
before=$(lib_compiles)
"$MCPP" build --workspace > w3.log 2>&1 || fail "W3: the rebuild failed" w3.log
after=$(lib_compiles)
[ $((after - before)) -eq 1 ] || fail "W3: a header outside the root caused $((after - before)) library compiles, not 1" w3.log
for a in app1 app2; do
    "$MCPP" run -p $a > run-$a.log 2>&1 || fail "W3: $a did not run" run-$a.log
    grep -qx 41 run-$a.log || fail "W3: $a did not take the changed header" run-$a.log
done

# W4
sleep 1.1
printf '#pragma once\ninline int shared_value() { return 90; }\n' > shared/value.h
before=$(lib_compiles)
"$MCPP" build -p app1 > w4a.log 2>&1 & p1=$!
"$MCPP" build -p app2 > w4b.log 2>&1 & p2=$!
wait $p1 || fail "W4: the concurrent build of app1 failed" w4a.log
wait $p2 || fail "W4: the concurrent build of app2 failed" w4b.log
after=$(lib_compiles)
[ $((after - before)) -eq 1 ] || fail "W4: two concurrent builds compiled the includer $((after - before)) times" w4a.log w4b.log
for a in app1 app2; do
    "$MCPP" run -p $a > run4-$a.log 2>&1 || fail "W4: $a did not run" run4-$a.log
    grep -qx 91 run4-$a.log || fail "W4: $a did not take the changed header" run4-$a.log
done

echo "OK"
