#!/usr/bin/env python3
"""The release canaries (.github/release-canaries.toml, WS10 of the 2026-09-28
ecosystem design), read by .github/workflows/release-canaries.yml.

    release_canaries.py matrix              the GitHub Actions matrix, as `matrix=<json>`
    release_canaries.py unpin <checkout>    remove the project's own mcpp pin from its checkout
    release_canaries.py run <name>          run a canary's commands in the current directory

`run` reads the candidate from `$MCPP`. Each command runs under bash with
`set -eo pipefail`, so a failure is the command's own; a command listed under
`expect` must also print the given text. The whole list runs, and the exit
status says whether every command held.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIST = ROOT / ".github" / "release-canaries.toml"


def canaries() -> list[dict]:
    with LIST.open("rb") as f:
        data = tomllib.load(f)
    out = data.get("canary", [])
    names = [c["name"] for c in out]
    if len(set(names)) != len(names):
        raise SystemExit(f"FAIL: duplicate canary names in {LIST}: {names}")
    for c in out:
        for key in ("name", "repo", "ref", "os", "commands"):
            if key not in c:
                raise SystemExit(f"FAIL: canary {c.get('name', '?')} has no `{key}`")
    return out


def cmd_matrix() -> int:
    include = [{
        "name": c["name"], "repo": c["repo"], "ref": c["ref"], "os": c["os"],
        "timeout": int(c.get("timeout", 60)),
        "submodules": bool(c.get("submodules", False)),
        "cache": "\n".join(c.get("cache", [])),
    } for c in canaries()]
    print("matrix=" + json.dumps({"include": include}, separators=(",", ":")))
    return 0


def cmd_unpin(checkout: str) -> int:
    """A project pins the mcpp it builds with in `.xlings.json`
    (`workspace.mcpp`); the canary builds with the candidate instead, so the
    pin is removed in the checkout and nothing installs the released mcpp."""
    path = Path(checkout) / ".xlings.json"
    if not path.is_file():
        print(f"{path}: no .xlings.json, nothing to unpin")
        return 0
    data = json.loads(path.read_text(encoding="utf-8"))
    ws = data.get("workspace", {})
    if "mcpp" in ws:
        print(f"{path}: removing workspace.mcpp = {ws.pop('mcpp')}")
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    else:
        print(f"{path}: no workspace.mcpp pin")
    return 0


def cmd_run(name: str) -> int:
    mcpp = os.environ.get("MCPP", "")
    if not mcpp:
        raise SystemExit("FAIL: $MCPP names no candidate")
    matches = [c for c in canaries() if c["name"] == name]
    if not matches:
        raise SystemExit(f"FAIL: no canary named {name}")
    c = matches[0]
    expect = c.get("expect", {})
    failed = []
    for command in c["commands"]:
        print(f"::group::{command}", flush=True)
        proc = subprocess.run(["bash", "-c", f"set -eo pipefail\n{command}"],
                              capture_output=True, text=True, check=False,
                              env={**os.environ, "MCPP": mcpp})
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        print("::endgroup::", flush=True)
        if proc.returncode != 0:
            failed.append(f"`{command}` exited {proc.returncode}")
            continue
        want = expect.get(command)
        if want and want not in proc.stdout + proc.stderr:
            failed.append(f"`{command}` did not print `{want}`")
    for f in failed:
        print(f"::error::canary {name}: {f}")
    print(f"canary {name}: {len(c['commands']) - len(failed)} of {len(c['commands'])} command(s) held")
    return 1 if failed else 0


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "matrix":
        return cmd_matrix()
    if argv[0] == "unpin" and len(argv) == 2:
        return cmd_unpin(argv[1])
    if argv[0] == "run" and len(argv) == 2:
        return cmd_run(argv[1])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
