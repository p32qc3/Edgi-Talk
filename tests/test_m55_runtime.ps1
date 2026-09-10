param(
    [string]$OpenOcdRoot = 'D:\RT-ThreadStudio\repo\Extract\Debugger_Support_Packages\Infineon\OpenOCD-Infineon\2.0.0',
    [string]$ToolchainBin = 'D:\RT-ThreadStudio\repo\Extract\ToolChain_Support_Packages\ARM\GNU_Tools_for_ARM_Embedded_Processors\13.3\bin',
    [string]$M55Elf = 'D:\edgi_pet\M55_Edgi_Pet_A1\rt-thread.elf',
    [int]$BootWaitMs = 12000
)

$ErrorActionPreference = 'Stop'
$openOcd = Join-Path $OpenOcdRoot 'bin\openocd.exe'
$targetConfig = Join-Path $OpenOcdRoot 'scripts\target\infineon\pse84xgxs2.cfg.codexbak_20260608'
$nm = Join-Path $ToolchainBin 'arm-none-eabi-nm.exe'
foreach ($required in @($openOcd, $targetConfig, $nm, $M55Elf)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required runtime-check input not found: $required"
    }
}

$arguments = @(
    '-s', (Join-Path $OpenOcdRoot 'scripts'),
    '-f', 'interface/kitprog3.cfg',
    '-f', 'target/infineon/pse84xgxs2.cfg.codexbak_20260608',
    '-c', 'transport select swd',
    '-c', "init; reset run; sleep $BootWaitMs; targets cat1d.cm55; halt; reg; shutdown"
)

$output = (& $openOcd @arguments 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD failed:`n$output"
}
if ($output -match 'current mode:\s+Handler HardFault') {
    throw 'M55 entered HardFault during startup.'
}

$pcMatch = [regex]::Match($output, '(?m)^\(15\) pc \(/32\): 0x([0-9a-fA-F]{8})\b')
if (-not $pcMatch.Success) {
    throw "M55 program counter was not captured:`n$output"
}
$pc = [Convert]::ToUInt32($pcMatch.Groups[1].Value, 16)

$symbolLine = (& $nm -S --defined-only $M55Elf | Select-String -Pattern '\brt_assert_handler$' | Select-Object -First 1).Line
if (-not $symbolLine) {
    throw 'rt_assert_handler symbol was not found in the M55 image.'
}
$symbolMatch = [regex]::Match($symbolLine, '^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+rt_assert_handler$')
if (-not $symbolMatch.Success) {
    throw "Unexpected symbol format: $symbolLine"
}
$assertStart = [Convert]::ToUInt32($symbolMatch.Groups[1].Value, 16)
$assertSize = [Convert]::ToUInt32($symbolMatch.Groups[2].Value, 16)
if ($pc -ge $assertStart -and $pc -lt ($assertStart + $assertSize)) {
    throw ('M55 stopped in rt_assert_handler after {0} ms (PC=0x{1:X8}).' -f $BootWaitMs, $pc)
}

Write-Output ('PASS: M55 is running outside fault/assert handlers (PC=0x{0:X8})' -f $pc)
