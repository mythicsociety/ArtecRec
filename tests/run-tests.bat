@echo off
rem ArtecRec test suite. Builds and runs every level, prints PASS/FAIL/SKIP per test and a summary.
rem   run-tests.bat            unit + integration + system + e2e tooling self-check
rem   run-tests.bat unit       only one level (unit | integration | system | e2e)
rem   set FULL=1               also run the uncancelled registration timings (integration, +2-3 min)
rem Exit code 0 = no test failed. Tests named *FINDING* fail on purpose while the app rule they guard is wrong.
rem Integration tests: ~1.1 GB copy of C:\Artec\stage in %TEMP%\artecrec-tests (kept between runs), ~6 GB RAM, ~5 min.
rem System tests: start a COPY of C:\Artec\sdk-fps\artecrec.exe from %TEMP%\artecrec-systest (its window appears on screen
rem for ~3 min; do not click in it). Nothing is written to C:\Artec\sdk-fps, C:\Artec\stage or D:\.
setlocal EnableDelayedExpansion
call "%~dp0env.bat"
set "LEVEL=%~1"
if "%LEVEL%"=="" set LEVEL=all
set "BIN=%TESTS%\bin"
set "TEST_RESULTS=%BIN%\results.txt"
if exist "%TEST_RESULTS%" del "%TEST_RESULTS%"
set BLEVEL=%LEVEL%
if /i "%LEVEL%"=="e2e" set BLEVEL=none
if /i not "%BLEVEL%"=="none" call "%TESTS%\build-tests.bat" %BLEVEL% || (echo BUILD FAILED & exit /b 2)
set T0=%TIME%

if /i "%LEVEL%"=="all" goto do_unit
if /i "%LEVEL%"=="unit" goto do_unit
goto end_unit
:do_unit
"%BIN%\test_logic.exe"
"%BIN%\test_feat.exe"
:end_unit

if /i "%LEVEL%"=="all" goto do_integ
if /i "%LEVEL%"=="integration" goto do_integ
goto end_integ
:do_integ
rem one process per test: a hung SDK job is killed with its process and the next test still runs
set ITESTS=fixture_copy open_project_counts save_and_reopen_round_trip save_writes_only_project_files save_into_existing_folder kannada_names_in_sproj loaded_scan_names_match_sproj auto_align_two_scans_returns cancel_serial_registration cancel_global_registration cancel_global_before_start flatness_of_real_windows
if "%FULL%"=="1" set ITESTS=%ITESTS% baseline_serial_registration baseline_global_registration
for %%t in (%ITESTS% cleanup_saved_copies) do "%BIN%\itest_sdk.exe" %%t 2>&1 | findstr /v /c:"Failed to decrypt config" /c:"=== integration"
:end_integ

if /i "%LEVEL%"=="all" goto do_sys
if /i "%LEVEL%"=="system" goto do_sys
goto end_sys
:do_sys
"%BIN%\systest.exe"
:end_sys

if /i "%LEVEL%"=="all" goto do_e2e
if /i "%LEVEL%"=="e2e" goto do_e2e
goto end_e2e
:do_e2e
echo === e2e tooling: verify-log.ps1 self-check ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%TESTS%\e2e\verify-log.ps1" -Log "%TESTS%\e2e\samples\good-session.log" -Quiet >nul
if !ERRORLEVEL!==0 (echo PASS  verify_log_accepts_good_session & >>"%TEST_RESULTS%" echo e2e 1 0 0 0) else (echo FAIL  verify_log_accepts_good_session & >>"%TEST_RESULTS%" echo e2e 0 1 0 0)
powershell -NoProfile -ExecutionPolicy Bypass -File "%TESTS%\e2e\verify-log.ps1" -Log "%TESTS%\e2e\samples\bad-session.log" -Quiet >nul
if !ERRORLEVEL!==1 (echo PASS  verify_log_flags_bad_session & >>"%TEST_RESULTS%" echo e2e 1 0 0 0) else (echo FAIL  verify_log_flags_bad_session & >>"%TEST_RESULTS%" echo e2e 0 1 0 0)
:end_e2e

echo.
echo ================= SUMMARY =================
set /a TP=0, TF=0, TS=0
if not exist "%TEST_RESULTS%" (echo no results & exit /b 2)
for %%L in (unit-logic unit-feat integration system e2e) do (
  set /a P=0, F=0, S=0, N=0
  for /f "tokens=1-4" %%a in ('type "%TEST_RESULTS%"') do if "%%a"=="%%L" set /a P+=%%b, F+=%%c, S+=%%d, N+=1
  if !N! gtr 0 echo   %%L: !P! passed, !F! failed, !S! skipped
  set /a TP+=!P!, TF+=!F!, TS+=!S!
)
echo   TOTAL: %TP% passed, %TF% failed, %TS% skipped   (started %T0%, ended %TIME%)
if %TF% gtr 0 (echo   RESULT: FAIL & exit /b 1)
echo   RESULT: PASS
exit /b 0
