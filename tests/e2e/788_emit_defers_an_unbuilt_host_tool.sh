#!/usr/bin/env bash
# requires: gcc python3
# 788 -- `emit build-database` builds no host tool (SPEC-005 R2.5 v1.4,
# mcpp-community/mcpp#707; before it, #699 item 2).
#
# `user` requests the host tool `t` of package `tool`, whose build carries a
# blocking `check` action that fails (docs/30's `dep_bin` pattern, and e2e 315's
# fixture for a blocking check). Planning describes a build and performs none
# (R2.2), and a tool sub-build is a whole compile of another package with its
# own actions: on a fresh store, one `emit` used to compile the tool and run
# its check. Criteria:
#   A. `emit --format json` in `user` with the tool not in the store: exit 0,
#      `data` present with `user`'s set, exactly one note
#      `MCPP_BUILD_DATABASE_HOST_TOOL_DEFERRED` naming the tool and its
#      package, and neither the tool's build nor its check ran.
#   B. `mcpp build` in `user` still exits non-zero on the failing check: the
#      tool's build itself is unchanged.
#   C. Once the tool is in the store, `emit` uses it and reports nothing.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
PY=python3

mkdir -p "$TMP/tool/src" "$TMP/user/src"

cat > "$TMP/tool/mcpp.toml" <<'EOF'
[package]
name    = "tool"
version = "0.1.0"

[targets.t]
kind = "bin"
main = "src/main.cpp"
EOF
echo 'int main() { return 0; }' > "$TMP/tool/src/main.cpp"
cat > "$TMP/tool/check.sh" <<'EOF'
#!/usr/bin/env bash
echo "the check says no" >&2
exit 1
EOF
chmod +x "$TMP/tool/check.sh"
# Same shape as e2e 315's blocking-check fixture: a `check` action, marked
# `blocking = true`, that fails.
cat > "$TMP/tool/build.mcpp" <<'EOF'
#include <string>
import mcpp;
int main() {
    const std::string root = mcpp::manifest_dir();
    mcpp::action a;
    a.id       = "gate";
    a.role     = "check";
    a.blocking = true;
    a.arg((root + "/check.sh").c_str())
     .output("${mcpp.out_dir}/gate.stamp")
     .submit();
}
EOF

cat > "$TMP/user/mcpp.toml" <<'EOF'
[package]
name    = "user"
version = "0.1.0"

[dependencies]
tool = { path = "../tool", tools = ["t"] }
EOF
echo 'int main() { return 0; }' > "$TMP/user/src/main.cpp"

cd "$TMP/user"

# ── A ──────────────────────────────────────────────────────────────────────
set +e
"$MCPP" emit build-database --format json > a.json 2> a.err
rc=$?
set -e
[ "$rc" = 0 ] || fail "A: emit exited $rc, expected 0" a.err a.json
"$PY" - a.json <<'EOF' || fail "A: the envelope" a.json
import json, sys
e = json.load(open(sys.argv[1]))
d = e["data"]
sets = [s["name"] for s in d["database"]["sets"]]
assert sets == ["user"], sets
diags = e["diagnostics"]
assert len(diags) == 1, diags
diag = diags[0]
assert diag["code"] == "MCPP_BUILD_DATABASE_HOST_TOOL_DEFERRED", diag
assert diag["severity"] == "note", diag
assert "'t'" in diag["message"] and "'tool'" in diag["message"], diag["message"]
EOF
if grep -q "Building.*host tool\|the check says no" a.err; then
    fail "A: planning built the tool or ran its check" a.err
fi
echo "ok: A, emit defers the tool and builds nothing"

# ── B ──────────────────────────────────────────────────────────────────────
set +e
"$MCPP" build > build.log 2>&1
rc=$?
set -e
[ "$rc" != 0 ] || fail "B: mcpp build succeeded despite the failing blocking check" build.log
grep -q "the check says no" build.log || fail "B: the check's own failure is not on the build's output" build.log
echo "ok: B, mcpp build still fails on the same blocking check"

# ── C ──────────────────────────────────────────────────────────────────────
printf '#!/usr/bin/env bash\nexit 0\n' > "$TMP/tool/check.sh"
"$MCPP" build > build2.log 2>&1 || fail "C: the build with a passing check failed" build2.log
set +e
"$MCPP" emit build-database --format json > c.json 2> c.err
rc=$?
set -e
[ "$rc" = 0 ] || fail "C: emit exited $rc, expected 0" c.err c.json
"$PY" - c.json <<'EOF' || fail "C: the envelope" c.json
import json, sys
e = json.load(open(sys.argv[1]))
assert e["diagnostics"] == [], e["diagnostics"]
EOF
echo "ok: C, a stored tool is used and nothing is reported"
echo "PASS: 788_emit_defers_an_unbuilt_host_tool"
