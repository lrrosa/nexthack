@ECHO OFF
REM === NextHack build (banked; nightly z88dk) ===
REM   build.bat            builds the full game (all modules) -> nexthack.nex
REM   build.bat foo.c      builds a single source file        -> foo.nex
REM
REM The game is code-banked (>64K): hot code + all data resident in 0x8000-0xBFF0,
REM cold code in PAGE_20/PAGE_22 (the 0xC000 window). That needs the nightly
REM z88dk (__banked trampoline) and the banking pragmas (zpragma.inc/mmap.inc).
REM build.ps1 is the faster incremental+parallel equivalent; prefer it.

SET Z88DK_DIR=%~dp0..\z88dk-latest
SET ZCCCFG=%Z88DK_DIR%\lib\config\
SET PATH=%Z88DK_DIR%\bin;%PATH%

SET FLAGS=+zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -startup=1 -pragma-include:zpragma.inc -m

IF NOT "%~1"=="" GOTO single

SET SRCS=mainentry.c nexthack.c platform.c platform_init.c rng.c level.c levelgen.c levelfov.c monster.c monster_ai.c item.c sfx.c titlegfx0.c titlegfx1.c titlegfx2.c titlepal.c
ECHO Building NextHack (nexthack.nex) ...
zcc %FLAGS% %SRCS% -o nexthack -create-app
IF EXIST nexthack.nex (ECHO. & ECHO OK: nexthack.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)
GOTO end

:single
ECHO Building %~1 -^> %~n1.nex ...
zcc %FLAGS% %~1 -o %~n1 -create-app
IF EXIST %~n1.nex (ECHO. & ECHO OK: %~n1.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)

:end
