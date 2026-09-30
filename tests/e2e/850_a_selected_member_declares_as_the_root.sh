#!/usr/bin/env bash
# requires: gcc
# 850 -- a selected workspace member's own declarations are the root's.
#
# Since 2026.9.29.1 a workspace is one plan whose root is a virtual manifest
# that declares only its members, by `path`. The rules that grant the root's
# own declarations a privilege read that virtual root's edges alone, so a
# rooted workspace's own package, and any member selected with `-p`, lost the
# position each held when it was planned as its own root (2026.9.28.3):
#
#   - a `path` override of a dependency that a transitive package requests by
#     another kind was refused ("... Pick one. declare it in the root"), which
#     is how the release canary of 2026.9.30.2 failed on mcpp-language-server,
#     whose root package overrides `openkal-linux` by `path`;
#   - a `linkage` stated on a member's dependency edge was ignored;
#   - the index refresh never considered a member's own dependencies.
#
# Criteria:
#   A. A rooted workspace's own package declares `framework` by `path`; a
#      library it uses declares it by `git`. `mcpp build` builds, states that
#      the root's declaration wins, and the program reads the path checkout.
#   B. The same for a member selected with `-p`.
#   C. Two selected members that declare `framework` by different kinds are
#      refused, naming both; so are two that point it at two directories.
#   D. A member's `linkage = "shared"` on its dependency edge is honoured: the
#      dependency is a shared library beside the member's program.
#   E. Two selected members that ask for two forms of one dependency are
#      refused, naming both; one of them alone builds.
#   F. The index refresh considers a member's own dependencies.
set -e
source "$(dirname "$0")/_host_path.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
bin_of() { find "$1" -path "*/bin/*" -name "$2$EXE" -type f | head -1; }

# ── a git origin for "framework": commit A answers 101, the working tree 199 ──
FW="$TMP/framework"
FW_HOST="$(host_path "$FW")"
mkdir -p "$FW/src"
git init --quiet "$FW"
git -C "$FW" config user.email "test@local"
git -C "$FW" config user.name "test"
printf '[package]\nname    = "framework"\nversion = "0.1.0"\n\n[build]\nsources = ["src/*.c"]\n\n[targets.framework]\nkind = "lib"\n' > "$FW/mcpp.toml"
printf 'int framework_marker(void) { return 101; }\n' > "$FW/src/framework.c"
git -C "$FW" add -A > /dev/null
git -C "$FW" commit --quiet -m A
REV_A=$(git -C "$FW" rev-parse HEAD)
printf 'int framework_marker(void) { return 199; }\n' > "$FW/src/framework.c"

# libg: a library outside every workspace that asks for framework at commit A.
LIBG_HOST="$(host_path "$TMP/libg")"
mkdir -p "$TMP/libg/src"
cat > "$TMP/libg/mcpp.toml" <<EOF
[package]
name    = "libg"
version = "0.1.0"

[build]
sources = ["src/*.c"]

[targets.libg]
kind = "lib"

[dependencies.framework]
git = "$FW_HOST"
rev = "$REV_A"
EOF
printf 'extern int framework_marker(void);\nint libg_marker(void) { return framework_marker(); }\n' > "$TMP/libg/src/libg.c"

main_file() {
    printf '#include <cstdio>\nextern "C" int libg_marker(void);\nint main() { std::printf("libg=%%d\\n", libg_marker()); return 0; }\n' > "$1"
}
# package <dir> <name> <framework-declaration>: a program that uses libg and
# declares framework itself.
package() {
    mkdir -p "$1/src"
    main_file "$1/src/main.cpp"
    cat > "$1/mcpp.toml" <<EOF
[package]
name    = "$2"
version = "0.1.0"

[dependencies]
libg = { path = "$LIBG_HOST" }
$3

[targets.$2]
kind = "bin"
main = "src/main.cpp"
EOF
}

# ── A ──────────────────────────────────────────────────────────────────────
W1="$TMP/w1"
package "$W1" app "framework = { path = \"$FW_HOST\" }"
package "$W1/tool" tool "framework = { path = \"$FW_HOST\" }"
cat >> "$W1/mcpp.toml" <<'EOF'

[workspace]
members = [".", "tool"]
EOF
(cd "$W1" && "$MCPP" build > "$TMP/a.log" 2>&1) || fail "A: the rooted workspace was refused" "$TMP/a.log"
grep -q "the root's declaration wins" "$TMP/a.log" || fail "A: no statement that the root's declaration wins" "$TMP/a.log"
[ "$("$(bin_of "$W1/target" app)" | tr -d '\r')" = "libg=199" ] \
    || fail "A: the program does not read the path checkout" "$TMP/a.log"
echo "ok: A, a rooted workspace's own path declaration wins over a library's git one"

# ── B ──────────────────────────────────────────────────────────────────────
(cd "$W1" && "$MCPP" build -p tool > "$TMP/b.log" 2>&1) || fail "B: -p tool was refused" "$TMP/b.log"
[ "$("$(bin_of "$W1/target" tool)" | tr -d '\r')" = "libg=199" ] \
    || fail "B: the selected member does not read the path checkout" "$TMP/b.log"
echo "ok: B, a member selected with -p declares as the root"

# ── C ──────────────────────────────────────────────────────────────────────
W2="$TMP/w2"
mkdir -p "$W2"
printf '[workspace]\nmembers = ["one", "two"]\n' > "$W2/mcpp.toml"
package "$W2/one" one "framework = { path = \"$FW_HOST\" }"
package "$W2/two" two "framework = { git = \"$FW_HOST\", rev = \"$REV_A\" }"
if (cd "$W2" && "$MCPP" build --workspace > "$TMP/c.log" 2>&1); then
    fail "C: two members that declare one dependency by two kinds were accepted" "$TMP/c.log"
fi
grep -q "two members this build selects" "$TMP/c.log" \
    && grep -q "'[a-z.]*one@path'" "$TMP/c.log" && grep -q "'[a-z.]*two@path'" "$TMP/c.log" \
    || fail "C: the refusal does not name both members" "$TMP/c.log"
# The same kind, two references: a second checkout of framework at commit A.
git clone --quiet "$FW" "$TMP/framework-copy"
FW_COPY_HOST="$(host_path "$TMP/framework-copy")"
package "$W2/two" two "framework = { path = \"$FW_COPY_HOST\" }"
if (cd "$W2" && "$MCPP" build --workspace > "$TMP/c2.log" 2>&1); then
    fail "C: two members that point a dependency at two directories were accepted" "$TMP/c2.log"
fi
grep -q "two members this build selects" "$TMP/c2.log" \
    && grep -q "'[a-z.]*one@path'" "$TMP/c2.log" && grep -q "'[a-z.]*two@path'" "$TMP/c2.log" \
    || fail "C: the refusal of two references does not name both members" "$TMP/c2.log"
echo "ok: C, two members that disagree about a dependency's checkout are refused, naming both"

# ── D ──────────────────────────────────────────────────────────────────────
W3="$TMP/w3"
mkdir -p "$W3/libs/src" "$W3/tool/src"
printf '[workspace]\nmembers = ["tool"]\n' > "$W3/mcpp.toml"
printf '[package]\nname    = "libs"\nversion = "0.1.0"\n\n[targets.libs]\nkind = "lib"\n' > "$W3/libs/mcpp.toml"
printf 'int libs_value() { return 7; }\n' > "$W3/libs/src/libs.cpp"
cat > "$W3/tool/mcpp.toml" <<'EOF'
[package]
name    = "tool"
version = "0.1.0"

[dependencies]
libs = { path = "../libs", linkage = "shared" }

[targets.tool]
kind = "bin"
main = "src/main.cpp"
EOF
printf '#include <cstdio>\nint libs_value();\nint main() { std::printf("%%d\\n", libs_value()); }\n' > "$W3/tool/src/main.cpp"
(cd "$W3" && "$MCPP" build -v > "$TMP/d.log" 2>&1) || fail "D: the member with a linkage request did not build" "$TMP/d.log"
tooldir=$(dirname "$(bin_of "$W3/target" tool)")
[ -n "$(find "$tooldir" -maxdepth 1 \( -name 'liblibs.so*' -o -name 'liblibs*.dylib' -o -name 'libs.dll' -o -name 'liblibs.dll' \) | head -1)" ] \
    || fail "D: the dependency is not a shared library beside the member's program" "$TMP/d.log"
! grep -q "only the root project decides link forms" "$TMP/d.log" \
    || fail "D: the member's request was reported as ignored" "$TMP/d.log"
[ "$("$(bin_of "$W3/target" tool)" | tr -d '\r')" = 7 ] || fail "D: the program does not run" "$TMP/d.log"
echo "ok: D, a member's linkage request is honoured"

# ── F ──────────────────────────────────────────────────────────────────────
grep -q "index: libs:" "$TMP/d.log" \
    || fail "F: the index refresh did not consider the member's own dependency" "$TMP/d.log"
echo "ok: F, the index refresh considers a member's own dependencies"

# ── E ──────────────────────────────────────────────────────────────────────
mkdir -p "$W3/tool2/src"
sed -e 's/"tool"/"tool2"/; s/targets.tool/targets.tool2/; s/linkage = "shared"/linkage = "static"/' \
    "$W3/tool/mcpp.toml" > "$W3/tool2/mcpp.toml"
cp "$W3/tool/src/main.cpp" "$W3/tool2/src/main.cpp"
printf '[workspace]\nmembers = ["tool", "tool2"]\n' > "$W3/mcpp.toml"
if (cd "$W3" && "$MCPP" build --workspace > "$TMP/e.log" 2>&1); then
    fail "E: two members asking for two forms of one dependency were accepted" "$TMP/e.log"
fi
grep -Eq "linked as '(shared|static)' by 'tool2?' and as '(shared|static)' by 'tool2?'" "$TMP/e.log" \
    && grep -q "'tool'" "$TMP/e.log" && grep -q "'tool2'" "$TMP/e.log" \
    && grep -q "'shared'" "$TMP/e.log" && grep -q "'static'" "$TMP/e.log" \
    || fail "E: the refusal does not name both members and both forms" "$TMP/e.log"
(cd "$W3" && "$MCPP" build -p tool2 > "$TMP/e2.log" 2>&1) || fail "E: one member alone did not build" "$TMP/e2.log"
echo "ok: E, two members asking for two link forms are refused; one alone builds"

echo "PASS: 850_a_selected_member_declares_as_the_root"
