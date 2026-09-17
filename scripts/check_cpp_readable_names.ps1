#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BaseRevision = '',
    [string[]]$Paths = @('src', 'tests')
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceExtensions = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
foreach ($extension in @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp')) {
    [void]$sourceExtensions.Add($extension)
}

$ignoredPathPattern = [regex]::new(
    '(^|/)(?:backup|third_party|generated|\.cxx)(/|$)|' +
    '^src/px_render/architecture/encoders/(?:amf/amf|nvenc/vencoder)(/|$)',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)
$prohibitedNamePattern = '(?<name>[a-wz]|tmp|data|obj|item|thing|foo|bar)'
$typePattern =
    '(?:(?:const|volatile)\s+)*' +
    '(?:auto|decltype\s*\([^)]*\)|' +
    '(?:(?:un)?signed\s+)?(?:char|short|int|long|float|double)|' +
    '[A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^;(){}=]+>)?)'
$declarationPattern = [regex]::new(
    '(?:^|[,(;])\s*' + $typePattern +
    '(?:\s+[*&]*\s*|\s*[*&]+\s*)' + $prohibitedNamePattern + '\b'
)
$structuredBindingPattern = [regex]::new(
    '\bauto\s*(?:&&|&)?\s*\[(?<bindings>[^]]+)\]'
)
$capturePattern = [regex]::new('\[(?<captures>[^]]+)\]')
$contextFreeNamePattern = [regex]::new(
    '^(?:[a-wz]|tmp|data|obj|item|thing|foo|bar)$'
)
$geometryNamePattern = [regex]::new(
    '(?:^|[,(;])\s*' + $typePattern +
    '(?:\s+[*&]*\s*|\s*[*&]+\s*)(?<name>[xy])\b'
)

function Resolve-BaseRevision {
    if ($BaseRevision) {
        return $BaseRevision
    }
    if ($env:CPP_NAMING_BASE_REVISION) {
        return $env:CPP_NAMING_BASE_REVISION
    }
    & git -C $repositoryRoot rev-parse --verify origin/master 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $mergeBase = & git -C $repositoryRoot merge-base HEAD origin/master
        if ($LASTEXITCODE -eq 0 -and $mergeBase) {
            return $mergeBase.Trim()
        }
    }
    return 'HEAD~1'
}

function Get-DiffText {
    param([string[]]$Arguments)

    $output = & git -C $repositoryRoot @Arguments -- @Paths
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect C++ naming diff: git $($Arguments -join ' ')"
    }
    return @($output)
}

function Get-UntrackedSourceDiff {
    $untrackedFiles = & git -C $repositoryRoot ls-files --others `
        --exclude-standard -- @Paths
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to inspect untracked C++ sources.'
    }
    $syntheticDiff = [Collections.Generic.List[string]]::new()
    foreach ($untrackedFile in $untrackedFiles) {
        $normalizedFile = $untrackedFile.Replace('\', '/')
        if (-not $sourceExtensions.Contains(
                [IO.Path]::GetExtension($normalizedFile)
            ) -or $ignoredPathPattern.IsMatch($normalizedFile)) {
            continue
        }
        $sourceLines = [IO.File]::ReadAllLines(
            (Join-Path $repositoryRoot $untrackedFile)
        )
        $syntheticDiff.Add("+++ b/$normalizedFile")
        $syntheticDiff.Add("@@ -0,0 +1,$($sourceLines.Count) @@")
        foreach ($sourceLine in $sourceLines) {
            $syntheticDiff.Add("+$sourceLine")
        }
    }
    return @($syntheticDiff)
}

function Remove-LiteralsAndComments {
    param([string]$Line)

    $withoutStrings = [regex]::Replace($Line, '"(?:\\.|[^"\\])*"', '""')
    $withoutCharacters = [regex]::Replace(
        $withoutStrings,
        "'(?:\\.|[^'\\])'",
        "''"
    )
    return ($withoutCharacters -split '//', 2)[0]
}

$resolvedBaseRevision = Resolve-BaseRevision
$diffSets = @(
    , (Get-DiffText -Arguments @(
        'diff', '--no-color', '--unified=0', $resolvedBaseRevision
    ))
    , (Get-UntrackedSourceDiff)
)
$violations = [Collections.Generic.HashSet[string]]::new()
$geometryReview = [Collections.Generic.HashSet[string]]::new()
$checkedLines = 0

foreach ($diffLines in $diffSets) {
    $currentFile = ''
    $newLineNumber = 0
    foreach ($diffLine in $diffLines) {
        if ($diffLine.StartsWith('+++ b/')) {
            $currentFile = $diffLine.Substring(6).Replace('\', '/')
            continue
        }
        if ($diffLine -match '^@@\s+-\d+(?:,\d+)?\s+\+(?<line>\d+)') {
            $newLineNumber = [int]$Matches['line']
            continue
        }
        if (-not $currentFile -or $diffLine.StartsWith('---')) {
            continue
        }
        if ($diffLine.StartsWith('+') -and -not $diffLine.StartsWith('+++')) {
            $extension = [IO.Path]::GetExtension($currentFile)
            if ($sourceExtensions.Contains($extension) -and
                -not $ignoredPathPattern.IsMatch($currentFile)) {
                $sourceLine = $diffLine.Substring(1)
                $trimmedLine = $sourceLine.TrimStart()
                if (-not $trimmedLine.StartsWith('#')) {
                    $codeLine = Remove-LiteralsAndComments $sourceLine
                    if ($codeLine.Trim()) {
                        ++$checkedLines
                        foreach ($match in $declarationPattern.Matches($codeLine)) {
                            [void]$violations.Add(
                                "${currentFile}:${newLineNumber}: identifier '$($match.Groups['name'].Value)'"
                            )
                        }
                        foreach ($match in $structuredBindingPattern.Matches($codeLine)) {
                            foreach ($binding in $match.Groups['bindings'].Value.Split(',')) {
                                $bindingName = $binding.Trim()
                                if ($contextFreeNamePattern.IsMatch($bindingName)) {
                                    [void]$violations.Add(
                                        "${currentFile}:${newLineNumber}: structured binding '$bindingName'"
                                    )
                                }
                            }
                        }
                        foreach ($match in $capturePattern.Matches($codeLine)) {
                            foreach ($capture in $match.Groups['captures'].Value.Split(',')) {
                                $captureName = (($capture.Trim() -replace '^[&*]', '') -split '=', 2)[0].Trim()
                                if ($contextFreeNamePattern.IsMatch($captureName)) {
                                    [void]$violations.Add(
                                        "${currentFile}:${newLineNumber}: lambda capture '$captureName'"
                                    )
                                }
                            }
                        }
                        foreach ($match in $geometryNamePattern.Matches($codeLine)) {
                            [void]$geometryReview.Add(
                                "${currentFile}:${newLineNumber}: review coordinate identifier '$($match.Groups['name'].Value)'"
                            )
                        }
                    }
                }
            }
            ++$newLineNumber
            continue
        }
        if (-not $diffLine.StartsWith('-')) {
            ++$newLineNumber
        }
    }
}

if ($geometryReview.Count -gt 0) {
    $geometryReview | Sort-Object | ForEach-Object {
        Write-Host "REVIEW $_" -ForegroundColor Yellow
    }
}
if ($violations.Count -gt 0) {
    $violations | Sort-Object | ForEach-Object {
        Write-Host "ERROR $_" -ForegroundColor Red
    }
    throw "C++ readable naming gate failed with $($violations.Count) violation(s)."
}

Write-Host (
    "C++ readable naming gate passed for {0} changed code lines against {1}." -f
        $checkedLines, $resolvedBaseRevision
)
