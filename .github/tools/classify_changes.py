#!/usr/bin/env python3
"""Decide whether a change can affect anything but the documentation checks.

Rule R2 of .agents/docs/2026-10-02-pr-ci-acceleration-and-the-toolchain-
specification-design.md: what a job runs follows from what the commit changed.
Measured before this rule (2026-10-01): a commit that changed two files under
`.agents/docs` ran 35 jobs, 36 builds of mcpp and 436 runner minutes.

THE CLASSES

  documentation  `*.md` anywhere, `docs/**`, `.agents/**`, `LICENSE*`.
  code           everything else, and every documentation file that a script,
                 a test, a workflow or a source file names. The second half is
                 DERIVED, by searching the repository, rather than listed: a test
                 added later that reads a document is found without editing this
                 program. The search looks for the file's path and for the path of
                 its translation (`docs/X` and `docs/zh/X` name each other), since
                 a check that reads one language usually reads both. It does not
                 search for a bare file name: `README.md` is named by release
                 packaging, and a match on the name alone would make every design
                 record, whose index is `.agents/docs/README.md`, a code change.

A change whose every path is documentation that nothing names starts the
documentation checks only. Anything else, an empty change and a change that
cannot be listed included, starts the whole CI.

Usage:
    classify_changes.py [--root DIR] [--github-output FILE] < changed-paths
    One path per line on standard input, relative to the repository root.
    Prints `code=true` or `code=false` and one line per reason; with
    --github-output, appends `code=...` to that file as well.
"""
from __future__ import annotations

import argparse
import fnmatch
import os
import sys
from pathlib import Path

DOC_PATTERNS = ("*.md", "docs/*", ".agents/*", "LICENSE*")
SEARCHED = ("tests", ".github", "src", "modules", "tools", "mcpp.toml", "bench/src")
SKIPPED_DIRS = {".git", "target", "node_modules", "__pycache__"}

# A document read only by these files is still documentation: this program and
# its test name paths as data, and the checks of the `docs` job in ci.yml run on
# every change, documentation-only ones included. tests/scripts/
# test_classify_changes.py requires each check listed here to appear in ci.yml.
DOCS_JOB_CHECKS = (
    ".github/tools/check_docs_style.sh",
    ".github/tools/check_docs_structure.sh",
    ".github/tools/gen_agents_index.py",
    ".github/tools/check_target_tiers.py",
    ".github/tools/check_reason_tokens.sh",
    ".github/tools/check_matrix_reasons.sh",
    ".github/tools/check_version_pins.sh",
)
NOT_READERS = (".github/tools/classify_changes.py", "tests/scripts/test_classify_changes.py") \
    + DOCS_JOB_CHECKS


def is_documentation(path: str) -> bool:
    return any(fnmatch.fnmatchcase(path, p) for p in DOC_PATTERNS) or \
        fnmatch.fnmatchcase(Path(path).name, "*.md")


def searched_files(root: Path):
    for entry in SEARCHED:
        base = root / entry
        if base.is_file():
            yield base
            continue
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in SKIPPED_DIRS]
            for name in filenames:
                if name.endswith(".md"):
                    continue
                yield Path(dirpath) / name


def names_of(path: str) -> tuple[str, ...]:
    """The path, and the path of its translation under docs/."""
    if path.startswith("docs/zh/"):
        return path, "docs/" + path[len("docs/zh/"):]
    if path.startswith("docs/"):
        return path, "docs/zh/" + path[len("docs/"):]
    return (path,)


def readers_of(root: Path, docs: list[str]) -> dict[str, list[str]]:
    """Map each documentation path to the files that name it."""
    needles = {d: names_of(d) for d in docs}
    found: dict[str, list[str]] = {d: [] for d in docs}
    for f in searched_files(root):
        if str(f.relative_to(root)).replace(os.sep, "/") in NOT_READERS:
            continue
        try:
            text = f.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for d, names in needles.items():
            if any(n in text for n in names):
                found[d].append(str(f.relative_to(root)))
    return found


def classify(root: Path, paths: list[str]) -> tuple[bool, list[str]]:
    paths = [p.strip() for p in paths if p.strip()]
    if not paths:
        return True, ["the change lists no path"]
    code = [p for p in paths if not is_documentation(p)]
    if code:
        shown = ", ".join(code[:5]) + (" ..." if len(code) > 5 else "")
        return True, [f"{len(code)} path(s) outside the documentation: {shown}"]
    named = {d: r for d, r in readers_of(root, paths).items() if r}
    if named:
        return True, [f"{d} is read by {', '.join(r[:3])}" for d, r in sorted(named.items())]
    return False, [f"{len(paths)} documentation path(s), none named by a script, test or source"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--github-output")
    args = parser.parse_args()
    code, reasons = classify(Path(args.root).resolve(), sys.stdin.read().splitlines())
    value = "true" if code else "false"
    print(f"code={value}")
    for r in reasons:
        print(f"  {r}")
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as out:
            out.write(f"code={value}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
