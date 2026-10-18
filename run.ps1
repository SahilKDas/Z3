param([switch]$Validate)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!(Test-Path ./build/z3.exe)) { throw 'Build first with ./build.ps1' }
    if ($Validate) { & ./build/z3.exe --validate } else { & ./build/z3.exe }
    if ($LASTEXITCODE -ne 0) { throw "Z3 exited with code $LASTEXITCODE" }
} finally { Pop-Location }
