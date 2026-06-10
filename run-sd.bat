@ECHO OFF
REM ============================================================
REM  Deploy + run for testing SAVE/RESTORE
REM ------------------------------------------------------------
REM  esxDOS file I/O needs a mounted, writable filesystem. The
REM  normal run.bat autoloads the .nex with NO SD card, so it
REM  cannot test save/restore. This script instead copies the
REM  built nexthack.nex into the NextZXOS SD image (zxnext.sd)
REM  with hdfmonkey and boots NextZXOS from it.
REM
REM  Usage:
REM    build.bat        REM build nexthack.nex first
REM    run-sd.bat       REM deploy into the SD image and boot it
REM
REM  Then in NextZXOS use the Browser to run  /nexthack.nex .
REM  In game: 'S' saves and returns to the title; on the next
REM  boot the save is loaded automatically (and then deleted).
REM ============================================================

SET CSPECT_DIR=%~dp0..\CSpect
SET HDF=%CSPECT_DIR%\hdfmonkey\windows-64\hdfmonkey.exe
SET SD=%CSPECT_DIR%\zxnext.sd

IF NOT EXIST "%~dp0nexthack.nex" (ECHO Build first with build.bat. & GOTO end)
IF NOT EXIST "%SD%" (ECHO SD image not found: "%SD%" & GOTO end)

ECHO Copying nexthack.nex into the SD image...
"%HDF%" put "%SD%" "%~dp0nexthack.nex" /nexthack.nex
IF ERRORLEVEL 1 (ECHO Failed to write to the SD image. & GOTO end)
ECHO OK: /nexthack.nex written.

ECHO Booting NextZXOS from the SD image...
"%CSPECT_DIR%\CSpect.exe" -w4 -zxnext -tv "%SD%"

:end
