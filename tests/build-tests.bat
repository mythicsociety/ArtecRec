@echo off
rem Builds every test executable into C:\Artec\tests\bin. Usage: build-tests.bat [unit|integration|system|all]
rem The unit tests include rec_logic.h / rec_feat.h straight from C:\Artec\sdk-fps, so they always test the current source.
setlocal
call "%~dp0env.bat"
set "LEVEL=%~1"
if "%LEVEL%"=="" set LEVEL=all
if not exist "%TESTS%\bin" mkdir "%TESTS%\bin"
if not exist "%TESTS%\bin\obj" mkdir "%TESTS%\bin\obj"
set "CL_COMMON=/nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE /W3 /wd4996 /Fo"%TESTS%\bin\obj\\" /I"%APPSRC%" /I"%TESTS%""
set ERR=0

if /i "%LEVEL%"=="unit" goto unit
if /i "%LEVEL%"=="all" goto unit
goto after_unit
:unit
echo [build] unit: test_logic, test_feat
cl %CL_COMMON% "%TESTS%\unit\test_logic.cpp" /Fe"%TESTS%\bin\test_logic.exe" /link user32.lib >"%TESTS%\bin\build-test_logic.txt" 2>&1 || (type "%TESTS%\bin\build-test_logic.txt" & set ERR=1)
cl %CL_COMMON% "%TESTS%\unit\test_feat.cpp" /Fe"%TESTS%\bin\test_feat.exe" >"%TESTS%\bin\build-test_feat.txt" 2>&1 || (type "%TESTS%\bin\build-test_feat.txt" & set ERR=1)
:after_unit

if /i "%LEVEL%"=="integration" goto integ
if /i "%LEVEL%"=="all" goto integ
goto after_integ
:integ
echo [build] integration: itest_sdk
cl %CL_COMMON% /I"%SDK%\include" "%TESTS%\integration\itest_sdk.cpp" /Fe"%TESTS%\bin\itest_sdk.exe" /link /SUBSYSTEM:CONSOLE /LIBPATH:"%SDK%\bin-x64" artec-sdk-base.lib artec-sdk-project.lib artec-sdk-algorithms.lib shell32.lib user32.lib >"%TESTS%\bin\build-itest_sdk.txt" 2>&1 || (type "%TESTS%\bin\build-itest_sdk.txt" & set ERR=1)
:after_integ

if /i "%LEVEL%"=="system" goto sys
if /i "%LEVEL%"=="all" goto sys
goto after_sys
:sys
echo [build] system: systest
cl %CL_COMMON% "%TESTS%\system\systest.cpp" /Fe"%TESTS%\bin\systest.exe" /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib >"%TESTS%\bin\build-systest.txt" 2>&1 || (type "%TESTS%\bin\build-systest.txt" & set ERR=1)
:after_sys

if %ERR%==1 (echo [build] FAILED & exit /b 1)
echo [build] ok
exit /b 0
