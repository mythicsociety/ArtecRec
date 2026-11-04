@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SDK=C:\Program Files\Artec\Artec 3D Scanning SDK
cl /nologo /EHsc /O2 /I"%SDK%\include" sdkscan.cpp /link /LIBPATH:"%SDK%\bin-x64" artec-sdk-base.lib artec-sdk-capturing.lib artec-sdk-scanning.lib artec-sdk-project.lib psapi.lib /OUT:sdkscan.exe


