#!/usr/bin/env bash
# requires: windows
# 825_a_build_program_reads_the_msvc_toolset.sh -- #734 E2 on the MSVC ABI.
#
# On the MSVC ABI a build program reads the resolved toolset as facts, whether
# the row's driver is cl.exe or clang++: the toolset's `cl`, `link` and `lib`,
# the SDK's `rc` and `mt`, the environment the engine runs them with, a
# path-free identity, and the CRT linkage the program compiles with.
#
#   W1  `abi_tool("cxx")` is an existing cl.exe, `abi_tool("ld")` a link.exe;
#   W2  `abi_tool("rc")` and `abi_tool("mt")` are existing SDK tools;
#   W3  `tool_env()` carries INCLUDE and LIB;
#   W4  `toolset_identity()` reads "msvc <version>; sdk <version>";
#   W5  `msvc_crt_linkage()` is "dynamic" by default and "static" under
#       `cxx_runtime = "self-contained"`.
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
    for (const char* r : {"cxx", "ld", "ar", "rc", "mt"})
        out << "abi." << r << "=" << mcpp::abi_tool(r) << "\n";
    std::string env = mcpp::tool_env();
    out << "include=" << (env.find("INCLUDE=") != std::string::npos ? "yes" : "no") << "\n"
        << "lib=" << (env.find("\nLIB=") != std::string::npos || env.starts_with("LIB=") ? "yes" : "no") << "\n"
        << "identity=" << mcpp::toolset_identity() << "\n"
        << "crt=" << mcpp::msvc_crt_linkage() << "\n";
    return 0;
}
EOF
printf '[package]\nname = "msvc825"\nversion = "0.1.0"\n' > mcpp.toml
"$MCPP" build > b1.log 2>&1 || fail "the build failed" b1.log
INFO=$(find target -name info.txt | head -1)
test -s "$INFO" || fail "the build program wrote no info.txt" b1.log
val() { grep "^$1=" "$INFO" | head -1 | cut -d= -f2- | tr -d '\r'; }
exists() { [ -f "$(cygpath -u "$1" 2>/dev/null || echo "$1")" ]; }

case "$(val abi.cxx)" in *[Cc][Ll].exe) ;; *) fail "W1: abi_tool(cxx) is '$(val abi.cxx)'" "$INFO" ;; esac
exists "$(val abi.cxx)" || fail "W1: abi_tool(cxx) does not exist" "$INFO"
case "$(val abi.ld)" in *[Ll]ink.exe) ;; *) fail "W1: abi_tool(ld) is '$(val abi.ld)'" "$INFO" ;; esac
for r in rc mt; do exists "$(val abi.$r)" || fail "W2: abi_tool($r) does not exist" "$INFO"; done
[ "$(val include)" = yes ] || fail "W3: tool_env() has no INCLUDE" "$INFO"
[ "$(val lib)" = yes ] || fail "W3: tool_env() has no LIB" "$INFO"
case "$(val identity)" in msvc\ *\;\ sdk\ *) ;; *) fail "W4: identity is '$(val identity)'" "$INFO" ;; esac
[ "$(val crt)" = dynamic ] || fail "W5: the default CRT linkage is '$(val crt)'" "$INFO"

printf '[package]\nname = "msvc825"\nversion = "0.1.0"\n\n[build]\ncxx_runtime = "self-contained"\n' > mcpp.toml
"$MCPP" build > b2.log 2>&1 || fail "the self-contained build failed" b2.log
INFO=$(find target -name info.txt | head -1)
[ "$(val crt)" = static ] || fail "W5: the self-contained CRT linkage is '$(val crt)'" "$INFO"

echo "OK"
