#!/usr/bin/env bash
# requires: unix-shell
# 824_a_build_program_reads_the_resolved_toolchain.sh -- #734 E2 (protocol 14).
#
# A build program reads the resolved toolchain as facts: the row's tools, the
# target ABI's native tools, the environment the engine runs them with, a
# path-free identity, the ninja mcpp runs, and the program's C++ runtime
# contract. A plugin driving a foreign build system translates them; mcpp
# interprets nothing. On the MSVC ABI the Windows rows of CI read the toolset's
# `cl`, `link` and SDK tools and a non-empty environment; on this row:
#
#   B1  `tool("cxx")` is the resolved compiler and `tool("cc")` its C driver,
#       both existing files; `tool("ld")` is the driver;
#   B2  `abi_tool(role)` equals `tool(role)` off the MSVC ABI;
#   B3  `tool_env()`, `msvc_instance_dir()` and `msvc_crt_linkage()` are empty;
#   B4  `toolset_identity()` names the family and version, and no path;
#   B5  `ninja_program()` runs;
#   B6  `cxx_runtime()` follows the manifest: the default, then a stated
#       `host-coupled`.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p p/src
cd p
printf 'int main() { return 0; }\n' > src/main.cpp
cat > build.mcpp <<'EOF'
import std;
import mcpp.core;
int main() {
    std::ofstream out(std::string(mcpp::out_dir()) + "/info.txt");
    for (const char* r : {"cc", "cxx", "ld", "ar", "rc", "as", "mt"})
        out << "tool." << r << "=" << mcpp::tool(r) << "\n"
            << "abi." << r << "=" << mcpp::abi_tool(r) << "\n";
    out << "env=" << mcpp::tool_env() << "\n"
        << "identity=" << mcpp::toolset_identity() << "\n"
        << "instance=" << mcpp::msvc_instance_dir() << "\n"
        << "ninja=" << mcpp::ninja_program() << "\n"
        << "runtime=" << mcpp::cxx_runtime() << "\n"
        << "crt=" << mcpp::msvc_crt_linkage() << "\n";
    return 0;
}
EOF
printf '[package]\nname = "info824"\nversion = "0.1.0"\n' > mcpp.toml
"$MCPP" build > b1.log 2>&1 || fail "the build failed" b1.log
INFO=$(find target -name info.txt | head -1)
test -s "$INFO" || fail "the build program wrote no info.txt" b1.log
val() { grep "^$1=" "$INFO" | head -1 | cut -d= -f2-; }

# B1
for r in cxx cc; do
    [ -x "$(val tool.$r)" ] || fail "B1: tool($r) is not an executable file" "$INFO"
done
[ "$(val tool.ld)" = "$(val tool.cxx)" ] || fail "B1: tool(ld) is not the driver" "$INFO"
# B2
for r in cc cxx ld ar rc as mt; do
    [ "$(val abi.$r)" = "$(val tool.$r)" ] || fail "B2: abi_tool($r) differs from tool($r) off the MSVC ABI" "$INFO"
done
# B3
for k in env instance crt; do
    [ -z "$(val $k)" ] || fail "B3: $k is not empty off the MSVC ABI" "$INFO"
done
# B4
id=$(val identity)
case "$id" in clang\ [0-9]*|gcc\ [0-9]*) ;; *) fail "B4: identity '$id' is not '<family> <version>'" "$INFO" ;; esac
case "$id" in */*) fail "B4: identity '$id' holds a path" "$INFO" ;; esac
# B5
"$(val ninja)" --version > /dev/null 2>&1 || fail "B5: ninja_program() does not run" "$INFO"
# B6
[ "$(val runtime)" = "self-contained" ] || fail "B6: the default contract is '$(val runtime)'" "$INFO"
printf '[package]\nname = "info824"\nversion = "0.1.0"\n\n[build]\ncxx_runtime = "host-coupled"\n' > mcpp.toml
"$MCPP" build > b2.log 2>&1 || fail "the second build failed" b2.log
INFO=$(find target -name info.txt -newer b1.log | head -1)
[ -n "$INFO" ] || INFO=$(find target -name info.txt | head -1)
[ "$(val runtime)" = "host-coupled" ] || fail "B6: the stated contract is '$(val runtime)'" "$INFO"

echo "OK"
