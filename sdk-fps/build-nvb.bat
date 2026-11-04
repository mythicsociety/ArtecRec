@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SDK=C:\Program Files\Artec\Artec 3D Scanning SDK
cl /nologo /EHsc /O2 /I"%SDK%\include" nvbase.cpp /link /OUT:nvbase.exe


