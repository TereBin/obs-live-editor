[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^https://[^/]+\.workers\.dev/?$')]
    [string] $WorkerUrl
)

$ErrorActionPreference = 'Stop'
$NodePath = 'C:\Program Files\nodejs\node.exe'
$WranglerPath = Join-Path $PSScriptRoot 'node_modules\wrangler\bin\wrangler.js'

if (-not (Test-Path -LiteralPath $NodePath)) {
    throw "Node.js was not found at $NodePath"
}
if (-not (Test-Path -LiteralPath $WranglerPath)) {
    throw 'Wrangler was not found. Run npm install in the worker directory first.'
}

function Set-WorkerSecret {
    param(
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [string] $Value
    )

    $StartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $StartInfo.FileName = $NodePath
    $StartInfo.Arguments = "`"$WranglerPath`" secret put $Name"
    $StartInfo.WorkingDirectory = $PSScriptRoot
    $StartInfo.UseShellExecute = $false
    $StartInfo.RedirectStandardInput = $true

    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $StartInfo
    [void] $Process.Start()
    $Process.StandardInput.WriteLine($Value)
    $Process.StandardInput.Close()
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) {
        throw "Failed to upload $Name."
    }
}

$ClientId = Read-Host 'CHZZK Client ID'
if ([string]::IsNullOrWhiteSpace($ClientId)) {
    throw 'Client ID cannot be empty.'
}

$SecureClientSecret = Read-Host 'CHZZK Client Secret' -AsSecureString
$SecretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureClientSecret)
try {
    $ClientSecret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($SecretPointer)
    if ([string]::IsNullOrWhiteSpace($ClientSecret)) {
        throw 'Client Secret cannot be empty.'
    }

    Set-WorkerSecret -Name 'CHZZK_CLIENT_ID' -Value $ClientId
    Set-WorkerSecret -Name 'CHZZK_CLIENT_SECRET' -Value $ClientSecret
} finally {
    if ($SecretPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($SecretPointer)
    }
    $ClientSecret = $null
    $SecureClientSecret = $null
}

$WorkerUrl = $WorkerUrl.TrimEnd('/')
$Response = $null
for ($Attempt = 1; $Attempt -le 5 -and -not $Response; $Attempt++) {
    try {
        $Response = Invoke-RestMethod -Method Get -Uri "$WorkerUrl/oauth/start"
    } catch {
        if ($Attempt -eq 5) {
            throw
        }
        Start-Sleep -Seconds 2
    }
}
if (-not $Response.authorizationUrl -or -not $Response.state) {
    throw 'Worker validation failed.'
}

Write-Host 'Worker secrets configured and OAuth start endpoint verified.'
