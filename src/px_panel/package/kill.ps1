Get-WmiObject Win32_Process -Filter "name = 'px_panel.exe'" | Invoke-WmiMethod -Name Terminate | Out-Null
Get-WmiObject Win32_Process -Filter "name = 'px_client.exe'" | Invoke-WmiMethod -Name Terminate | Out-Null
Get-WmiObject Win32_Process -Filter "name = 'px_render.exe'" | Invoke-WmiMethod -Name Terminate | Out-Null
Get-WmiObject Win32_Process -Filter "name = 'px_service.exe'" | Invoke-WmiMethod -Name Terminate | Out-Null
Get-WmiObject Win32_Process -Filter "name = 'px_service_manager.exe'" | Invoke-WmiMethod -Name Terminate | Out-Null
