## Project structure

This is an Unreal Engine project. 
Source code lives in `Source/`.
Work primarily within `Source/`.

## Agent skills

### Issue tracker

Issues and PRDs live as markdown files under `.scratch/<feature>/` in this repo. See `docs/agents/issue-tracker.md`.

### Triage labels

Default triage vocabulary (needs-triage, needs-info, ready-for-agent, ready-for-human, wontfix), recorded as a `Status:` line in each issue file. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

### Skill notes

**/tdd** — read `docs/agents/running-tests.md` before writing or running tests.

**/improve-codebase-architecture** — read `docs/agents/unreal-architecture.md` before generating candidates.

## Code conventions

- **.h** files in public folder. **.cpp** in private folder.

- **Comments describe the current code, not its history or roadmap.** Never reference past or future slices, issues, or work in a comment (e.g. "added in a later slice", "retargeted in #1", "TODO for the traffic slice"). A comment should explain what the code does and why, so it stays true regardless of when it's read. Put process/history in the issue tracker, not the source.
