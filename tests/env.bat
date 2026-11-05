@echo off
rem Compiler + SDK environment, copied from C:\Artec\sdk-fps\build-rec.bat. Called by build-tests.bat and run-tests.bat.
if not defined VCINSTALLDIR call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "SDK=C:\Program Files\Artec\Artec 3D Scanning SDK"
set "APPSRC=C:\Artec\sdk-fps"
set "TESTS=%~dp0"
set "TESTS=%TESTS:~0,-1%"
rem the SDK DLLs are not on PATH on this PC; the integration and system tests need them at run time
echo %PATH% | find /i "%SDK%\bin-x64" >nul || set "PATH=%SDK%\bin-x64;%PATH%"
