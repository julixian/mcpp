#!/usr/bin/env python3
"""Every e2e test runs somewhere, or says why it cannot (rule R5).

WHY THIS EXISTS

`run_all.sh` exits 0 on a skip, and a skip line reads the same whether the
host legitimately lacks a capability or the runner was set up wrong. Measured
on 2026-10-01, before this check: 24 of 564 tests ran on no host and were named
by no workflow. Seven declared `llvm`, which no line granted; three declared
`musl`, whose probe named a release (15.1.0) the runners no longer installed;
seven declared `mingw-cross`, which no shard installed. Every one of those runs
was green.

WHAT IT CHECKS

It reads the per-test reports the shards write (`E2E_REPORT` of run_all.sh, one
`<status>\\t<test>\\t<ms>\\t<detail>` line per test) and decides, for every test
under tests/e2e:

  ran        some report says pass, fail or timeout;
  job        no report ran it, but a workflow names it (by file name, or by its
             number as an `E2E_ONLY` pattern such as `239_*.sh`): a dedicated job
             runs it and asserts its result itself;
  excused    tests/e2e/coverage-exceptions.tsv lists it with the reason no hosted
             runner can run it;
  uncovered  none of these. The check fails.

An exception for a test that ran, or for a test that does not exist, also fails
the check, so that the list cannot outlive the reason it records.

With --timings-out DIR it writes `<host>.tsv`, the measured duration of every
test that ran, merged across that host's shards, in the format run_all.sh reads
from E2E_TIMINGS; refreshing tests/e2e/timings/ is copying those files. A report
file is named `e2e-report-<host>-<shard>.tsv`.

Usage:
    check_e2e_coverage.py --reports DIR [--root DIR] [--timings-out DIR]
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

RAN = {"pass", "fail", "timeout"}
REPORT_NAME = re.compile(r"e2e-report-(?P<host>[a-z0-9-]+?)-(?P<shard>\d+)\.tsv$")


def read_reports(directory: Path):
    """Yield (host, shard, status, test, ms, detail) for every report line."""
    for path in sorted(directory.rglob("e2e-report-*.tsv")):
        m = REPORT_NAME.search(path.name)
        if not m:
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            ms = int(parts[2]) if parts[2].isdigit() else 0
            yield m["host"], int(m["shard"]), parts[0], parts[1], ms, parts[3] if len(parts) > 3 else ""


def read_exceptions(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    if not path.exists():
        return out
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        test, _, reason = line.partition("\t")
        out[test.strip()] = reason.strip()
    return out


def named_by_a_workflow(test: str, workflows: str) -> bool:
    number = test.split("_", 1)[0]
    return test in workflows or test[:-3] in workflows or f"{number}_*" in workflows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reports", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--timings-out")
    args = parser.parse_args()
    root = Path(args.root).resolve()

    tests = sorted(p.name for p in (root / "tests" / "e2e").glob("[0-9]*.sh"))
    workflows = "\n".join(p.read_text(encoding="utf-8")
                          for p in sorted((root / ".github" / "workflows").glob("*.yml")))
    exceptions = read_exceptions(root / "tests" / "e2e" / "coverage-exceptions.tsv")

    ran: dict[str, set[str]] = defaultdict(set)
    skipped: dict[str, set[str]] = defaultdict(set)
    shard_ms: dict[tuple[str, int], int] = defaultdict(int)
    timings: dict[str, dict[str, int]] = defaultdict(dict)
    reports = 0
    for host, shard, status, test, ms, detail in read_reports(Path(args.reports)):
        reports += 1
        if status in RAN:
            ran[test].add(host)
            shard_ms[(host, shard)] += ms
            timings[host][test] = ms
        elif status == "skip":
            skipped[test].add(f"{host}: {detail}")
    if reports == 0:
        print(f"no report under {args.reports}: nothing can be said about coverage")
        return 1

    uncovered, stale = [], []
    counts = defaultdict(int)
    for test in tests:
        if test in ran:
            counts["ran"] += 1
            if test in exceptions:
                stale.append(f"{test} is excused but ran on {', '.join(sorted(ran[test]))}")
        elif named_by_a_workflow(test, workflows):
            counts["job"] += 1
        elif test in exceptions:
            counts["excused"] += 1
        else:
            uncovered.append(f"{test}: {'; '.join(sorted(skipped[test])) or 'in no report'}")
    for test in exceptions:
        if test not in tests:
            stale.append(f"{test} is excused but does not exist")

    print(f"{len(tests)} tests: {counts['ran']} ran on a shard, {counts['job']} run by a "
          f"dedicated job, {counts['excused']} excused, {len(uncovered)} uncovered")
    for (host, shard), ms in sorted(shard_ms.items()):
        print(f"  {host} shard {shard}: {ms / 60000:.1f} min of tests")
    for line in uncovered:
        print(f"UNCOVERED: {line}")
    for line in stale:
        print(f"STALE EXCEPTION: {line}")

    if args.timings_out:
        out = Path(args.timings_out)
        out.mkdir(parents=True, exist_ok=True)
        for host, table in timings.items():
            (out / f"{host}.tsv").write_text(
                "".join(f"{t}\t{ms}\n" for t, ms in sorted(table.items())), encoding="utf-8")
    return 1 if uncovered or stale else 0


if __name__ == "__main__":
    sys.exit(main())
