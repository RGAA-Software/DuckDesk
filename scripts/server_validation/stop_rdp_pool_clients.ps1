#requires -Version 5.1
param([Parameter(Mandatory)][string]$BatchReport,[string]$ClientRoot='C:\Program Files\Pixels Client')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$expectedPath=[IO.Path]::GetFullPath((Join-Path $ClientRoot 'px_client.exe'))
$reports=Get-Content -LiteralPath $BatchReport -Raw|ConvertFrom-Json
foreach($report in $reports){
    $current=Get-CimInstance Win32_Process -Filter ('ProcessId='+[int]$report.pid)
    if(-not $current){$report.state='closed';continue}
    if($current.ExecutablePath -ine $expectedPath){throw 'Owned Client PID path differs'}
    $client=[Diagnostics.Process]::GetProcessById([int]$report.pid)
    try{
        if($client.StartTime.ToUniversalTime().ToString('o') -ne $report.process_created_utc){throw 'Owned Client PID was reused'}
        [void]$client.CloseMainWindow()
        if(-not $client.WaitForExit(10000)){$client.Kill();if(-not $client.WaitForExit(10000)){throw 'Owned Client did not exit'}}
        $report.state='closed'
    }finally{$client.Dispose()}
}
[IO.File]::WriteAllText($BatchReport,($reports|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$reports|ConvertTo-Json -Depth 5 -Compress
