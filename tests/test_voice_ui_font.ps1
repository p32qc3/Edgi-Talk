$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$fontPath = Join-Path $root 'M55_Edgi_Pet_A1\applications\pet_font_22.c'
$font = Get-Content -LiteralPath $fontPath -Raw
$body = [regex]::Match($font,
    'static const uint16_t unicode_list\[\] = \{([^}]*)\}').Groups[1].Value
if (-not $body) { throw 'pet_font_22 unicode list missing' }

$supported = @{}
[regex]::Matches($body, '\d+') | ForEach-Object {
    $supported[[int]$_.Value + 32] = $true
}

function Phrase([string]$prefix, [int[]]$codepoints, [string]$suffix = '') {
    return $prefix + (-join ($codepoints | ForEach-Object { [char]$_ })) + $suffix
}

$phrases = @(
    (Phrase 'MIC ' @(0x70b9, 0x51fb, 0x5f00, 0x59cb)),
    (Phrase 'MIC ' @(0x6b63, 0x5728, 0x8bb0, 0x5f55) '...'),
    (Phrase 'AI ' @(0x601d, 0x8003, 0x4e2d) '...'),
    (Phrase 'AI ' @(0x6b63, 0x5728, 0x56de, 0x5e94) '...'),
    (Phrase 'MIC ' @(0x6ca1, 0x6709, 0x8bb0, 0x5f55, 0xff0c,
                     0x8bf7, 0x91cd, 0x8bd5)),
    (Phrase 'WIFI ' @(0x672a, 0x8fde, 0x63a5)),
    (Phrase 'WIFI ' @(0x8fde, 0x63a5, 0x672a, 0x6210, 0x529f))
)
foreach ($phrase in $phrases) {
    foreach ($character in $phrase.ToCharArray()) {
        if (-not $supported.ContainsKey([int]$character)) {
            throw "voice status glyph missing: $character"
        }
    }
}

Write-Output 'PASS: voice_ui_font'
