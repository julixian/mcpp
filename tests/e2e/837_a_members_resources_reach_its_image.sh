#!/usr/bin/env bash
# requires: mingw-cross
# 837_a_members_resources_reach_its_image.sh -- workspace design 2026-09-29
# §15 and §17.
#
# In a workspace plan each selected member's [resources] is compiled against
# the member's directory and embedded into the member's images only
# (2026.9.29.1 read the virtual root's [resources], which is empty, so no
# member's icon, version resource or script reached its program).
#
#   R1  each member's program carries its own icon and not the other's;
#   R2  a member's synthesised version resource is its own [package]'s;
#   R3  a member's own script is compiled with the member's include_dirs.
#
# Linux to Windows through the MinGW cross toolchain, as e2e 198.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"
export MCPP_HOME="${MCPP_HOME:-$HOME/.mcpp}"

hexof() { od -An -v -tx1 "$1" | tr -d ' \n'; }
# A 4x1 32bpp icon whose sixteen pixel bytes are findable in the linked image
# (the layout of _windows_resources_body.sh).
write_icon() {   # $1 = file, $2 = 4 BGRA pixels as printf escapes
    printf '\x00\x00\x01\x00\x01\x00\x04\x01\x00\x00\x01\x00\x20\x00\x3c\x00\x00\x00\x16\x00\x00\x00\x28\x00\x00\x00\x04\x00\x00\x00\x02\x00\x00\x00\x01\x00\x20\x00\x00\x00\x00\x00\x14\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'"$2"'\x00\x00\x00\x00' > "$1"
}
ICON_CLI='\xd3\x1c\x7a\x45\x92\xe6\x0b\xa8\x41\xf7\x2d\x63\xbe\x50\x84\x19'
ICON_GUI='\x6c\xa2\x38\xd7\xe1\x4b\x95\x0f\x77\xc4\x1a\x8e\x2b\xf3\x60\xd5'
ICON_CLI_HEX='d31c7a4592e60ba841f72d63be508419'
ICON_GUI_HEX='6ca238d7e14b950f77c41a8e2bf360d5'

cat > mcpp.toml <<'EOF'
[workspace]
members = ["cli", "gui"]
EOF

mkdir -p cli/src cli/assets
write_icon cli/assets/app.ico "$ICON_CLI"
printf 'int main() { return 0; }\n' > cli/src/main.cpp
cat > cli/mcpp.toml <<'EOF'
[package]
name    = "cli"
version = "1.2.3"
authors = ["Cli Corp"]

[resources]
icon = "assets/app.ico"

[targets.cli]
kind = "bin"
main = "src/main.cpp"
EOF

# gui's script finds its identifiers through the member's include_dirs, and
# its icon beside the script.
mkdir -p gui/src gui/include gui/assets
write_icon gui/assets/app.ico "$ICON_GUI"
printf 'int main() { return 0; }\n' > gui/src/main.cpp
printf '#define GUI_ICON 101\n' > gui/include/gui_ids.h
cat > gui/gui.rc <<'EOF'
#include "gui_ids.h"
GUI_ICON ICON "assets/app.ico"
EOF
cat > gui/mcpp.toml <<'EOF'
[package]
name    = "gui"
version = "4.5.6"

[build]
include_dirs = ["include"]

[resources]
files = ["gui.rc"]
extra-inputs = ["assets/app.ico"]

[targets.gui]
kind = "bin"
main = "src/main.cpp"
EOF

"$MCPP" build --workspace --target x86_64-windows-gnu > b.log 2>&1 || fail "workspace build" b.log

CLI_EXE=$(find target -path '*/bin/cli/cli.exe' | head -1)
GUI_EXE=$(find target -path '*/bin/gui/gui.exe' | head -1)
[ -f "$CLI_EXE" ] && [ -f "$GUI_EXE" ] || { find target -name '*.exe'; fail "the members' programs" b.log; }

# R1
hexof "$CLI_EXE" | grep -q "$ICON_CLI_HEX" || fail "R1 cli.exe does not carry cli's icon" b.log
hexof "$CLI_EXE" | grep -q "$ICON_GUI_HEX" && fail "R1 cli.exe carries gui's icon" b.log
hexof "$GUI_EXE" | grep -q "$ICON_GUI_HEX" || fail "R1 gui.exe does not carry gui's icon" b.log
hexof "$GUI_EXE" | grep -q "$ICON_CLI_HEX" && fail "R1 gui.exe carries cli's icon" b.log

# R2
GEN_RC=$(find target -path '*/res/cli/cli.mcpp.rc' | head -1)
[ -f "$GEN_RC" ] || { find target -path '*/res/*'; fail "R2 no synthesised script for cli under res/cli/" b.log; }
grep -q 'FILEVERSION    1,2,3,0' "$GEN_RC" || fail "R2 cli's FILEVERSION is not cli's [package].version" "$GEN_RC"
grep -q '"CompanyName", "Cli Corp"' "$GEN_RC" || fail "R2 cli's CompanyName" "$GEN_RC"

# R3: gui's script compiled (the build succeeded), against gui's include_dirs.
NINJA=$(dirname "$(dirname "$(dirname "$CLI_EXE")")")/build.ninja
grep -A2 'rc_object .*gui\.rc' "$NINJA" | grep -q 'gui/include' \
    || fail "R3 gui.rc is not compiled with gui's include_dirs" "$NINJA"

echo "PASS: 837_a_members_resources_reach_its_image"
