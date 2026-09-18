# Changelog: Agent documentation standardization

**Completed:** 2026-09-18 13:59 PDT

## Changed

- Added `Plan.md` and `Task.md` ignore rules for local task workflow artifacts.
- Added root-level `AGENTS.md` and `CLAUDE.md` pointers to the canonical
  `.claude/CLAUDE.md`.
- Kept the canonical document limited to title, summary, and Docker server
  build and test instructions.

## Validation

- Verified the pointer files and canonical document meet the required shape.
- Verified `make build` and `make test` are declared Makefile targets.
