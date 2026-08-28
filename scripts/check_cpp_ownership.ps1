param(
    [switch]$Staged,
    [string]$BaseRef = "",
    [switch]$ReportAll
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot
try {
    $nativeGlobs = @("*.cpp", "*.cc", "*.cxx", "*.h", "*.hpp")
    $violations = [System.Collections.Generic.List[string]]::new()

    if ($ReportAll) {
        $files = & rg --files src | Where-Object {
            $_ -match '\.(cpp|cc|cxx|h|hpp)$' -and
            ($_ -notmatch '^src[\\/]px_deps[\\/]px_3rdparty[\\/]' -or
             $_ -match '^src[\\/]px_deps[\\/]px_3rdparty[\\/]asio2[\\/]') -and
            $_ -notmatch '^src[\\/]px_deps[\\/]px_webrtc_client[\\/]'
        }
        foreach ($file in $files) {
            $lineNumber = 0
            foreach ($line in Get-Content -LiteralPath $file) {
                ++$lineNumber
                if ($line -match '\[[^\]]*\bthis\b[^\]]*\]' -or
                    $line -match '\bnew\s+[A-Za-z_:]' -or
                    $line -match '\bdelete\s+[A-Za-z_]') {
                    $violations.Add("${file}:${lineNumber}: $line")
                }
            }
        }
    }
    else {
        $diffArgs = @("diff", "--unified=0", "--no-ext-diff")
        if ($Staged) {
            $diffArgs += "--cached"
        }
        elseif ($BaseRef) {
            $diffArgs += "$BaseRef...HEAD"
        }
        $diffArgs += "--"
        $diffArgs += $nativeGlobs

        $currentFile = ""
        foreach ($line in (& git @diffArgs)) {
            if ($line -match '^\+\+\+ b/(.+)$') {
                $currentFile = $Matches[1]
                continue
            }
            if ($line -notmatch '^\+(?!\+\+\+)') {
                continue
            }
            if ($currentFile -match '^third_party/' -or
                $currentFile -match '^src/px_deps/px_webrtc_client/' -or
                ($currentFile -match '^src/px_deps/px_3rdparty/' -and
                 $currentFile -notmatch '^src/px_deps/px_3rdparty/asio2/')) {
                continue
            }
            $added = $line.Substring(1)
            # Comment-only additions cannot introduce ownership or lifetime
            # behavior. Ignoring them also prevents prose such as "new path"
            # from being mistaken for a C++ new-expression.
            if ($added -match '^\s*(?://|/\*|\*|\*/)') {
                continue
            }
            $isReviewedRawPointerBoundary =
                $added -match 'NOLINT\(gammaray-raw-pointer-boundary\)'
            if ($added -match '\[[^\]]*\bthis\b[^\]]*\]' -or
                $added -match '\bnew\s+[A-Za-z_:]' -or
                $added -match '\bdelete\s+[A-Za-z_]') {
                if (-not $isReviewedRawPointerBoundary) {
                    $violations.Add("${currentFile}: $added")
                }
            }
            # New project code may not declare raw pointers, including locals,
            # members, returns or parameters. A declaration forced by an
            # external ABI requires the reviewed boundary annotation defined
            # in docs/cpp_smart_pointer_standard.md.
            $rawPointerAtLineStart = $added -match '^\s*(?:(?:static|const|constexpr|volatile|mutable|inline|virtual|typename)\s+)*(?:[A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^;{}()=]+>)?|auto)\s*\*+\s*(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*'
            $rawPointerParameter = $added -match '[\(,]\s*(?:const\s+)?(?:[A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^;{}()=]+>)?|auto)\s*\*+\s*(?:const\s+)?[A-Za-z_][A-Za-z0-9_]*'
            if (($rawPointerAtLineStart -or $rawPointerParameter) -and
                -not $isReviewedRawPointerBoundary) {
                $violations.Add("${currentFile}: $added")
            }
        }
    }

    if ($violations.Count -gt 0) {
        Write-Error ("C++ ownership check failed ({0} violation(s)):`n{1}" -f
            $violations.Count, ($violations -join "`n"))
    }
    Write-Host "C++ ownership check passed: no new raw-pointer declarations, manual ownership, or [this] captures."
}
finally {
    Pop-Location
}
