if (-not $env:VCVARS) { Write-Error "set VCVARS env var to your vcvars32.bat"; exit 1 }
if (-not (Test-Path $env:VCVARS)) { Write-Error "VCVARS not found: $env:VCVARS"; exit 1 }
& $env:VCVARS | Out-Null

Push-Location $PSScriptRoot

cl /nologo /O2 /LD /EHsc /std:c++latest /D_CRT_SECURE_NO_WARNINGS /I include `
   src\patcher.cpp src\hook.cpp src\redirect.cpp src\overlay.cpp src\overlay_load.cpp `
   src\manifest.cpp src\edat.cpp src\version_gate.cpp src\plugin.cpp src\ipc.cpp `
   src\version_shim.cpp /Fe:version.dll /link /OUT:version.dll

Remove-Item *.obj -ErrorAction SilentlyContinue

Pop-Location

Write-Host "done -> version.dll"
