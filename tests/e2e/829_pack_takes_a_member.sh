#!/usr/bin/env bash
# requires: unix-shell
# 829_pack_takes_a_member.sh -- #734 E3.
#
# `build`, `run` and `test` select a workspace member with `-p`, and so does
# `pack`: the member is resolved by the resolver every `-p` shares, and the pack
# runs in its directory.
#
#   P1  `mcpp pack -p lib` at the workspace root produces the archive that
#       `mcpp pack` in `lib/` produces, with the same contents;
#   P2  a relative `-o` keeps meaning the directory the command was typed in;
#   P3  a name that is no member is refused, naming the members.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p ws/lib/src ws/app/src
cat > ws/mcpp.toml <<'EOF'
[workspace]
members = ["lib", "app"]
EOF
cat > ws/lib/mcpp.toml <<'EOF'
[package]
namespace = "probe"
name      = "packlib"
version   = "0.1.0"

[targets.packlib]
kind = "lib"
EOF
printf 'export module probe.packlib;\nexport int one() { return 1; }\n' > ws/lib/src/packlib.cppm
printf '[package]\nname = "app829"\nversion = "0.1.0"\n' > ws/app/mcpp.toml
printf 'int main() { return 0; }\n' > ws/app/src/main.cpp

# P1
cd "$TMP/ws/lib"
"$MCPP" pack > inside.log 2>&1 || fail "P1: pack inside the member failed" inside.log
A=$(ls target/dist/*.tar.gz | head -1)
tar tzf "$A" | sort > "$TMP/inside.list"
rm -rf target/dist
cd "$TMP/ws"
"$MCPP" pack -p lib > root.log 2>&1 || fail "P1: pack -p lib at the root failed" root.log
B=$(ls lib/target/dist/*.tar.gz 2>/dev/null | head -1)
[ -n "$B" ] || fail "P1: pack -p did not write the member's archive" root.log
[ "$(basename "$A")" = "$(basename "$B")" ] || fail "P1: the archives are named differently: $(basename "$A") vs $(basename "$B")" root.log
tar tzf "$B" | sort > "$TMP/root.list"
diff -u "$TMP/inside.list" "$TMP/root.list" > "$TMP/diff.txt" || fail "P1: the archives differ" "$TMP/diff.txt"

# P2
"$MCPP" pack -p lib -o out829.tar.gz > p2.log 2>&1 || fail "P2: pack -p with -o failed" p2.log
[ -f "$TMP/ws/out829.tar.gz" ] || fail "P2: a relative -o did not land in the directory it was typed in" p2.log

# P3
if "$MCPP" pack -p nosuch > p3.log 2>&1; then fail "P3: an unknown member was accepted" p3.log; fi
grep -qi "nosuch" p3.log || fail "P3: the refusal does not name the value" p3.log

echo "OK"
