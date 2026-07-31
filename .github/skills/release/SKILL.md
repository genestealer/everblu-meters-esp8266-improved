---
name: release
description: Cut a new release of everblu-meters-esp8266-improved. Use when the user asks to prepare, cut, tag or publish a release, bump the version, roll up the changelog into a version section, write release notes, or check which merged and open PRs are going into the next version.
---

# Release process

End-to-end workflow for shipping a new version. Work through the phases in order.
**Phase 5 is a hard stop: never tag, push a tag, or publish a release without explicit approval from the user.**

## Repository facts

| Item | Value |
| --- | --- |
| Release remote | `origin` = `genestealer/everblu-meters-esp8266-improved` |
| Default branch | `main` |
| Working branch | `develop` |
| Tag format | `vMAJOR.MINOR.PATCH` (for example `v3.2.1`) |
| Version constant | `src/core/version.h` (`EVERBLU_FW_VERSION`, no `v` prefix) |
| Changelog | `CHANGELOG.md` (Keep a Changelog plus an `AI Metadata` YAML block per release) |
| Curated notes | `release_notes/RELEASE_NOTES_vX.Y.Z.md` |
| Generated output | `ESPHOME-release/` (regenerate, never hand-edit) |
| Release automation | `.github/workflows/release.yml`, triggered by pushing a `v*.*.*` tag |

`upstream` and `b4dpxl` remotes exist. Never push to them.

## Phase 1: gather the change set

1. Confirm the starting point:

   ```powershell
   git status --short
   git rev-parse --abbrev-ref HEAD
   git tag --sort=-creatordate | Select-Object -First 5
   ```

   The working tree must be clean. If it is not, stop and ask the user what to do with the pending changes.

2. List everything that has landed since the last tag:

   ```powershell
   git log v<LAST>..HEAD --no-merges --pretty=format:"%h %s"
   git log v<LAST>..HEAD --merges --pretty=format:"%h %s"
   git diff --stat v<LAST>..HEAD
   ```

3. Identify merged and open PRs. The `gh` CLI is **not** installed on this machine, so use the GitHub tools instead. Load them first with `tool_search` (they are deferred):

   - `github-pull-request_doSearch` for queries such as `repo:genestealer/everblu-meters-esp8266-improved is:pr is:merged merged:>YYYY-MM-DD` and `... is:pr is:open`.
   - `mcp_gitkraken_cli_pull_request_get_detail` to read an individual PR's description and discussion.

4. Report to the user:
   - merged PRs that are included in this release (number, title, one-line effect),
   - **open PRs**, flagged clearly, with a recommendation on whether each should be merged first or deferred,
   - any commit on `develop` that is not covered by a PR.

   Ask the user to confirm the scope before continuing if any open PR looks like it belongs in this release.

## Phase 2: choose the version

Apply semantic versioning against the current value in `src/core/version.h`:

- **patch**: bug fixes and internal changes only, no new configuration keys or entities,
- **minor**: new features, new ESPHome entities or YAML keys, backwards compatible,
- **major**: breaking changes to YAML configuration, MQTT topics, or pin/wiring expectations.

State the proposed version and the reasoning, then continue. Correct a wrong guess later is cheap; a wrong tag is not.

## Phase 3: update the files

1. **`src/core/version.h`**: set `EVERBLU_FW_VERSION` to the bare version (`"3.2.1"`, no `v`).

2. **`CHANGELOG.md`**:
   - Rename the `## [Unreleased]` heading to `## [vX.Y.Z] - YYYY-MM-DD` using today's date.
   - Leave the `## AI Notes For Maintainers And Tools` section untouched at the top. New versions go **below** it, above the previous release.
   - Do not add an empty `[Unreleased]` placeholder; the next piece of work recreates it.
   - Add an `### AI Metadata` block as the first subsection of the new release:

     ````markdown
     ### AI Metadata

     ```yaml
     release_type: patch
     base_branch: main
     release_branch: develop
     includes_prs: [136, 137]
     notable_superseded_work:
       - "short description of anything added then reverted within this release"
     scope_summary:
       - "one line per headline change"
     ```
     ````

     Omit `notable_superseded_work` if nothing was superseded.
   - Keep only `### Added`, `### Changed`, `### Fixed`, `### Removed` subsections that have content, in that order.
   - Fold in anything from the change set that is missing. Each entry starts with a **bold summary sentence** and then explains the user-visible effect and the cause. Link PRs and issues as `[#134](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/134)`.
   - If work was introduced and later superseded on the same branch, describe only the final behaviour and record the superseded step in the metadata block.

3. **`release_notes/RELEASE_NOTES_vX.Y.Z.md`**: create a curated, user-facing summary. Match the structure of the previous file in that folder:

   ```markdown
   # Release Notes - vX.Y.Z

   <one-line summary, including whether there are breaking changes>

   ## Highlights: <theme>

   - **Feature name** (`yaml_key`): what it does and why it matters.

   ## <Detailed section per significant change>

   ## Upgrade notes

   - Migration steps, or "No migration required."

   ## What's Changed

   - <PR title> by @<author> in [#NNN](https://github.com/genestealer/everblu-meters-esp8266-improved/pull/NNN)

   **Full Changelog**: https://github.com/genestealer/everblu-meters-esp8266-improved/compare/v<PREV>...vX.Y.Z
   ```

   This file is the body that goes on the GitHub release. It is a summary for users, not a copy of the changelog.

4. **Regenerate the ESPHome component** (required, because `version.h` is copied into it):

   ```powershell
   ./ESPHOME/prepare-component-release.ps1
   ```

   Never edit `ESPHOME-release/` by hand. The `esphome-release-sync-check` workflow fails the release if the committed output does not match a fresh run.

## Phase 4: validate

Run these from the repository root and report any failure rather than working around it:

```powershell
pio test -e native
pio run -e huzzah
pio run -e esp32dev
python -m pytest tests/esphome
python -m ruff check .
```

If the ESPHome component's C++ changed, also check formatting:

```powershell
./ESPHOME/format-component.ps1
```

Then commit:

```powershell
git add -A
git commit -m "Release vX.Y.Z"
```

Do not push yet.

## Phase 5: approval gate (STOP)

Present a summary and wait for an explicit go-ahead. Include:

- the version and why that bump level,
- the changelog entries added,
- the release notes body,
- the validation results,
- open PRs that are **not** in this release,
- the exact commands that will run next.

Do not run any of the Phase 6 commands until the user approves. If the user wants edits, loop back to Phase 3.

## Phase 6: publish (only after approval)

1. Push the release commit and get it onto `main`:

   ```powershell
   git push origin develop
   ```

   Then open a PR from `develop` to `main` and merge it once checks pass, or fast-forward `main` if the user prefers that. Ask which they want; do not force-push.

2. Tag the commit on `main` and push the tag:

   ```powershell
   git checkout main
   git pull origin main
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```

3. Pushing the tag triggers `.github/workflows/release.yml`, which creates the GitHub release. No firmware binaries are built or attached: users build from source with their own `private.h`.

4. **Replace the release body.** The workflow generates a generic body from the commit log, not the curated notes. Once the workflow finishes, tell the user to edit the release at
   `https://github.com/genestealer/everblu-meters-esp8266-improved/releases/tag/vX.Y.Z`
   and paste in the contents of `release_notes/RELEASE_NOTES_vX.Y.Z.md`. There is no `gh` CLI available to do this automatically.

5. Confirm the workflow run succeeded before calling the release done.

## Common mistakes

- Forgetting to regenerate `ESPHOME-release/` after bumping `version.h`, which fails the sync check.
- Tagging `3.2.1` instead of `v3.2.1`. The workflow only triggers on `v*.*.*`.
- Putting a `v` prefix inside `EVERBLU_FW_VERSION`. That constant is bare.
- Adding the new changelog section above the `AI Notes For Maintainers And Tools` block.
- Publishing before the user has approved the notes.

## Writing style

Follow the repository conventions in `.github/copilot-instructions.md`: UK English, no em dashes, plain direct language, no filler or puffery.
