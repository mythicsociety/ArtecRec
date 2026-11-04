@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SDK=C:\Program Files\Artec\Artec 3D Scanning SDK
cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE /I"%SDK%\include" regtest2.cpp /link /SUBSYSTEM:CONSOLE /LIBPATH:"%SDK%\bin-x64" artec-sdk-base.lib artec-sdk-project.lib artec-sdk-algorithms.lib shell32.lib /OUT:regtest2.exe
