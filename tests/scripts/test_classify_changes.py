#!/usr/bin/env python3
"""Fixture tests for .github/tools/classify_changes.py (rule R2).

A change of documentation that nothing names is `code=false`; a change of any
other path, of a document a test reads, or of nothing at all is `code=true`.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / ".github" / "tools" / "classify_changes.py"


def classify(root: Path, paths: list[str]) -> str:
    out = subprocess.run([sys.executable, str(SCRIPT), "--root", str(root)],
                         input="\n".join(paths), capture_output=True, text=True, check=True)
    return out.stdout.splitlines()[0]


class ClassifyChanges(unittest.TestCase):
    def setUp(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        (self.root / "docs").mkdir()
        (self.root / "docs" / "guide.md").write_text("guide\n")
        (self.root / "docs" / "examples.md").write_text("examples\n")
        (self.root / "tests" / "e2e").mkdir(parents=True)
        (self.root / "tests" / "e2e" / "616_examples.sh").write_text(
            'grep -q x "$ROOT/docs/examples.md"\n')
        (self.root / "src").mkdir()
        (self.root / "src" / "main.cpp").write_text("int main() {}\n")

    def tearDown(self) -> None:
        self._dir.cleanup()

    def test_documentation_that_nothing_names_is_not_code(self) -> None:
        self.assertEqual(classify(self.root, ["docs/guide.md", ".agents/docs/x.md",
                                              "README.md", "LICENSE"]), "code=false")

    def test_a_document_a_test_reads_is_code(self) -> None:
        self.assertEqual(classify(self.root, ["docs/examples.md"]), "code=true")

    def test_the_translation_of_a_document_a_test_reads_is_code(self) -> None:
        # The test names docs/examples.md; docs/zh/examples.md is its translation.
        self.assertEqual(classify(self.root, ["docs/zh/examples.md"]), "code=true")

    def test_any_other_path_is_code(self) -> None:
        self.assertEqual(classify(self.root, ["docs/guide.md", "src/main.cpp"]), "code=true")

    def test_an_empty_change_is_code(self) -> None:
        self.assertEqual(classify(self.root, []), "code=true")

    def test_the_repository_classifies_its_own_documents(self) -> None:
        # docs/20-toolchains.md is read by check_default_toolchain_docs.py.
        self.assertEqual(classify(REPO_ROOT, ["docs/20-toolchains.md"]), "code=true")
        # Release packaging copies the root README; the design-record index is
        # read by nothing that a documentation check does not already run.
        self.assertEqual(classify(REPO_ROOT, ["README.md"]), "code=true")
        self.assertEqual(classify(REPO_ROOT, [".agents/docs/README.md",
                                              ".agents/docs/2026-10-02-x.md"]), "code=false")

    def test_every_documentation_check_it_trusts_runs_in_the_docs_job(self) -> None:
        sys.path.insert(0, str(SCRIPT.parent))
        import classify_changes  # noqa: E402
        ci = (REPO_ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        docs_job = ci[ci.index("\n  docs:"):ci.index("\n  build-linux:")]
        for check in classify_changes.DOCS_JOB_CHECKS:
            with self.subTest(check=check):
                name = Path(check).name
                self.assertIn(name, docs_job if name != "gen_agents_index.py"
                              else (REPO_ROOT / ".github" / "tools" / "check_docs_structure.sh")
                              .read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
