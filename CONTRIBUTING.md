# Contributing

Thanks for contributing to EverBlu meters.

## Source of truth for ESPHome component

The source of truth is:

- `src/`
- `ESPHOME/components/everblu_meter/`

The `ESPHOME-release/` folder is generated output.

## Do not edit generated files directly

Do not manually edit files in `ESPHOME-release/`.
Any direct edits will be overwritten the next time the release preparation script runs.

If you need to change generated content, edit the source files and regenerate:

### Windows

```powershell
./ESPHOME/prepare-component-release.ps1
```

### Linux or macOS

```bash
bash ./ESPHOME/prepare-component-release.sh
```

## Pull request expectations

If your PR changes files under `src/` or `ESPHOME/components/everblu_meter/`, you should also run the release preparation script and commit updated files in `ESPHOME-release/`.

A CI workflow validates that `ESPHOME-release/` matches generated output.
If it fails, run the script again from repository root and commit regenerated files.

## Code style & linting (pre-commit)

Linting and formatting are handled by [pre-commit](https://pre-commit.com) and mirror the
[ESPHome project's toolchain](https://developers.esphome.io/contributing/code/):

| Tool | Scope | Config |
| --- | --- | --- |
| **ruff** (lint + format) | all Python | [`ruff.toml`](ruff.toml) (mirrors ESPHome's rules) |
| **yamllint** | all YAML | [`.yamllint`](.yamllint) |
| **clang-format** | ESPHome component C++ | [`ESPHOME/components/everblu_meter/.clang-format`](ESPHOME/components/everblu_meter/.clang-format) (verbatim ESPHome style) |
| trailing-whitespace, end-of-file, check-yaml, … | repo hygiene | [`.pre-commit-config.yaml`](.pre-commit-config.yaml) |

The tools are installed **unpinned** (latest) into isolated environments, so there are no
versions to track or bump manually.

Set it up once, then it runs automatically on every commit:

```bash
pip install pre-commit
pre-commit install
```

Run it across the whole repo at any time:

```bash
pre-commit run --all-files
```

For C++ only, you can also use the helper scripts (no pre-commit required):

```powershell
./ESPHOME/format-component.ps1 -Fix     # Windows / PowerShell
```

```bash
ESPHOME/format-component.sh --fix        # Linux / macOS
```

These hooks are **not** a hard merge gate. They run locally on every commit once you have run
`pre-commit install`, and on pull requests the [pre-commit.ci](https://pre-commit.ci) app (if
enabled for the repository) runs them and can auto-fix issues for you.

The ESPHome component additionally follows ESPHome's C++ conventions (leaf classes marked
`final`, `this->` member access, trailing-underscore members). ESPHome's `clang-tidy` checks
are **not** wired into this repo because they require a full ESPHome build environment; see
[ESPHome `.clang-tidy`](https://github.com/esphome/esphome/blob/dev/.clang-tidy) for reference.

## CI and branch protection recommendation

Set the GitHub branch protection rule on `main` to require the check named:

- `ESPHOME Release Sync Check / verify-generated-release`

This prevents merge when generated release files are stale or manually edited.
