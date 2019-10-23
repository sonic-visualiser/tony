@rem  Run this from within the top-level dir: deploy\clean-build-and-package
@echo on

@set /p VERSION=<version.h
@set VERSION=%VERSION:#define TONY_VERSION "=%
set VERSION=%VERSION:"=%

@echo(
@set YN=y
@set /p YN="Proceed to clean, rebuild, package, and sign version %VERSION% [Yn] ?"

@if "%YN%" == "Y" set YN=y
@if "%YN%" neq "y" exit /b 3

@echo Proceeding
call .\deploy\win64\build-and-package.bat sign
if %errorlevel% neq 0 exit /b %errorlevel%

mkdir packages
copy build_win64\tony.msi packages\tony-%VERSION%-win64.msi

@echo(
@echo Done

   
