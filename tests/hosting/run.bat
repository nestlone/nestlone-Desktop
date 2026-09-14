@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0..\.."
cl /nologo /std:c++17 /utf-8 /EHsc /W4 tests\hosting\probe.cpp /Fo"build\obj\\" /Fe"build\hosting-probe.exe" /link user32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib
if errorlevel 1 exit /b 1
build\hosting-probe.exe "%~1" "%~2"
set RESULT=%errorlevel%
popd
exit /b %RESULT%
