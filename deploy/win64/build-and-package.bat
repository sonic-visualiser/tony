rem  Run this from within the top-level Tony dir: deploy\win64\build-and-package.bat

set STARTPWD=%CD%

if not exist "C:\Program Files (x86)\SMLNJ\bin" (
@   echo Could not find SML/NJ, required for Repoint
@   exit /b 2
)

if not exist "C:\Program Files (x86)\WiX Toolset v3.11\bin" (
@   echo Could not find WiX Toolset
@   exit /b 2
)

set ORIGINALPATH=%PATH%
set PATH=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Bin;%PATH%
set NAME=Open Source Developer, Christopher Cannam

set ARG=%1
shift
if "%ARG%" == "sign" (
@   echo NOTE: sign option specified, will attempt to codesign exe and msi
@   echo NOTE: starting by codesigning an unrelated executable, so we know
@   echo NOTE: whether it'll work before doing the entire build
copy sv-dependency-builds\win64-msvc\bin\capnp.exe signtest.exe
signtool sign /v /n "%NAME%" /t http://time.certum.pl /fd sha1 signtest.exe
if errorlevel 1 exit /b %errorlevel%
signtool verify /pa signtest.exe
if errorlevel 1 exit /b %errorlevel%
del signtest.exe
@   echo NOTE: success
) else (
@   echo NOTE: sign option not specified, will not codesign anything
)

@echo ""
@echo Rebuilding 

cd %STARTPWD%
del /q /s build_win64
call .\deploy\win64\build-64.bat
if %errorlevel% neq 0 exit /b %errorlevel%

if "%ARG%" == "sign" (
@echo Signing 64-bit executables and libraries
signtool sign /v /n "%NAME%" /t http://time.certum.pl /fd sha1 build_win32\release\*.exe build_win64\release\*.dll
)

set PATH=%PATH%;"C:\Program Files (x86)\WiX Toolset v3.11\bin"

@echo Packaging 

cd %STARTPWD%\build_win64
del tony.msi
candle -v ..\deploy\win64\tony.wxs
light -b . -ext WixUIExtension -ext WixUtilExtension -v tony.wixobj
if %errorlevel% neq 0 exit /b %errorlevel%
del tony.wixobj
del tony.wixpdb

if "%ARG%" == "sign" (
@echo Signing 64-bit package
signtool sign /v /n "%NAME%" /t http://time.certum.pl /fd sha1 tony.msi
signtool verify /pa tony.msi
)

set PATH=%ORIGINALPATH%

@echo Done

