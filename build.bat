@ECHO OFF
REM === NetHack-Next build ===
REM   build.bat            builds the full game (all modules) -> nhnext.nex
REM   build.bat foo.c      builds a single source file        -> foo.nex

SET Z88DK_DIR=%~dp0..\z88dk
SET ZCCCFG=%Z88DK_DIR%\lib\config\
SET PATH=%Z88DK_DIR%\bin;%PATH%

SET FLAGS=+zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -m

IF NOT "%~1"=="" GOTO single

SET SRCS=nhnext.c platform.c rng.c level.c monster.c item.c
ECHO Building NetHack Next (nhnext.nex) ...
zcc %FLAGS% %SRCS% -o nhnext -create-app
IF EXIST nhnext.nex (ECHO. & ECHO OK: nhnext.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)
GOTO end

:single
ECHO Building %~1 -^> %~n1.nex ...
zcc %FLAGS% %~1 -o %~n1 -create-app
IF EXIST %~n1.nex (ECHO. & ECHO OK: %~n1.nex built.) ELSE (ECHO. & ECHO BUILD FAILED.)

:end
