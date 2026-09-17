#requires -Version 7.0
[CmdletBinding()]
param(
    [string[]]$Paths = @(
        'rust_server/builder',
        'rust_server/px_credentials',
        'rust_server/px_node_protocol',
        'rust_server/px_pg',
        'rust_server/px_private_files',
        'rust_server/px_release_catalog',
        'rust_server/px_backup',
        'rust_server/px_console_server',
        'rust_server/px_auth_server/license',
        'rust_server/px_auth_server/src/config.rs',
        'rust_server/px_auth_server/storage',
        'rust_server/px_auth_server/tests',
        'rust_server/px_desk_server',
        'rust_client/px_service/service_core/src',
        'rust_client/px_service/src',
        'rust_client/px_sysinfo',
        'rust_client/px_uninstall',
        'rust_client/px_user_proxy'
    )
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$singleLetterPatterns = @(
    [regex]'\b(?:let|for)\s+(?:mut\s+)?(?<name>[a-z])\b',
    [regex]'\b(?:if\s+|while\s+)?let\s+(?:Some|Ok|Err)\(\s*(?<name>[a-z])\s*\)',
    [regex]'\b(?:Some|Ok|Err)\(\s*(?<name>[a-z])\s*\)\s*(?:=>|=)',
    [regex]'\|\s*(?:&\s*)?(?:mut\s+)?(?<name>[a-z])\s*(?:[:,|])'
)
$genericNamePattern = [regex]'\b(?:let|for)\s+(?:mut\s+)?(?<name>tmp|data|obj|item|thing|foo|bar)\b'
$tupleBindingPattern = [regex]::new(
    '\blet\s*\((?<bindings>[^)]{1,500})\)\s*=',
    [Text.RegularExpressions.RegexOptions]::Singleline
)
$functionParameterPattern = [regex]::new(
    '\bfn\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*<[^>{}]*>)?\s*\((?<parameters>[^)]{0,2000})\)',
    [Text.RegularExpressions.RegexOptions]::Singleline
)
$violations = [Collections.Generic.List[string]]::new()
$sourceFiles = [Collections.Generic.List[IO.FileInfo]]::new()

foreach ($relativePath in $Paths) {
    $resolvedPath = [IO.Path]::GetFullPath((Join-Path $repo $relativePath))
    if (-not $resolvedPath.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Naming-check path escapes the repository: $relativePath"
    }
    if (-not (Test-Path -LiteralPath $resolvedPath)) {
        throw "Naming-check path does not exist: $relativePath"
    }
    if ((Get-Item -LiteralPath $resolvedPath) -is [IO.FileInfo]) {
        if ([IO.Path]::GetExtension($resolvedPath) -eq '.rs') {
            $sourceFiles.Add((Get-Item -LiteralPath $resolvedPath))
        }
        continue
    }
    foreach ($sourceFile in Get-ChildItem -LiteralPath $resolvedPath -Filter '*.rs' -File -Recurse) {
        if ($sourceFile.FullName -match '[\\/](?:target|backup|node_modules|\.sqlx)[\\/]') {
            continue
        }
        $sourceFiles.Add($sourceFile)
    }
}

foreach ($sourceFile in $sourceFiles | Sort-Object FullName -Unique) {
    $relativeFile = [IO.Path]::GetRelativePath($repo, $sourceFile.FullName)
    $sourceText = [IO.File]::ReadAllText($sourceFile.FullName)
    foreach ($tupleMatch in $tupleBindingPattern.Matches($sourceText)) {
        foreach ($binding in $tupleMatch.Groups['bindings'].Value.Split(',')) {
            if ($binding -match '^\s*(?:mut\s+)?(?<name>[a-z])\s*$') {
                $violations.Add("${relativeFile}: tuple binding uses single-letter identifier '$($Matches['name'])'")
            }
            if ($binding -match '^\s*(?:mut\s+)?(?<name>tmp|data|obj|item|thing|foo|bar)\s*$') {
                $violations.Add("${relativeFile}: tuple binding uses context-free identifier '$($Matches['name'])'")
            }
        }
    }
    foreach ($functionMatch in $functionParameterPattern.Matches($sourceText)) {
        foreach ($parameter in $functionMatch.Groups['parameters'].Value.Split(',')) {
            if ($parameter -match '^\s*(?:&\s*)?(?:mut\s+)?(?<name>[a-z])\s*:') {
                $violations.Add("${relativeFile}: function parameter uses single-letter identifier '$($Matches['name'])'")
            }
            if ($parameter -match '^\s*(?:&\s*)?(?:mut\s+)?(?<name>tmp|data|obj|item|thing|foo|bar)\s*:') {
                $violations.Add("${relativeFile}: function parameter uses context-free identifier '$($Matches['name'])'")
            }
        }
    }
    $lineNumber = 0
    foreach ($line in [IO.File]::ReadLines($sourceFile.FullName)) {
        $lineNumber++
        $trimmed = $line.TrimStart()
        if ($trimmed.StartsWith('//')) {
            continue
        }
        $codeLine = ($line -split '//', 2)[0]
        foreach ($pattern in $singleLetterPatterns) {
            foreach ($match in $pattern.Matches($codeLine)) {
                $name = $match.Groups['name'].Value
                $violations.Add("${relativeFile}:${lineNumber}: single-letter identifier '$name'")
            }
        }
        foreach ($match in $genericNamePattern.Matches($codeLine)) {
            $name = $match.Groups['name'].Value
            $violations.Add("${relativeFile}:${lineNumber}: context-free identifier '$name'")
        }
    }
}

if ($violations.Count -gt 0) {
    $violations | Sort-Object -Unique | ForEach-Object { Write-Host "ERROR $_" -ForegroundColor Red }
    throw "Readable naming gate failed with $($violations.Count) violation(s)."
}

Write-Host "Readable naming gate passed for $(@($sourceFiles | Sort-Object FullName -Unique).Count) Rust source files."
