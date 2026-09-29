#!/usr/bin/env bash
# requires: gcc
# 807_a_lib_root_imports_its_own_package.sh — a host-module package's lib root
# may import another unit of its own package (mcpp#720).
#
# The units of a host-module package are compiled in a list, and each unit sees
# only the BMIs of the units ahead of it. The list is ordered by what each unit
# imports. Until mcpp#720 the lib root was placed at the head of the list before
# the order was computed, so a lib root that imports a sibling was compiled
# first and failed with "module 'repro.helper' not found", while the same
# package built when `lib.path` named the sibling instead.
#
# The lib root is now the first node of the same sort: it is still emitted
# first whenever it imports nothing of its own package, so every package that
# built before keeps its order.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"

make_rules() {   # $1 = directory, $2 = "explicit" | "convention"
    local dir="$1" form="$2"
    mkdir -p "$dir/src"
    {
        printf '[package]\nnamespace = "repro"\nname = "rules"\nversion = "0.1.0"\n\n'
        if [[ "$form" == explicit ]]; then
            printf '[lib]\npath = "src/rules.cppm"\n\n'
        fi
        printf '[build]\nsources = ["src/*.cppm"]\n\n[targets.rules]\nkind = "lib"\n'
    } > "$dir/mcpp.toml"
    cat > "$dir/src/rules.cppm" <<'EOF'
export module repro.rules;
import repro.helper;
export int answer() { return helper_answer(); }
EOF
    cat > "$dir/src/helper.cppm" <<'EOF'
export module repro.helper;
export int helper_answer() { return 42; }
EOF
}

make_app() {   # $1 = directory
    local dir="$1"
    mkdir -p "$dir"
    cat > "$dir/mcpp.toml" <<'EOF'
[package]
namespace = "repro"
name = "app"
version = "0.1.0"

[build-dependencies]
"repro.rules" = { path = "../rules", host-module = true }

[build]
sources = ["main.cpp"]

[targets.app]
kind = "bin"
main = "main.cpp"
EOF
    cat > "$dir/build.mcpp" <<'EOF'
import repro.rules;
int main() { return answer() == 42 ? 0 : 1; }
EOF
    printf 'int main() { return 0; }\n' > "$dir/main.cpp"
}

for form in explicit convention; do
    rm -rf "$form"
    mkdir "$form"
    make_rules "$form/rules" "$form"
    make_app "$form/app"
    ( cd "$form/app" && "$MCPP" build > build.log 2>&1 ) || {
        cat "$form/app/build.log"
        echo "FAIL: $form lib root: a lib root that imports its own package's unit did not build"
        exit 1
    }
    grep -qE "^ *build\.mcpp .* ran [0-9]" "$form/app/build.log" || {
        cat "$form/app/build.log"
        echo "FAIL: $form lib root: the build program did not run"
        exit 1
    }
    echo "ok: $form lib root imports its own package's unit"
done

echo "PASS: 807_a_lib_root_imports_its_own_package"
