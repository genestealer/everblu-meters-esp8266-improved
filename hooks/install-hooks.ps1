# install-hooks.ps1
# Installs Git hooks from the hooks/ directory into .git/hooks/

$ErrorActionPreference = "Stop"
$isWindows = ($env:OS -eq 'Windows_NT')

Write-Host "Installing Git hooks..." -ForegroundColor Cyan

$hooks = Get-ChildItem -Path "hooks" -File | Where-Object { $_.Extension -eq "" }
$gitHooksDir = Join-Path -Path ".git" -ChildPath "hooks"

foreach ($hook in $hooks) {
    $source = $hook.FullName
    $dest = Join-Path -Path $gitHooksDir -ChildPath $hook.Name
    
    Copy-Item $source $dest -Force
    
    # On Unix-like systems (Linux/macOS), set executable bit
    if (-not $isWindows) {
        try {
            & chmod +x -- $dest
            Write-Host "✓ Installed $($hook.Name) (executable)" -ForegroundColor Green
        }
        catch {
            Write-Warning "Failed to set executable bit on '$dest'. The hook may not run. Please run 'chmod +x \"$dest\"' manually."
            Write-Host "✓ Installed $($hook.Name)" -ForegroundColor Green
        }
    }
    else {
        Write-Host "✓ Installed $($hook.Name)" -ForegroundColor Green
    }
}

Write-Host "`nGit hooks installed successfully!" -ForegroundColor Green
Write-Host "The pre-commit hook will now run automatically before each commit." -ForegroundColor Yellow
