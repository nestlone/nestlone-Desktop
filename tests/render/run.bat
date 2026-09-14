@echo off
setlocal
set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0..\.."
if not exist build\obj mkdir build\obj
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE tests\render\main.cpp src\BoxModel.cpp src\DesktopItems.cpp src\DesktopSession.cpp src\ShellIcon.cpp src\DesktopHost.cpp src\log.cpp /Fo"build\obj\\" /Fe"build\render-test.exe" /link user32.lib shell32.lib shlwapi.lib gdi32.lib gdiplus.lib ole32.lib oleaut32.lib comctl32.lib
if errorlevel 1 exit /b 1
build\render-test.exe
set RESULT=%errorlevel%
popd
exit /b %RESULT%
