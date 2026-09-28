[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $IsccPath = ''
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildSpec = Get-Content -LiteralPath (Join-Path $ProjectRoot 'buildspec.json') -Raw | ConvertFrom-Json
$BuildRoot = Join-Path $ProjectRoot "release\$Configuration\$($BuildSpec.name)"
$OutputRoot = Join-Path $ProjectRoot 'release'

if (-not (Test-Path -LiteralPath (Join-Path $BuildRoot 'bin\64bit\obs-live-editor.dll'))) {
    throw "Built plugin was not found under $BuildRoot. Run the CMake install step first."
}

if (-not $IsccPath) {
    $Candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $IsccPath = $Candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)) {
    throw 'Inno Setup 6 was not found. Install it with: winget install --id JRSoftware.InnoSetup'
}

$IssPath = Join-Path $PSScriptRoot 'obs-live-editor.iss'
& $IsccPath "/DMyAppVersion=$($BuildSpec.version)" "/DBuildRoot=$BuildRoot" "/DOutputRoot=$OutputRoot" $IssPath
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE."
}

$Installer = Join-Path $OutputRoot "obs-live-editor-$($BuildSpec.version)-windows-x64-setup.exe"
Write-Output $Installer
