$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBytes = (Get-ChildItem -LiteralPath $taskRoot -Force -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ('Workspace: {0:N2} MB ({1} bytes), including .git and build' -f ($taskBytes / 1000000), $taskBytes)
if ($taskBytes -ge 100000000) { throw 'Workspace exceeds the strict 100 MB budget' }
