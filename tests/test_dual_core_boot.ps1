param(
    [string]$OpenOcdRoot = 'D:\RT-ThreadStudio\repo\Extract\Debugger_Support_Packages\Infineon\OpenOCD-Infineon\2.0.0',
    [int]$BootWaitMs = 8000
)

$ErrorActionPreference = 'Stop'
$openOcd = Join-Path $OpenOcdRoot 'bin\openocd.exe'
if (-not (Test-Path -LiteralPath $openOcd)) {
    throw "OpenOCD not found: $openOcd"
}

$arguments = @(
    '-s', (Join-Path $OpenOcdRoot 'scripts'),
    '-s', (Join-Path $OpenOcdRoot 'flm\cypress\cat1d'),
    '-f', 'interface/kitprog3.cfg',
    '-f', 'target/infineon/pse84xgxs2.cfg',
    '-c', 'transport select swd',
    '-c', "init; reset run; sleep $BootWaitMs; halt; mdw 0x44160000 4; resume; shutdown"
)

$output = (& $openOcd @arguments 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD failed:`n$output"
}
if ($output -match 'current mode:\s+Handler HardFault') {
    throw 'M33 entered HardFault before dual-core startup completed.'
}

$registerLine = [regex]::Match($output, '(?m)^0x44160000:\s+([0-9a-fA-F]{8})\b')
if (-not $registerLine.Success) {
    throw "CM55 control register was not captured:`n$output"
}
$control = [Convert]::ToUInt32($registerLine.Groups[1].Value, 16)
if (($control -band 0x10) -ne 0) {
    throw ('CM55 CPU_WAIT is still set after {0} ms (CM55_CTL=0x{1:X8}).' -f $BootWaitMs, $control)
}

Write-Output ('PASS: M33 running and CM55 released (CM55_CTL=0x{0:X8})' -f $control)
