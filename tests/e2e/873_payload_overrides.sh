#!/usr/bin/env bash
# requires: gcc unix-shell
# `[xlings.overrides]`, `MCPP_XLINGS_OVERRIDE_<NS>_<NAME>` and config.toml
# state where a declared payload comes from (mcpp#755): an overridden payload
# is not provisioned, the build program receives the override, and the build
# says so on a `Using` line and in its `Finished` line.
#
# THE ASSERTION IS ON WHAT mcpp ASKS FOR. The payload is a name no index
# carries, and every build runs with `MCPP_NO_AUTO_INSTALL=1`: a build that
# still wanted it is refused naming it, so "built" means "not asked for". The
# baseline is the control: the same project without an override is refused.
set -e

MCPP="${MCPP:-mcpp}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export NO_COLOR=1

# A plugin that declares the payload behind a feature, the way a rule package
# declares its tool, and a consumer whose build program reads it.
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
"xim:mcpp-e2e-absent-tool" = ">=2.0"

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
cp "$work/app/mcpp.toml" "$work/app/mcpp.toml.base"
cat > "$work/app/build.mcpp" <<'CPP'
import std;
import mcpp;
import e2e.tools.x;
int main() {
    std::string s = std::string("dir=") + mcpp::xpkg_dir("xim", "mcpp-e2e-absent-tool")
        + " source=" + mcpp::xpkg_source("xim", "mcpp-e2e-absent-tool")
        + " program=" + mcpp::xpkg_program("xim", "mcpp-e2e-absent-tool");
    mcpp::warning(s.c_str());
    return 0;
}
CPP
echo 'int main() { return 0; }' > "$work/app/src/main.cpp"
mkdir -p "$work/opt/bin"
printf '#!/bin/sh\necho tool\n' > "$work/opt/bin/absent-tool"; chmod +x "$work/opt/bin/absent-tool"

cd "$work/app"
probe() { rm -rf target; MCPP_NO_AUTO_INSTALL=1 "$MCPP" "$@" 2>&1 || true; }
fail() { echo "FAIL: $*"; echo "----"; echo "$out"; exit 1; }

# Control: declared and not overridden, so it is asked for.
out="$(probe build)"
grep -q "not provisioned" <<<"$out" || fail "the baseline did not ask for the payload"
grep -q "mcpp-e2e-absent-tool" <<<"$out" || fail "the refusal does not name the payload"

# The environment variable.
out="$(MCPP_XLINGS_OVERRIDE_XIM_MCPP_E2E_ABSENT_TOOL="$work/opt/bin/absent-tool" probe build)"
grep -q "Finished" <<<"$out" || fail "an env override still needed the payload"
grep -q "Using xim:mcpp-e2e-absent-tool ← $work/opt/bin/absent-tool  \[custom · env MCPP_XLINGS_OVERRIDE_XIM_MCPP_E2E_ABSENT_TOOL\]" <<<"$out" \
    || fail "no Using line naming the env override"
grep -q "dir=$work/opt source=override program=$work/opt/bin/absent-tool" <<<"$out" \
    || fail "the build program did not receive the override (root, source, program)"
grep -q "Finished .* · custom: xim:mcpp-e2e-absent-tool" <<<"$out" || fail "Finished does not summarise the source"

# The manifest, with a version that satisfies the plugin's requirement.
cp mcpp.toml.base mcpp.toml
printf '\n[xlings.overrides]\n"xim:mcpp-e2e-absent-tool" = { program = "%s", version = "2.1" }\n' \
    "$work/opt/bin/absent-tool" >> mcpp.toml
out="$(probe build)"
grep -q "\[custom · mcpp.toml:[0-9]*\]" <<<"$out" || fail "no Using line naming mcpp.toml"
# --managed-only refuses it, naming it.
out="$(probe --managed-only build)"
grep -q "managed-only" <<<"$out" && grep -q "mcpp-e2e-absent-tool" <<<"$out" \
    || fail "--managed-only did not refuse the override"

# A stated version below the requirement is refused, naming both sides.
cp mcpp.toml.base mcpp.toml
printf '\n[xlings.overrides]\n"xim:mcpp-e2e-absent-tool" = { program = "%s", version = "1.0" }\n' \
    "$work/opt/bin/absent-tool" >> mcpp.toml
out="$(probe build)"
grep -q ">=2.0" <<<"$out" && grep -q "e2e:plug" <<<"$out" || fail "a version below the requirement was accepted"

# A path that does not exist is refused, naming it.
cp mcpp.toml.base mcpp.toml
printf '\n[xlings.overrides]\n"xim:mcpp-e2e-absent-tool" = "%s"\n' "$work/nope" >> mcpp.toml
out="$(probe build)"
grep -q "does not exist" <<<"$out" || fail "a missing override path was accepted"

# A dependency may not state where a payload comes from.
cp mcpp.toml.base mcpp.toml
printf '\n[xlings.overrides]\n"xim:mcpp-e2e-absent-tool" = "/bin/sh"\n' >> "$work/plug/mcpp.toml"
out="$(probe build)"
grep -q "is a dependency of this build" <<<"$out" || fail "a dependency's override was accepted"

echo "PASS: overrides skip provisioning, reach the build program, and are reported"
