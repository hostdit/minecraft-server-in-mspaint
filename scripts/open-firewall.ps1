#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"
$paint = Join-Path (Get-AppxPackage Microsoft.Paint).InstallLocation "PaintApp\mspaint.exe"
$name = "Minecraft Server in Paint"
Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow -Protocol TCP -LocalPort 25565 -Program $paint | Out-Null
"Allowed inbound 25565 for $paint"
