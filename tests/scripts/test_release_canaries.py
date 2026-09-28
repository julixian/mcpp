#!/usr/bin/env python3
"""Tests for .github/tools/release_canaries.py (WS10 of the 2026-09-28 design).

The canary runner runs each command under a named bash (`CANARY_BASH`): on
Windows, `bash` by name is System32's WSL launcher, and the first release run
of the canaries failed every command that way. The runner also reports every
command, enforces `expect`, and removes only the project's own mcpp pin.
"""

from __future__ import annotations

import importlib.util
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / ".github" / "tools" / "release_canaries.py"

spec = importlib.util.spec_from_file_location("release_canaries", SCRIPT)
rc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rc)


class ShellArgv(unittest.TestCase):
    def test_the_named_bash_runs_the_command(self) -> None:
        os.environ["CANARY_BASH"] = "/opt/git/usr/bin/bash.exe"
        try:
            argv = rc.shell_argv("echo hi")
        finally:
            del os.environ["CANARY_BASH"]
        self.assertEqual(argv[0], "/opt/git/usr/bin/bash.exe")
        self.assertEqual(argv[1], "-c")
        self.assertTrue(argv[2].startswith("set -eo pipefail\n"))
        self.assertTrue(argv[2].endswith("echo hi"))

    def test_without_a_name_the_runner_uses_bash(self) -> None:
        os.environ.pop("CANARY_BASH", None)
        self.assertEqual(rc.shell_argv("true")[0], "bash")


@unittest.skipIf(shutil.which("bash") is None, "no bash on this host")
class Run(unittest.TestCase):
    def run_list(self, toml: str) -> int:
        with tempfile.TemporaryDirectory() as d:
            listing = Path(d) / "canaries.toml"
            listing.write_text(toml, encoding="utf-8")
            saved = rc.LIST
            rc.LIST = listing
            os.environ["MCPP"] = "/bin/true"
            try:
                return rc.cmd_run("probe")
            finally:
                rc.LIST = saved
                del os.environ["MCPP"]

    def test_every_command_held(self) -> None:
        self.assertEqual(self.run_list(
            '[[canary]]\nname = "probe"\nrepo = "a/b"\nref = "main"\nos = "x"\n'
            'commands = ["true", "echo canary-ok"]\n'
            'expect = { "echo canary-ok" = "canary-ok" }\n'), 0)

    def test_a_failing_command_and_a_missing_expectation_fail(self) -> None:
        self.assertEqual(self.run_list(
            '[[canary]]\nname = "probe"\nrepo = "a/b"\nref = "main"\nos = "x"\n'
            'commands = ["false"]\n'), 1)
        self.assertEqual(self.run_list(
            '[[canary]]\nname = "probe"\nrepo = "a/b"\nref = "main"\nos = "x"\n'
            'commands = ["echo other"]\nexpect = { "echo other" = "canary-ok" }\n'), 1)

    def test_a_pipeline_fails_on_its_first_command(self) -> None:
        self.assertEqual(self.run_list(
            '[[canary]]\nname = "probe"\nrepo = "a/b"\nref = "main"\nos = "x"\n'
            'commands = ["false | cat"]\n'), 1)


class Unpin(unittest.TestCase):
    def test_only_the_mcpp_pin_is_removed(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / ".xlings.json"
            path.write_text(json.dumps({"workspace": {"mcpp": "2026.9.28.1", "cmake": "4.0"},
                                        "mirror": "GLOBAL"}), encoding="utf-8")
            self.assertEqual(rc.cmd_unpin(d), 0)
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertNotIn("mcpp", data["workspace"])
            self.assertEqual(data["workspace"]["cmake"], "4.0")
            self.assertEqual(data["mirror"], "GLOBAL")


class Matrix(unittest.TestCase):
    def test_the_repository_list_is_well_formed(self) -> None:
        canaries = rc.canaries()
        names = [c["name"] for c in canaries]
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)))
        for c in canaries:
            for key in ("repo", "ref", "os", "commands"):
                self.assertTrue(c.get(key), f"canary {c['name']} has no {key}")
        # The gate holds the ecosystem's own projects (mcpp#736); a downstream
        # project validates a release in its own pull request after it.
        self.assertEqual(set(names), {"xlings", "mcppls"},
                         "a new canary is an ecosystem repository, added deliberately here")


if __name__ == "__main__":
    unittest.main(verbosity=2)
