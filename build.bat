@ECHO OFF
REM === NetHack-Next build ===
REM Sets up the z88dk environment and compiles <file>.c -> <file>.nex
REM Usage:  build.bat            (builds hello.c)
REM         build.bat my.c       (builds my.c -> my.nex)

SET Z88DK_DIR=%~dp0..\z88dk
SET ZCCCFG=%Z88DK_DIR%\lib\config\
SET PATH=%Z88DK_DIR%\bin;%PATH%

IF "%~1"=="" (SET SRC=hello.c) ELSE (SET SRC=%~1)
SET OUT=%~n1
IF "%OUT%"=="" SET OUT=hello

ECHO Building %SRC% -^> %OUT%.nex ...
zcc +zxn -subtype=nex -vn -SO3 -clib=sdcc_iy --max-allocs-per-node200000 -m %SRC% -o %OUT% -create-app

IF EXIST %OUT%.nex (
    ECHO.
    ECHO OK: %OUT%.nex built.
) ELSE (
    ECHO.
    ECHO BUILD FAILED.
)
