@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE zspread_ply.cpp /link /SUBSYSTEM:CONSOLE /OUT:zspread_ply.exe
