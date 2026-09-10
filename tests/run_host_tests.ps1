$ErrorActionPreference = 'Stop'

$gcc = 'D:\CodeBlocks\MinGW\bin\gcc.exe'
$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root 'M55_Edgi_Pet_A1\applications'
$comm = Join-Path $app 'comm_ai'
$common = Join-Path $app 'pet_logic\common'
$shared = Join-Path $root 'pet_shared'
$voice = Join-Path $app 'voice'
$outputs = @()

function Build-And-Run([string]$name, [string[]]$sources) {
    $output = Join-Path $PSScriptRoot ($name + '.exe')
    $outputs += $output
    & $gcc -std=c99 -Wall -Wextra -Werror "-I$comm" "-I$common" "-I$shared" "-I$voice" @sources -o $output
    if ($LASTEXITCODE -ne 0) { throw "$name compile failed" }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}

try {
    Build-And-Run 'test_comm_link' @(
        (Join-Path $PSScriptRoot 'test_comm_link.c'),
        (Join-Path $comm 'comm_link.c'),
        (Join-Path $common 'protocol.c'),
        (Join-Path $common 'messages.c'),
        (Join-Path $common 'reliable.c'))
    Build-And-Run 'test_ai_action' @(
        (Join-Path $PSScriptRoot 'test_ai_action.c'),
        (Join-Path $comm 'ai_action.c'),
        (Join-Path $comm 'ai_client.c'))
    Build-And-Run 'test_ai_offline' @(
        (Join-Path $PSScriptRoot 'test_ai_offline.c'),
        (Join-Path $comm 'ai_offline.c'),
        (Join-Path $comm 'ai_response.c'),
        (Join-Path $comm 'ai_client.c'))
    Build-And-Run 'test_ai_client' @(
        (Join-Path $PSScriptRoot 'test_ai_client.c'),
        (Join-Path $comm 'ai_response.c'),
        (Join-Path $comm 'ai_client.c'))
    Build-And-Run 'test_service_status' @(
        (Join-Path $PSScriptRoot 'test_service_status.c'),
        (Join-Path $comm 'pet_service_status.c'))
    Build-And-Run 'test_voice_ring' @(
        (Join-Path $PSScriptRoot 'test_voice_ring.c'),
        (Join-Path $shared 'edgi_voice_ring.c'))
    Build-And-Run 'test_voice_session' @(
        (Join-Path $PSScriptRoot 'test_voice_session.c'),
        (Join-Path $voice 'voice_session.c'))
    Build-And-Run 'test_voice_response' @(
        (Join-Path $PSScriptRoot 'test_voice_response.c'),
        (Join-Path $voice 'voice_response.c'))
}
finally {
    Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.exe' -File |
        Remove-Item -Force
}
