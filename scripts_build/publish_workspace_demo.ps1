$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'build_workspace_demo'
$destination = Join-Path $repo 'build_official/dist/workspace_ui_demo'
$null = New-Item -ItemType Directory -Path $destination -Force
foreach ($name in @('workspace_ui_demo.exe', 'workspace.xml', 'duilib-copyright.txt')) {
    $inputFile = Join-Path $source $name
    $outputFile = Join-Path $destination $name
    Copy-Item -LiteralPath $inputFile -Destination $outputFile -Force
    $expected = (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $outputFile -Algorithm SHA256).Hash -ne $expected) {
        throw "Published file hash mismatch: $name"
    }
    "$name SHA256=$expected"
}
