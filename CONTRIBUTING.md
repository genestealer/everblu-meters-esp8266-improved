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

## CI and branch protection recommendation

Set the GitHub branch protection rule on `main` to require the check named:

- `ESPHOME Release Sync Check / verify-generated-release`

This prevents merge when generated release files are stale or manually edited.
