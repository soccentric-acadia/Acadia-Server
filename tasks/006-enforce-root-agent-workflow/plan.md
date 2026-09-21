# Task 006 — Enforce root agent workflow (Acadia-Server)

Parent plan: [`../../../../tasks/006-enforce-root-agent-workflow/plan.md`](../../../../tasks/006-enforce-root-agent-workflow/plan.md)

- **Scope:** add the standard feature-change guard to `.claude/CLAUDE.md`.
- **Interface impact:** agent workflow only; no server API or runtime change.
- **Preservation:** do not modify the pre-existing `CMakeLists.txt` change.
- **Validation:** exact refusal text, pointer integrity, and scoped diff check.
