@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0..\.."
if not exist build\obj mkdir build\obj
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /DUNICODE /D_UNICODE /DDESKBOX_TESTING tests\hosting\session.cpp src\BoxModel.cpp src\DesktopSession.cpp src\ShellIcon.cpp src\DesktopHost.cpp src\DesktopItems.cpp src\log.cpp /Fo"build\obj\\" /Fe"build\hosting-session-test.exe" /link user32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib gdiplus.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1
build\hosting-session-test.exe
set RESULT=%errorlevel%
popd
exit /b %RESULT%
