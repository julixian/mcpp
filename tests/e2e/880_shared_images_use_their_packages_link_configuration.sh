#!/usr/bin/env bash
# requires: unix-shell
# DLL 的脚本库与静态依赖的链接选项必须在该 DLL 自己的链接边生效。
set -euo pipefail
MCPP="${MCPP:-mcpp}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; cat "$2"; exit 1; }
cd "$TMP"
mkdir -p owner/src helper/src app/src standalone/src
cat > mcpp.toml <<'EOF'
[workspace]
members = ["owner", "app"]
EOF
cat > owner/mcpp.toml <<'EOF'
[package]
name = "owner"
version = "0.1.0"
[build]
sources = ["src/*.c"]
[dependencies]
helper = { path = "../helper" }
[targets.owner]
kind = "shared"
EOF
cat > owner/build.mcpp <<'EOF'
import std;
import mcpp;
int main() {
    if (std::string_view(mcpp::target_os()) == "windows") {
        mcpp::link_lib("bcrypt");
    } else {
        mcpp::link_lib("m");
    }
}
EOF
cat > helper/mcpp.toml <<'EOF'
[package]
name = "helper"
version = "0.1.0"
[build]
sources = ["src/*.c"]
EOF
cat > helper/build.mcpp <<'EOF'
import std;
import mcpp;
int main() {
    if (std::string_view(mcpp::target_os()) == "windows") {
        mcpp::link_lib("userenv");
        mcpp::link_lib("delayimp");
        mcpp::link_flag("-Wl,/DELAYLOAD:userenv.dll");
    } else {
        mcpp::link_lib("m");
    }
}
EOF
cat > helper/src/helper.c <<'EOF'
#ifdef _WIN32
#include <windows.h>
#include <userenv.h>
int helper_value(void) {
    DWORD length = 0;
    GetProfilesDirectoryW(NULL, &length);
    return length != 0;
}
#else
#include <math.h>
int helper_value(void) { volatile double x = 0.0; return cos(x) == 1.0; }
#endif
EOF
cat > owner/src/owner.c <<'EOF'
int helper_value(void);
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
__declspec(dllexport) int owner_value(void) {
    unsigned char byte;
    return BCryptGenRandom(NULL, &byte, 1, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
        && helper_value() ? 42 : 0;
}
#else
#include <math.h>
int owner_value(void) {
    volatile double x = 0.0;
    return sin(x) == 0.0 && helper_value() ? 42 : 0;
}
#endif
EOF
for name in app standalone; do
    cat > "$name/mcpp.toml" <<EOF
[package]
name = "$name"
version = "0.1.0"
[dependencies]
owner = { path = "../owner" }
[targets.$name]
kind = "bin"
main = "src/main.c"
EOF
    printf 'int owner_value(void);\nint main(void) { return owner_value() == 42 ? 0 : 1; }\n' > "$name/src/main.c"
done
for mode in workspace member; do
    args=(--workspace)
    [ "$mode" != member ] || args=(-p app)
    "$MCPP" build "${args[@]}" > "$mode.log" 2>&1 || fail "$mode lost the DLL's link configuration" "$mode.log"
    exe=$(find target -type f \( -name app -o -name app.exe \) -path '*/bin/*' | head -1)
    [ -n "$exe" ] || fail "$mode built no app" "$mode.log"
    "$exe" || fail "$mode app could not use its shared library" "$mode.log"
done
"$MCPP" build -p owner > owner.log 2>&1 || fail "selecting only the DLL owner lost its flags" owner.log
cd standalone
"$MCPP" build > standalone.log 2>&1 || fail "a path dependency lost its DLL link configuration" standalone.log
exe=$(find target -type f \( -name standalone -o -name standalone.exe \) -path '*/bin/*' | head -1)
[ -n "$exe" ] || fail "the ordinary path consumer built no program" standalone.log
"$exe" || fail "the ordinary path consumer could not call the DLL" standalone.log
echo "ok: shared images use their owners' and static dependencies' link configuration"
