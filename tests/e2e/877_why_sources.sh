#!/usr/bin/env bash
# requires: gcc unix-shell python3
# `mcpp why sources|tool|payload` and `--format json` (mcpp#755): the decision
# record of a prepare, as the build reported it.
set -e

MCPP="${MCPP:-mcpp}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export NO_COLOR=1
mkdir -p "$work/app/src" "$work/opt/bin"
printf '#!/bin/sh\n' > "$work/opt/bin/tool"; chmod +x "$work/opt/bin/tool"
cat > "$work/app/mcpp.toml" <<TOML
[package]
name    = "app"
version = "0.1.0"

[xlings.workspace]
"xim:mcpp-e2e-absent-tool" = "1.0.0"

[xlings.overrides]
"xim:mcpp-e2e-absent-tool" = "$work/opt/bin/tool"
TOML
echo 'int main() { return 0; }' > "$work/app/src/main.cpp"
cd "$work/app"
fail() { echo "FAIL: $*"; echo "----"; echo "$out"; exit 1; }

out="$(MCPP_NO_AUTO_INSTALL=1 "$MCPP" why payload mcpp-e2e 2>&1)" || fail "why payload failed"
grep -q "payload:xim:mcpp-e2e-absent-tool  $work/opt/bin/tool" <<<"$out" || fail "the payload is not listed"
grep -q "custom · mcpp.toml:[0-9]*" <<<"$out" || fail "the origin is not listed"

out="$(MCPP_NO_AUTO_INSTALL=1 "$MCPP" why sources --format json 2>/dev/null)" || fail "why sources --format json failed"
python3 - "$out" <<'PY' || exit 1
import json, sys
env = json.loads(sys.argv[1])
assert env["kind"] == "mcpp.why.sources", env["kind"]
subjects = {s["subject"]: s for s in env["data"]["sources"]}
p = subjects["payload:xim:mcpp-e2e-absent-tool"]
assert p["class"] == "custom" and p["origin"]["kind"] == "manifest", p
assert "toolchain.build" in subjects, list(subjects)
PY

out="$("$MCPP" why nonsense 2>&1 || true)"
grep -q "is not a topic of \`mcpp why\`" <<<"$out" || fail "an unknown topic was accepted"

echo "PASS: mcpp why reports the decision record"
