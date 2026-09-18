@echo off
rem ---------------------------------------------------------------
rem nestlone-D build script.
rem   usage: build.bat [outdir] [exename] [srcdir] [extraflags]
rem ASCII-only on purpose: cmd.exe mis-parses batch files when the
rem code page changes mid-file with multibyte chars present, so no
rem chcp here. Chinese comments live in the .cpp/.h files instead,
rem and cl reads them correctly thanks to /utf-8.
rem ---------------------------------------------------------------
setlocal
if "%VSDIR%"=="" set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" set VSDIR=C:\Program Files\Microsoft Visual Studio\2022\BuildTools
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" set VSDIR=
if "%VSDIR%"=="" set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if "%VSDIR%"=="" if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%I
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (echo [BUILD] Visual Studio Build Tools not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo [BUILD] vcvars64 failed & exit /b 1)

set OUTDIR=%~f1
if "%OUTDIR%"=="" set OUTDIR=%~dp0build
set OUTNAME=%~2
if "%OUTNAME%"=="" set OUTNAME=nestlone-D.exe
set SRCDIR=%~3
if "%SRCDIR%"=="" set SRCDIR=%~dp0src
set EXTRA=%~4
set RCFILE=%~dp0src\DeskBox.rc

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
set OBJDIR=%~dp0build\obj
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
pushd "%OUTDIR%"
rc /nologo /fo"%OBJDIR%\DeskBox.res" "%RCFILE%"
if errorlevel 1 (echo [BUILD] resource compilation failed & popd & exit /b 1)
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /DUNICODE /D_UNICODE %EXTRA% ^
   /Fo"%OBJDIR%\\" /Fe"%OUTDIR%\%OUTNAME%" "%SRCDIR%\*.cpp" ^
   "%OBJDIR%\DeskBox.res" /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%~dp0src\compatibility.xml" ^
   user32.lib shell32.lib shlwapi.lib gdiplus.lib gdi32.lib ole32.lib oleaut32.lib comctl32.lib advapi32.lib
set RC=%errorlevel%
popd
if not "%RC%"=="0" (echo [BUILD] FAILED rc=%RC% & exit /b %RC%)
echo [BUILD] OK - %OUTDIR%\%OUTNAME%
exit /b 0
