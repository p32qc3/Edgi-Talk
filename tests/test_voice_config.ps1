$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$private = 'M55_Edgi_Pet_A1/applications/voice/voice_private_config.h'
$example = Join-Path $root 'M55_Edgi_Pet_A1\applications\voice\voice_private_config.h.example'
$public = Join-Path $root 'M55_Edgi_Pet_A1\applications\voice\voice_config.h'

Push-Location $root
try {
    & git check-ignore -q -- $private
    if ($LASTEXITCODE -ne 0) { throw 'voice_private_config.h is not ignored' }
    if (-not (Test-Path -LiteralPath $example)) { throw 'private config example missing' }
    $exampleText = Get-Content -LiteralPath $example -Raw
    if ($exampleText -notmatch 'VOICE_PRIVATE_CONFIG_READY\s+0') {
        throw 'example must remain disabled'
    }
    $quoted = [regex]::Matches($exampleText, '"([^"]*)"')
    foreach ($match in $quoted) {
        if ($match.Groups[1].Value -ne 'UNSET') {
            throw 'example contains a non-UNSET value'
        }
    }
    if (-not (Test-Path -LiteralPath $public)) { throw 'voice_config.h missing' }
    $publicText = Get-Content -LiteralPath $public -Raw
    if ($publicText -notmatch 'NDEBUG' -or $publicText -notmatch '#error') {
        throw 'release configuration guard missing'
    }

    $tracked = & git grep -n 'DASHSCOPE_API_KEY=' -- ':!deploy/edgi-voice.env.example'
    foreach ($line in $tracked) {
        if ($line -match 'DASHSCOPE_API_KEY=sk-[A-Za-z0-9_-]+') {
            throw "possible DashScope secret in tracked file: $line"
        }
    }
}
finally {
    Pop-Location
}

Write-Output 'PASS: voice_config'
