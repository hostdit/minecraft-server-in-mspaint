Stop-Process -Name mspaint -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process mspaint.exe
"Paint restarted"
