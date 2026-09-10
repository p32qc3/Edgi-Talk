param(
    [string]$EcsHost = '47.116.168.34',
    [string]$DeviceId = 'edgi-talk-01',
    [string]$FullBuildProject = 'D:\edgi_pet\M55_Edgi_Pet_A1'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

function Read-Required([string]$prompt) {
    $value = Read-Host $prompt
    if ([string]::IsNullOrWhiteSpace($value)) { throw "$prompt cannot be empty" }
    return $value
}

function Read-SecretText([string]$prompt) {
    $secure = Read-Host $prompt -AsSecureString
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        $value = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    }
    if ([string]::IsNullOrEmpty($value)) { throw "$prompt cannot be empty" }
    return $value
}

function ConvertTo-CString([string]$value) {
    return $value.Replace('\', '\\').Replace('"', '\"')
}

$ssid = Read-Required 'Wi-Fi name (SSID)'
$wifiPassword = Read-SecretText 'Wi-Fi password'
$deviceToken = Read-SecretText 'ECS EDGI_DEVICE_TOKEN'
if ($deviceToken.Length -lt 32) { throw 'Device token must contain at least 32 characters' }

$header = @"
#ifndef VOICE_PRIVATE_CONFIG_H
#define VOICE_PRIVATE_CONFIG_H

#define VOICE_PRIVATE_CONFIG_READY 1
#define VOICE_WIFI_SSID             "$(ConvertTo-CString $ssid)"
#define VOICE_WIFI_PASSWORD         "$(ConvertTo-CString $wifiPassword)"
#define VOICE_ECS_HOST              "$(ConvertTo-CString $EcsHost)"
#define VOICE_DEVICE_ID             "$(ConvertTo-CString $DeviceId)"
#define VOICE_DEVICE_TOKEN          "$(ConvertTo-CString $deviceToken)"

#endif /* VOICE_PRIVATE_CONFIG_H */
"@

$targets = @(
    (Join-Path $repoRoot 'M55_Edgi_Pet_A1\applications\voice\voice_private_config.h')
)
$fullBuildVoice = Join-Path $FullBuildProject 'applications\voice'
if (Test-Path -LiteralPath $fullBuildVoice) {
    $targets += Join-Path $fullBuildVoice 'voice_private_config.h'
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($target in $targets) {
    [IO.File]::WriteAllText($target, $header, $utf8)
    Write-Output "Configured private board file: $target"
}
Write-Output 'No API key was written to the board configuration.'
