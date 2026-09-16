---
name: apply-review
description: Implement a set of proposed code changes properly. Use when the user asks to apply, action or implement review feedback, suggestions from chat, a PR comment or an issue, and whenever changes need to be verified, covered by tests and recorded in the changelog before being called done.
---

# Apply review suggestions

**Origin:** written for this repository on 2026-09-16, not adapted from an external skill. It is the counterpart to [review-code.prompt.md](../../prompts/review-code.prompt.md), which produces the feedback this skill consumes. The phases encode this repo's own constraints (generated `ESPHOME-release/`, the dual ESP8266/ESP32 targets, `scripts/run-tests.ps1`, the Keep a Changelog format in `CHANGELOG.md`). Checked against the skill catalogues in [github/awesome-copilot](https://github.com/github/awesome-copilot) and [anthropics/skills](https://github.com/anthropics/skills) first: neither had an equivalent. Format follows the [Agent Skills specification](https://github.com/agentskills/agentskills).

Take a set of proposed changes (from a code review, a chat suggestion, a PR comment or an issue) and land them properly. Do not start editing until Phase 1 is agreed.

Follow [SKILL.md](../../../SKILL.md): surgical changes, simplicity first, every changed line traceable to a request.

## Phase 1: triage, then agree scope

1. Read the code each suggestion touches before judging it. A suggestion made without the surrounding context is often already handled elsewhere.
2. Classify each item:
   - **Apply** - correct and in scope.
   - **Apply with changes** - the problem is real, the proposed fix is not the right one. Say what you will do instead.
   - **Reject** - wrong, already handled, or out of scope. Give the reason.
   - **Defer** - real but belongs in its own change. Suggest an issue.
3. Check the repo constraints before committing to an approach:
   - `ESPHOME-release/` is generated. Changes go in `ESPHOME/components/everblu_meter/` or `src/`, then regenerate.
   - Anything in `src/core/` or `src/services/` compiles for **both** ESP8266 (Arduino, no `std::` threading) and ESP32/ESPHome.
   - `include/private.h` is gitignored. Never treat its absence as an error, never commit it.
4. Post the triage list and the resulting plan, one line per item with its verification step:

   ```
   1. [change] -> verify: [test / build / grep]
   ```

   Wait for the user to confirm before editing, unless they have already told you to just do it.

## Phase 2: implement

- One logical change at a time. Do not batch unrelated fixes into a single edit.
- Do not touch adjacent code, formatting or comments that the suggestion did not concern.
- Remove only the imports, variables and functions that **your** edit orphaned.
- Match the style of the file you are in: `src/` uses 4-space indentation, the ESPHome component uses ESPHome's clang-format.
- UK English in comments and docs. No em dashes.

After each edit, check for compile and lint errors in the files you touched before moving on.

## Phase 3: tests

A change without a test that would have failed before it is not finished.

- **Bug fix:** add a test that reproduces the bug, confirm it fails against the old behaviour, then confirm the fix makes it pass.
- **New behaviour:** cover the new path plus at least one boundary or failure case.
- **Refactor:** no new test, but the existing suite must pass unchanged. If it passes with the refactor reverted *and* applied but covers none of the moved code, say so.
- **ESPHome config validation:** add both a pytest case in `tests/esphome/` and, for a rejection, a negative fixture under `.ci/esphome/everblu_meter/`.

If an item is genuinely untestable in the host harness (for example a branch that is dead code under the native shims), state which item, which lines and why, rather than leaving it silently uncovered.

## Phase 4: verify

Run the checks, do not assume them:

```powershell
./scripts/run-tests.ps1          # host suites
./scripts/run-tests.ps1 -Build   # add this when src/ or the component changed
```

Then, as applicable:

- Component source changed: `./ESPHOME/format-component.ps1 -Fix`, then `./ESPHOME/prepare-component-release.ps1` to regenerate `ESPHOME-release/`.
- Python changed: `pre-commit run --all-files`.

Report the actual command output. "Should work" is not verification. If a step fails, fix the cause; do not retry the same command or weaken the test to make it pass.

## Phase 5: changelog

Add each user-visible change to the `[Unreleased]` section of [CHANGELOG.md](../../../CHANGELOG.md), under `Added`, `Changed`, `Fixed`, `Removed` or `Breaking Changes`. Create the heading if it is not there yet.

- Write for a user of the firmware, not for a reviewer of the diff. Lead with the effect, then the cause.
- Link the issue or PR: `([#67](https://github.com/genestealer/everblu-meters-esp8266-improved/issues/67))`.
- State which target is affected when they differ, for example "the MQTT build is unaffected".
- A breaking change needs a migration note saying what the user must change.
- Internal refactors, test-only changes and comment fixes do **not** get a changelog entry.
- Do not add a version heading or touch released sections. That is the [release](../release/SKILL.md) skill's job.

## Phase 6: report

Summarise in this order:

1. What was applied, one line each.
2. What was rejected or deferred, with the reason.
3. Commands run and their result.
4. Anything you noticed but did not touch.

Do not write a summary markdown file unless asked.
