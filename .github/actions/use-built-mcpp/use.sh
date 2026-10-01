#!/usr/bin/env bash
# The body of the use-built-mcpp action; see action.yml for what it provides.
#
# Usage: use.sh <host> <mirror>
set -euo pipefail

host="$1"
mirror="$2"

dir="$RUNNER_TEMP/mcpp-built"
case "$host" in
    windows-*) exe=mcpp.exe; dir="$(cygpath -u "$dir")" ;;
    *)         exe=mcpp ;;
esac
bin="$dir/$exe"
if [ ! -f "$bin" ]; then
    echo "::error::the artifact mcpp-built-$host holds no $exe"
    ls -la "$dir" || true
    exit 1
fi
chmod +x "$bin"

boot="${MCPP:-}"
if [ -z "$boot" ]; then
    echo "::error::MCPP is unset: run bootstrap-mcpp or setup-macos-llvm before use-built-mcpp"
    exit 1
fi

# The toolchain mcpp.toml names for this host is the one the build used, so it
# is the one whose payloads hold the binary's runtime.
manifest_toolchain() {
    local key
    case "$host" in
        macos-*)   key=macos ;;
        windows-*) key=windows ;;
        *)         key=default ;;
    esac
    awk -v k="$key" '
        /^\[/ { in_tc = ($0 == "[toolchain]") ; next }
        in_tc && $1 == k { gsub(/"/, "", $3); print $3; exit }
    ' mcpp.toml
}

if ! out=$("$bin" --version 2>&1); then
    tc="$(manifest_toolchain)"
    echo "this commit's mcpp does not run yet ($out); installing ${tc:-the default toolchain} with the bootstrap"
    if [ -n "$tc" ]; then
        "$boot" toolchain install "${tc%@*}" "${tc#*@}"
    fi
    if ! out=$("$bin" --version 2>&1); then
        echo "::error::this commit's mcpp does not run on this runner: $out"
        exit 1
    fi
fi
echo "this commit's mcpp: $out ($bin)"

{
    echo "MCPP_BOOT=$boot"
    echo "MCPP=$bin"
    echo "MCPP_FRESH=$bin"
    if [ -n "${XLINGS_BIN:-}" ]; then echo "MCPP_VENDORED_XLINGS=$XLINGS_BIN"; fi
} >> "$GITHUB_ENV"

if [ -n "${XLINGS_BIN:-}" ]; then
    "$XLINGS_BIN" config --mirror "$mirror" 2>/dev/null || true
fi
MCPP_VENDORED_XLINGS="${XLINGS_BIN:-}" "$bin" self config --mirror "$mirror"
