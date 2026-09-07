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
             $_ -match '^src[\\/]px_deps[\\/]px_3rdparty[\\/]asio2[\\/]include[\\/]asio2[\\/]') -and
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
        $addedCodeByFile = @{}
        $removedCodeCountsByFile = @{}
        $diffArgs = @("diff", "--unified=0", "--no-ext-diff")
        if ($Staged) {
            $diffArgs += "--cached"
        }
        elseif ($BaseRef) {
            $diffArgs += "$BaseRef...HEAD"
        }
        $diffArgs += "--"
        $diffArgs += $nativeGlobs

        $diffLines = [System.Collections.Generic.List[string]]::new()
        foreach ($line in (& git @diffArgs)) {
            $diffLines.Add($line)
        }
        if (-not $Staged -and -not $BaseRef -and $diffLines.Count -eq 0) {
            & git rev-parse --verify HEAD^ 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                $fallbackDiffArgs = @("diff", "--unified=0", "--no-ext-diff", "HEAD^...HEAD", "--") + $nativeGlobs
                foreach ($line in (& git @fallbackDiffArgs)) {
                    $diffLines.Add($line)
                }
            }
        }
        if ($BaseRef) {
            $workingDiffArgs = @("diff", "--unified=0", "--no-ext-diff", "--") + $nativeGlobs
            foreach ($line in (& git @workingDiffArgs)) {
                $diffLines.Add($line)
            }
        }
        if (-not $Staged) {
            $untrackedNativeFiles = & git ls-files --others --exclude-standard -- $nativeGlobs |
                Where-Object {
                    ($_ -match '^(src|tests)[\\/]' -or $_ -notmatch '[\\/]') -and
                    $_ -notmatch '(^|[\\/])(?:\.cxx[^\\/]*|build(?:_[^\\/]*)?|target(?:_[^\\/]*)?)([\\/]|$)'
                }
            foreach ($untrackedFile in $untrackedNativeFiles) {
                $normalizedFile = $untrackedFile.Replace('\', '/')
                $diffLines.Add("+++ b/$normalizedFile")
                foreach ($untrackedLine in Get-Content -LiteralPath $untrackedFile) {
                    $diffLines.Add("+$untrackedLine")
                }
            }
        }

        # Build the complete removed-line multiset before inspecting additions.
        # A formatter can move an unchanged line into an earlier diff hunk, so a
        # single forward pass would otherwise report it before seeing its removal.
        $removedFile = ""
        foreach ($line in $diffLines) {
            if ($line -match '^\+\+\+ b/(.+)$') {
                $removedFile = $Matches[1]
                continue
            }
            if ($line -match '^-(?!---)' -and $removedFile) {
                $normalizedRemoved = $line.Substring(1) -replace '\s+', ''
                if (-not $removedCodeCountsByFile.ContainsKey($removedFile)) {
                    $removedCodeCountsByFile[$removedFile] = @{}
                }
                $fileCounts = $removedCodeCountsByFile[$removedFile]
                $fileCounts[$normalizedRemoved] = 1 + [int]$fileCounts[$normalizedRemoved]
            }
        }

        $currentFile = ""
        foreach ($line in $diffLines) {
            if ($line -match '^\+\+\+ b/(.+)$') {
                $currentFile = $Matches[1]
                continue
            }
            if ($line -match '^-(?!---)') {
                continue
            }
            if ($line -notmatch '^\+(?!\+\+\+)') {
                continue
            }
            if ($currentFile -match '^third_party/' -or
                $currentFile -match '^src/px_deps/px_webrtc_client/' -or
                ($currentFile -match '^src/px_deps/px_3rdparty/' -and
                 $currentFile -notmatch '^src/px_deps/px_3rdparty/asio2/include/asio2/')) {
                continue
            }
            $added = $line.Substring(1)
            $normalizedAdded = $added -replace '\s+', ''
            if ($removedCodeCountsByFile.ContainsKey($currentFile)) {
                $fileCounts = $removedCodeCountsByFile[$currentFile]
                if ([int]$fileCounts[$normalizedAdded] -gt 0) {
                    $fileCounts[$normalizedAdded] = [int]$fileCounts[$normalizedAdded] - 1
                    continue
                }
            }
            if (-not $addedCodeByFile.ContainsKey($currentFile)) {
                $addedCodeByFile[$currentFile] =
                    [System.Collections.Generic.List[string]]::new()
            }
            $addedCodeByFile[$currentFile].Add($added)
            if ($added.Length -gt 150) {
                $violations.Add("${currentFile}: line exceeds the project limit of 150 characters ($($added.Length)): $added")
            }
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
            $inferredRawPointer = $added -match '^\s*(?:const\s+)?auto\s+[A-Za-z_][A-Za-z0-9_]*\s*=.*(?:mutable_[A-Za-z0-9_]+\s*\(|(?:->|\.)Add\s*\()'
            $isDereferenceReturn = $added -match '^\s*(?:co_)?return\s+\*'
            $isTestAssertion = $added -match '^\s*(?:EXPECT|ASSERT)_[A-Z_]+'
            if (($rawPointerAtLineStart -or $rawPointerParameter -or $inferredRawPointer) -and
                -not $isDereferenceReturn -and -not $isTestAssertion -and -not $isReviewedRawPointerBoundary) {
                $violations.Add("${currentFile}: $added")
            }
            if ($added -match '\.release\s*\(\s*\)' -and
                -not $isReviewedRawPointerBoundary) {
                $violations.Add(
                    "${currentFile}: smart-pointer release() requires a reviewed external ABI boundary and must never transfer ownership to a Qt parent: $added")
            }
        }

        foreach ($entry in $addedCodeByFile.GetEnumerator()) {
            $addedBlock = $entry.Value -join "`n"
            $smartPointerQtTransfer =
                $addedBlock -match '(?s)(?:make_unique|make_shared|unique_ptr|shared_ptr).{0,1200}?setParent\s*\(.{0,1200}?\.release\s*\('
            if ($smartPointerQtTransfer) {
                $violations.Add(
                    "$($entry.Key): smart-pointer ownership must not be transferred to a Qt parent with setParent()/release(); construct directly under the Qt parent and observe with QPointer")
            }
        }
    }

    if ($violations.Count -gt 0) {
        Write-Error ("C++ ownership check failed ({0} violation(s)):`n{1}" -f
            $violations.Count, ($violations -join "`n"))
    }
    Write-Host "C++ quality check passed: no new raw-pointer declarations, manual ownership, Qt parent ownership transfers, [this] captures, or lines over 150 characters."
}
finally {
    Pop-Location
}
