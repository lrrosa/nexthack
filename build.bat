@ECHO OFF
REM === NextHack build (banked; nightly z88dk) ===
REM   build.bat            builds the full game (all modules) -> nexthack.nex
REM   build.bat -Clean     forces a full rebuild
REM   build.bat foo.c      builds a single source file        -> foo.nex
REM
REM The game is code-banked (>64K): hot code + all data resident in 0x8000-0xBFF0,
REM cold code in PAGE_20/22/26/28 (the 0xC000 window). That needs the nightly
REM z88dk (__banked trampoline) and the banking pragmas (zpragma.inc/mmap.inc).
REM
REM WHICH bank each module lands in now comes from banks.json, applied as zcc's
REM --codeseg/--constseg -- and those are per-INVOCATION options, so the full
REM build needs one zcc call per module. That build lives in build.ps1
REM (incremental + parallel), and this script forwards to it rather than keeping
REM a second copy of the bank assignment. The single-file path below builds a
REM standalone test program, which is unbanked, so it stays native.

SET Z88DK_DIR=%~dp0..\z88dk
SET ZCCCFG=%Z88DK_DIR%\lib\config\
SET PATH=%Z88DK_DIR%\bin;%PATH%

IF "%~1"=="" GOTO full
IF /I "%~x1"==".c" GOTO single

:full
WHERE pwsh >NUL 2>&1
IF ERRORLEVEL 1 (
  ECHO ERROR: pwsh [PowerShell 7+] not found, and the full build needs it.
  ECHO        Install PowerShell 7 or run build.ps1 from a pwsh prompt.
  GOTO end
)
ECHO Building NextHack (nexthack.nex) via build.ps1 ...
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
GOTO end

:single
SET FLAGS=+zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -startup=1 -pragma-include:zpragma.inc -m
ECHO Building %~1 -^> %~n1.nex ...
zcc %FLAGS% %~1 -o %~n1 -create-app
IF EXIST %~n1.nex (ECHO. & ECHO OK: %~n1.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)

:end
