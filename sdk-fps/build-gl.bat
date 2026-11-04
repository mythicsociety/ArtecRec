@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SDK=C:\Program Files\Artec\Artec 3D Scanning SDK
cl /nologo /EHsc /O2 /I"%SDK%\include" gltest.cpp /link opengl32.lib gdi32.lib user32.lib /OUT:gltest.exe


