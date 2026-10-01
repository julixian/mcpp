#!/usr/bin/env bash
# requires: gcc unix-shell
# `provision = "on-request"` (mcpp#755): a payload installed when a build
# program asks for it with `xpkg_request`, not before the program runs.
#
# Every build runs with `MCPP_NO_AUTO_INSTALL=1`, so "built" means "nothing was
# asked for", and a refusal names the requester. The pair is the criterion: the
# same project builds while the program does not ask, and is refused naming
# the program when it does.
set -e

MCPP="${MCPP:-mcpp}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export NO_COLOR=1

mkdir -p "$work/plug/src" "$work/plug/members" "$work/app/src"
cat > "$work/plug/mcpp.toml" <<'TOML'
[package]
name      = "plug"
namespace = "e2e"
version   = "0.1.0"

[build]
sources = ["src/plug.cppm"]

[features.tools-x]
sources = ["members/x.cppm"]

[feature-xlings.tools-x]
"xim:mcpp-e2e-absent-tool" = { version = "1.0.0", provision = "on-request" }

[targets.plug]
kind = "lib"
TOML
echo 'export module e2e.plug;' > "$work/plug/src/plug.cppm"
cat > "$work/plug/members/x.cppm" <<'CPP'
export module e2e.tools.x;
export namespace e2e::x { inline int v = 1; }
CPP
cat > "$work/app/mcpp.toml" <<TOML
[package]
name    = "app"
version = "0.1.0"

[build-dependencies.e2e]
plug = { path = "$work/plug", features = ["tools-x"], host-module = true }

[targets.app]
kind = "bin"
main = "src/main.cpp"
TOML
cat > "$work/app/build.mcpp" <<'CPP'
import std;
import mcpp;
import e2e.tools.x;
int main() {
    std::string s = std::string("source=") + mcpp::xpkg_source("xim", "mcpp-e2e-absent-tool");
    if (const char* ask = std::getenv("E2E_ASK"); ask && *ask) {
        if (std::string_view(ask) == "undeclared") {
            std::printf("mcpp:xpkg-request=xim:mcpp-e2e-undeclared\n");
            return 0;
        }
        mcpp::xpkg_request("xim", "mcpp-e2e-absent-tool");
        if (mcpp::xpkg_pending()) return 0;
    }
    mcpp::warning(s.c_str());
    return 0;
}
CPP
echo 'int main() { return 0; }' > "$work/app/src/main.cpp"

cd "$work/app"
probe() { rm -rf target; MCPP_NO_AUTO_INSTALL=1 "$MCPP" "$@" 2>&1 || true; }
fail() { echo "FAIL: $*"; echo "----"; echo "$out"; exit 1; }

out="$(probe build)"
grep -q "Finished" <<<"$out" || fail "a payload nobody asked for was required"
grep -q "source=pending" <<<"$out" || fail "the build program was not told the payload is pending"

out="$(E2E_ASK=1 probe build)"
grep -q "payloads requested by the build program of \`app\`" <<<"$out" \
    || fail "a request did not reach the installer naming the requester"
grep -q "mcpp-e2e-absent-tool@1.0.0" <<<"$out" || fail "the refusal does not name the payload"

out="$(E2E_ASK=undeclared probe build)"
grep -q "no manifest of this build declares" <<<"$out" || fail "a request for an undeclared payload was accepted"

out="$(E2E_ASK=1 MCPP_NO_AUTO_INSTALL=1 "$MCPP" emit build-database --format json 2>/dev/null || true)"
grep -q "MCPP_BUILD_DATABASE_PAYLOAD_DEFERRED" <<<"$out" || fail "planning did not defer the request"

echo "PASS: on-request payloads wait for a request, and a request names its requester"
