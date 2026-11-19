# Package a built plugin into release/: a zip for users, a zip of debug symbols, and the
# Windows installer. Run after `cmake --build` (CI and local).
#   .\build-aux\package.ps1 [-Config RelWithDebInfo]
param(
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $root "buildspec.json") -Raw | ConvertFrom-Json).version
$release = Join-Path $root "release"
$stage = Join-Path $release "stage"
$plugin = Join-Path $stage "phone-cam"
$prefix = "phone-cam-$version-windows-x64"

if (Test-Path $release) { Remove-Item $release -Recurse -Force }

cmake --install (Join-Path $root "build_x64") --config $Config --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

# Users get the DLL and data only; symbols ship separately for crash analysis.
$symbols = Get-ChildItem $plugin -Recurse -Filter *.pdb
Compress-Archive -Path $symbols.FullName -DestinationPath (Join-Path $release "$prefix-pdb.zip")
$symbols | Remove-Item
Compress-Archive -Path $plugin -DestinationPath (Join-Path $release "$prefix.zip")

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
$isccPath = if ($iscc) { $iscc.Source } else {
    @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $isccPath) { throw "Inno Setup 6 (ISCC.exe) not found" }

& $isccPath /Q "/DAppVersion=$version" "/DStageDir=$plugin" "/O$release" (Join-Path $root "installer\phone-cam.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

Get-ChildItem $release -File | ForEach-Object { "{0,-55} {1,8:N0} KB" -f $_.Name, ($_.Length / 1KB) }
