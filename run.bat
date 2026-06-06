@ECHO OFF
REM ============================================================
REM  Run a .nex in the CSpect emulator
REM ============================================================
REM  CSpect 3.x boots with a built-in Next ROM, so no system ROM
REM  or SD card image needs to be installed in order to run.
REM
REM  IMPORTANT: -mmc must point to a NON-existent .img file (not a
REM  real folder). Pointing it at a real directory makes CSpect
REM  mount it as the SD root, find no NextZXOS and drop into 48K
REM  BASIC instead of autoloading the .nex.
REM ------------------------------------------------------------
REM  Usage:  run.bat            (runs nhnext.nex)
REM          run.bat my.nex
REM ============================================================

SET CSPECT_DIR=%~dp0..\CSpect

IF "%~1"=="" (SET NEX=nhnext.nex) ELSE (SET NEX=%~1)
SET MAPF=%NEX:.nex=.map%

REM -w4      window 4x        -tv   scanline filter (optional)
REM -zxnext  Next mode        -nextrom use the Next ROM
REM -mmc     SD image (none)  -zmap  load z88dk symbols for debugging
"%CSPECT_DIR%\CSpect.exe" -w4 -zxnext -nextrom -tv -mmc="%CSPECT_DIR%\sdcard.img" -zmap="%~dp0%MAPF%" "%~dp0%NEX%"
