[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputRoot,

    [string]$OutputRoot = "",

    [ValidateSet("Quality", "Compact")]
    [string]$Mode = "Quality",

    [switch]$Overwrite,

    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

function Require-Command([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        throw "Required tool '$Name' was not found in PATH. Install Khronos KTX-Software (toktx.exe) and ImageMagick (magick.exe)."
    }
    return $cmd.Source
}

function Is-DataTexture([string]$BaseName) {
    $n = $BaseName.ToLowerInvariant()
    return ($n -match '(^|[_\-.])(n|nm|normal|nh|spec|specular|gloss|rough|roughness|metal|metallic|ao|height|disp|displacement|mask)([_\-.]|$)')
}

function Has-UsefulAlpha([string]$SourcePath) {
    $channels = (& magick identify -format "%[channels]" "$SourcePath" 2>$null).Trim().ToLowerInvariant()
    return ($channels -match 'srgba|rgba|graya|cmyka|alpha')
}

$toktx = Require-Command "toktx"
$magick = Require-Command "magick"

$InputRoot = (Resolve-Path -LiteralPath $InputRoot).Path.TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $InputRoot
} else {
    if (-not (Test-Path -LiteralPath $OutputRoot)) {
        New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    }
    $OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path.TrimEnd('\', '/')
}

$extensions = @('.dds', '.tga', '.png', '.jpg', '.jpeg', '.bmp')
$files = Get-ChildItem -LiteralPath $InputRoot -File -Recurse | Where-Object {
    $extensions -contains $_.Extension.ToLowerInvariant()
}

if (-not $files) {
    Write-Host "No source textures found under: $InputRoot"
    exit 0
}

$converted = 0
$skipped = 0
$failed = 0
$warnings = 0

foreach ($file in $files) {
    $relative = $file.FullName.Substring($InputRoot.Length).TrimStart('\', '/')
    $relativeNoExt = [System.IO.Path]::ChangeExtension($relative, $null)
    $output = Join-Path $OutputRoot ($relativeNoExt + '.ktx2')
    $outDir = Split-Path -Parent $output
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    if ((Test-Path -LiteralPath $output) -and -not $Overwrite) {
        Write-Host "SKIP  $relative (KTX2 already exists)"
        $skipped++
        continue
    }

    $isData = Is-DataTexture $file.BaseName
    $hasAlpha = $false
    try {
        $hasAlpha = Has-UsefulAlpha $file.FullName
    } catch {
        Write-Warning "Could not inspect alpha for '$relative'; preserving RGBA to be safe."
        $hasAlpha = $true
        $warnings++
    }

    # BC1_OR_3 chosen by the engine depends on whether the KTX2 has alpha.
    # Data/normal maps are forced to RGBA so they become BC3 rather than BC1.
    $forceAlpha = $hasAlpha -or $isData

    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("arenamp_ktx2_" + [guid]::NewGuid().ToString('N') + '.png')
    try {
        if ($forceAlpha) {
            & $magick "$($file.FullName)" "PNG32:$temp"
        } else {
            & $magick "$($file.FullName)" "PNG24:$temp"
        }
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temp)) {
            throw "ImageMagick conversion failed with exit code $LASTEXITCODE"
        }

        $args = @('--t2', '--2d', '--genmipmap', '--nowarn')
        if ($isData) {
            $args += '--linear'
        } else {
            $args += '--srgb'
        }

        if ($Mode -eq 'Quality') {
            # Recommended for game assets, normals and future PC/Android reuse.
            $args += @('--uastc', '2', '--zcmp', '18')
        } else {
            # Smaller files, but visibly more lossy on normals/fine detail.
            $args += @('--bcmp', '--clevel', '2', '--qlevel', '255')
        }

        $args += @('--', $output, $temp)
        & $toktx @args
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output)) {
            throw "toktx failed with exit code $LASTEXITCODE"
        }

        Write-Host ("OK    {0} -> {1} [{2}{3}]" -f $relative, ($relativeNoExt + '.ktx2'), $Mode, $(if ($forceAlpha) { ', alpha/BC3' } else { ', opaque/BC1' }))
        $converted++

        if ($file.Extension.ToLowerInvariant() -eq '.dds') {
            # Re-encoding an already lossy BC/DXT source is not mathematically lossless.
            $warnings++
        }
    } catch {
        Write-Warning "FAIL  $relative : $($_.Exception.Message)"
        $failed++
        if (Test-Path -LiteralPath $output) {
            Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
        }
    } finally {
        if (-not $KeepTemp -and (Test-Path -LiteralPath $temp)) {
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host ""
Write-Host "ArenaMP KTX2 conversion finished"
Write-Host "  Converted: $converted"
Write-Host "  Skipped:   $skipped"
Write-Host "  Failed:    $failed"
if ($warnings -gt 0) {
    Write-Host "  Notes:     $warnings (DDS re-encodes / alpha detection warnings)"
}
Write-Host ""
Write-Host "Keep the original DDS/TGA/PNG files for the first test run. ArenaMP will prefer .ktx2 and automatically fall back to the original if KTX2 loading fails."
