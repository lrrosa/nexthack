# AGENTS.md

**Read [`CLAUDE.md`](CLAUDE.md) first — it is the instruction file for this
repository, and everything an agent needs is there.** It is named for Claude
Code only because that is where it started; nothing in it is Claude-specific.

It covers how to build and run both targets, the Next/128K architecture split,
the code-banking scheme and `banks.json`, the memory budget that constrains
every change, the balance tooling, and the gotchas that otherwise cost an hour
each.

Two more things a non-Claude agent should know:

- **`.claude/skills/*/SKILL.md` are plain Markdown.** If your tool has no way to
  invoke a skill, read them as documents — `zrcp-verify/SKILL.md` is the
  emulator test harness (there are no automated behaviour tests in this repo;
  driving ZEsarUX over ZRCP *is* the test), and `bank-budget/SKILL.md` is the
  memory-map procedure. Both are procedures this project keeps needing.
- **This file is deliberately a pointer, not a copy.** Duplicating CLAUDE.md
  here would mean two documents to keep in sync, and the stale one would be
  believed. Add guidance to `CLAUDE.md`; leave this file alone.
