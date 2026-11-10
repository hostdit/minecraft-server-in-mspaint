$ErrorActionPreference = "Stop"
$arch = if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq "Arm64") { "arm64" } else { "x64" }
$dll = Join-Path $PSScriptRoot "..\build\$arch\paint\Release\McPaint.dll"
$loader = Join-Path $PSScriptRoot "..\build\$arch\loader\Release\mcpaint_loader.exe"
if (-not (Test-Path $dll)) { throw "No build at $dll. Run cmake --build --preset windows-$arch first." }
$bin = Join-Path $env:LOCALAPPDATA "McPaint\bin"
New-Item -ItemType Directory -Force $bin | Out-Null
$target = Join-Path $bin ("McPaint-{0:yyyyMMdd-HHmmss}.dll" -f (Get-Date))
Copy-Item $dll $target
if (Test-Path $loader) { Copy-Item $loader (Join-Path $bin "mcpaint_loader.exe") }
Get-ChildItem $bin -Filter "McPaint-*.dll" | Where-Object FullName -ne $target | ForEach-Object { try { Remove-Item $_.FullName -ErrorAction Stop } catch {} }
"Deployed $arch build to $bin"
"  DLL:    $target"
"  Loader: $(Join-Path $bin 'mcpaint_loader.exe')"
""
"To inject: start mspaint.exe, then run mcpaint_loader.exe from $bin"
