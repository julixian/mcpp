#!/usr/bin/env bash
# requires: elf
# 835_a_shared_library_is_placed_by_a_link.sh -- workspace design 2026-09-29
# §5.1, §5.3.
#
# A member's product directory is its program's runtime closure: the
# graph-built shared libraries the program loads are placed beside it. Two
# product directories that place one library share its bytes by a hard link
# where the file system allows one.
#
#   L1  each program runs from its product directory;
#   L2  the library in the two product directories is one file (one inode),
#       or, where links are not possible, two copies with equal content;
#   L3  a shared library that another shared library of the closure needs is
#       placed too (2026.9.29.1 placed only the libraries a member's own units
#       link, so a program whose library needs libffi did not start).
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

cat > mcpp.toml <<'EOF'
[workspace]
members = ["base", "shlib", "p1", "p2"]
EOF
mkdir -p base/src shlib/src p1/src p2/src
cat > base/mcpp.toml <<'EOF'
[package]
name = "base"
version = "0.1.0"

[targets.base]
kind = "shared"
EOF
printf 'export module basis;\nexport int base_v() { return 6; }\n' > base/src/base.cppm
cat > shlib/mcpp.toml <<'EOF'
[package]
name = "shlib"
version = "0.1.0"

[dependencies]
base = { path = "../base" }

[targets.shlib]
kind = "shared"
EOF
printf 'export module shlib;\nimport basis;\nexport int shared_v() { return base_v() + 1; }\n' > shlib/src/shlib.cppm
for p in p1 p2; do
    cat > $p/mcpp.toml <<EOF
[package]
name = "$p"
version = "0.1.0"

[dependencies]
shlib = { path = "../shlib" }

[targets.$p]
kind = "bin"
main = "src/main.cpp"
EOF
    printf 'import shlib;\nint main() { return shared_v() == 7 ? 0 : 1; }\n' > $p/src/main.cpp
done

"$MCPP" build --workspace > b.log 2>&1 || fail "the workspace build failed" b.log
dir=$(dirname "$(dirname "$(find target -path '*/bin/p1/p1' -type f | head -1)")")
[ -n "$dir" ] && [ -d "$dir" ] || fail "no product directory for p1" b.log

# L1
for p in p1 p2; do
    lib=$(find "$dir/$p" -maxdepth 1 -name 'libshlib.so*' | head -1)
    [ -n "$lib" ] || fail "L1: libshlib is not placed in bin/$p/" b.log
    "$dir/$p/$p" || fail "L1: bin/$p/$p did not run from its product directory"
done

# L2
a=$(find "$dir/p1" -maxdepth 1 -name 'libshlib.so' | head -1)
b=$(find "$dir/p2" -maxdepth 1 -name 'libshlib.so' | head -1)
[ -n "$a" ] && [ -n "$b" ] || fail "L2: libshlib.so missing from a product directory"
ia=$(stat -c %i "$a" 2>/dev/null || stat -f %i "$a")
ib=$(stat -c %i "$b" 2>/dev/null || stat -f %i "$b")
if [ "$ia" != "$ib" ]; then
    cmp -s "$a" "$b" || fail "L2: the two placements differ"
    echo "note: the placements are copies on this file system"
fi

# L3
for p in p1 p2; do
    [ -n "$(find "$dir/$p" -maxdepth 1 -name 'libbase.so*' | head -1)" ] \
        || fail "L3: libbase, which libshlib needs, is not placed in bin/$p/" b.log
done

echo "PASS: 835_a_shared_library_is_placed_by_a_link"
