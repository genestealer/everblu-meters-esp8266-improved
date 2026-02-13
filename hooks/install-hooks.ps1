# install-hooks.ps1
# Installs Git hooks from the hooks/ directory into .git/hooks/

$ErrorActionPreference = "Stop"

Write-Host "Installing Git hooks..." -ForegroundColor Cyan

$hooks = Get-ChildItem -Path "hooks" -File | Where-Object { $_.Extension -eq "" }

foreach ($hook in $hooks) {
    $source = $hook.FullName
    $dest = ".git\hooks\$($hook.Name)"
    
    Copy-Item $source $dest -Force
    Write-Host "✓ Installed $($hook.Name)" -ForegroundColor Green
}

Write-Host "`nGit hooks installed successfully!" -ForegroundColor Green
Write-Host "The pre-commit hook will now run automatically before each commit." -ForegroundColor Yellow
