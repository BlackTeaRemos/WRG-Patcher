@echo off
setlocal
set SRC=%~dp0

if not defined VCVARS ( echo set VCVARS env var to your vcvars32.bat & exit /b 1 )
if not defined GAME ( echo set GAME env var to your Wargame Red Dragon install dir & exit /b 1 )
if not exist "%VCVARS%" ( echo VCVARS not found: %VCVARS% & exit /b 1 )
if not exist "%GAME%" ( echo GAME dir not found: %GAME% & exit /b 1 )

call "%VCVARS%" >nul
pushd "%SRC%"
echo === version.dll ===
cl /nologo /O2 /LD /EHsc /std:c++latest /D_CRT_SECURE_NO_WARNINGS /DWRG_RELEASE /I include src\patcher.cpp src\hook.cpp src\redirect.cpp src\overlay.cpp src\overlay_load.cpp src\manifest.cpp src\edat.cpp src\version_gate.cpp src\version_anchor.cpp src\plugin.cpp src\ipc.cpp src\mapdir.cpp src\tier.cpp src\revision.cpp src\version_shim.cpp /Fe:version.dll /link /DEF:src\version.def /OUT:version.dll
if errorlevel 1 ( echo PATCHER BUILD FAILED & popd & exit /b 1 )
del *.obj 2>nul
popd

copy /Y "%SRC%version.dll" "%GAME%\version.dll" >nul
if errorlevel 1 ( echo COPY version FAILED & exit /b 1 )
echo staged version.dll + defer_op.dll -> %GAME%
endlocal
