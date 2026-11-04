@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE meshcheck.cpp /link /SUBSYSTEM:CONSOLE /OUT:meshcheck.exe
