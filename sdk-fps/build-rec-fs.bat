@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set SDK=C:\Program Files\Artec\Artec 3D Scanning SDK
cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE /I"%SDK%\include" artecrec.cpp /Fo"artecrec-fs.obj" /link /SUBSYSTEM:WINDOWS /LIBPATH:"%SDK%\bin-x64" artec-sdk-base.lib artec-sdk-capturing.lib artec-sdk-scanning.lib artec-sdk-project.lib artec-sdk-algorithms.lib opengl32.lib gdi32.lib user32.lib comdlg32.lib shell32.lib comctl32.lib psapi.lib /OUT:artecrec-fs.exe
