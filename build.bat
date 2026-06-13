@ECHO OFF
REM === NextHack build ===
REM   build.bat            builds the full game (all modules) -> nexthack.nex
REM   build.bat foo.c      builds a single source file        -> foo.nex

SET Z88DK_DIR=%~dp0..\z88dk
SET ZCCCFG=%Z88DK_DIR%\lib\config\
SET PATH=%Z88DK_DIR%\bin;%PATH%

SET FLAGS=+zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -m

IF NOT "%~1"=="" GOTO single

SET SRCS=mainentry.c nexthack.c platform.c rng.c level.c levelgen.c monster.c monster_ai.c item.c sfx.c
ECHO Building NextHack (nexthack.nex) ...
zcc %FLAGS% %SRCS% -o nexthack -create-app
IF EXIST nexthack.nex (ECHO. & ECHO OK: nexthack.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)
GOTO end

:single
ECHO Building %~1 -^> %~n1.nex ...
zcc %FLAGS% %~1 -o %~n1 -create-app
IF EXIST %~n1.nex (ECHO. & ECHO OK: %~n1.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)

:end
