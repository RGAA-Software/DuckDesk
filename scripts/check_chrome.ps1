Get-Process chrome -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -ne '' } | Select-Object Id, SessionId, MainWindowTitle | Format-Table -AutoSize
