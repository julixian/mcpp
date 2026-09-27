## Summary

<!-- What changes, and why, in declarative sentences. Link the issues it closes. -->

## Criteria

<!-- For each change, the test that fails before it and passes after it, and
     where that test runs (host, CI job). A criterion that did not run is not
     a pass: say which ones are left to CI and why. -->

## Intersections

<!-- A release regression of 2026.9.27.1 had one shape three times: a new rule
     or feature crossed an existing invariant, and no test sat at the crossing
     (#725, #723, e2e 797). For each new gate, rule or feature in this change:

     - which existing invariant does it cross?
     - which test sits at that crossing?

     Write "none" only after looking. -->

| New rule or feature | Invariant it crosses | Test at the crossing |
|---|---|---|
|  |  |  |

## Compatibility

<!-- What an existing project, home or client observes after the upgrade:
     changed output, a changed default, a full rebuild, a migration. -->

## Checks before merging

- [ ] `bash .github/tools/check_docs_style.sh`, `check_docs_structure.sh` and `check_version_pins.sh` pass.
- [ ] `python3 .github/tools/check_workflow_assertions.py` passes (a step asserts what its name says).
- [ ] No commit on the branch carries an attribution trailer:
      `git log origin/main..HEAD -i --grep='Co-Authored-By'` prints nothing.
- [ ] The squash merge is given an explicit subject and body, so GitHub does not
      compose one from the branch's commits.
