param(
    [string]$Ssid,
    [SecureString]$Password,
    [int]$TimeoutSeconds = 45,
    [string]$OpenOcdPath = $env:OPENOCD_EXE,
    [string]$OpenOcdScripts = $env:OPENOCD_SCRIPTS
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Ssid)) {
    $Ssid = Read-Host "WiFi name (SSID)"
}
if ($null -eq $Password) {
    $Password = Read-Host "WiFi password (leave empty for an open network)" -AsSecureString
}

$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password)
try {
    $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
}
finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
}

$utf8 = [Text.UTF8Encoding]::new($false)
$ssidBytes = $utf8.GetBytes($Ssid)
$keyBytes = $utf8.GetBytes($plainPassword)
$plainPassword = $null

if ($ssidBytes.Length -lt 1 -or $ssidBytes.Length -gt 32) {
    throw "SSID must be 1 to 32 UTF-8 bytes."
}
if ($keyBytes.Length -gt 64) {
    throw "Password must be at most 64 UTF-8 bytes."
}

$openOcd = $OpenOcdPath
$scripts = $OpenOcdScripts
if ([string]::IsNullOrWhiteSpace($openOcd) -or [string]::IsNullOrWhiteSpace($scripts)) {
    throw "Set OPENOCD_EXE and OPENOCD_SCRIPTS to your local Infineon OpenOCD paths."
}
if (-not (Test-Path -LiteralPath $openOcd)) {
    throw "OpenOCD was not found. Install the Infineon debugger package in RT-Thread Studio."
}
if (-not (Test-Path -LiteralPath $scripts)) {
    throw "OpenOCD scripts directory was not found."
}

$magic = [uint32]0x49464957
$seq = [uint32](([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -band 0xffffffffL)
if ($seq -eq 0) {
    $seq = 1
}

function Add-FnvByte([uint32]$Hash, [byte]$Value) {
    $mixed = [uint32]($Hash -bxor [uint32]$Value)
    return [uint32](([uint64]$mixed * 16777619L) -band 0xffffffffL)
}

$checksum = [uint32]2166136261
foreach ($value in [BitConverter]::GetBytes($seq)) {
    $checksum = Add-FnvByte $checksum $value
}
$checksum = Add-FnvByte $checksum ([byte]$ssidBytes.Length)
$checksum = Add-FnvByte $checksum ([byte]$keyBytes.Length)
foreach ($value in $ssidBytes) {
    $checksum = Add-FnvByte $checksum $value
}
foreach ($value in $keyBytes) {
    $checksum = Add-FnvByte $checksum $value
}

$packet = [byte[]]::new(128)
[BitConverter]::GetBytes($magic).CopyTo($packet, 0)
[BitConverter]::GetBytes($seq).CopyTo($packet, 4)
[BitConverter]::GetBytes([uint32]1).CopyTo($packet, 12)
$packet[20] = [byte]$ssidBytes.Length
$packet[21] = [byte]$keyBytes.Length
$ssidBytes.CopyTo($packet, 24)
$keyBytes.CopyTo($packet, 57)
[BitConverter]::GetBytes($checksum).CopyTo($packet, 124)
[Array]::Clear($keyBytes, 0, $keyBytes.Length)

$tempFile = Join-Path $env:TEMP ("edgi-wifi-{0:x8}.bin" -f $seq)
try {
    [IO.File]::WriteAllBytes($tempFile, $packet)
    [Array]::Clear($packet, 0, $packet.Length)

    $tempFileOcd = $tempFile.Replace("\", "/")
    $writeCommand = "init; halt; load_image {$tempFileOcd} 0x261C0100 bin; resume; shutdown"
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $writeOutput = & $openOcd -s $scripts -f interface/kitprog3.cfg `
        -f target/infineon/pse84xgxs2.cfg -c "transport select swd" `
        -c $writeCommand 2>&1
    $writeExitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($writeExitCode -ne 0) {
        throw "Could not send WiFi settings to the board.`n$($writeOutput -join "`n")"
    }

    Write-Host "Settings sent. Waiting for the board to connect to '$Ssid'..."
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 1000
        $ErrorActionPreference = "Continue"
        $readOutput = & $openOcd -s $scripts -f interface/kitprog3.cfg `
            -f target/infineon/pse84xgxs2.cfg -c "transport select swd" `
            -c "init; halt; mdw 0x261C0100 5; resume; shutdown" 2>&1
        $ErrorActionPreference = $savedPreference
        $line = $readOutput | Where-Object { $_ -match "0x261c0100:" } | Select-Object -Last 1
        if ($line -match "0x261c0100:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)") {
            $done = [Convert]::ToUInt32($Matches[3], 16)
            $status = [Convert]::ToUInt32($Matches[4], 16)
            $result = [Convert]::ToInt32($Matches[5], 16)
            if ($done -eq $seq) {
                if ($status -eq 3) {
                    Write-Host "Connected. This WiFi will reconnect automatically after restart."
                    exit 0
                }
                throw "The board could not connect (status=$status, result=$result). Check the WiFi name and password."
            }
        }
    }
    throw "Timed out waiting for the board. Make sure the new M55 firmware is running and USB debugging is connected."
}
finally {
    if (Test-Path -LiteralPath $tempFile) {
        Remove-Item -LiteralPath $tempFile -Force
    }
}
