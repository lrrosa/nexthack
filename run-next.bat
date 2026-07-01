@ECHO OFF
REM ============================================================
REM  Run the ZX Spectrum Next build (nexthack.nex) in ZEsarUX
REM  with the ZRCP remote protocol (TCP :10000) enabled, so the
REM  build agent can verify rendering / drive the game via ZRCP.
REM  (ZEsarUX lives one dir up, in ..\ZEsarUX.)
REM
REM  Smartloading the .nex switches ZEsarUX to the tbblue machine
REM  AND auto-mounts esxDOS onto the .nex's own folder -- so file
REM  I/O, and thus SAVE/RESTORE ('S' in game), works against this
REM  folder with NO SD image (that is why run-sd.bat is gone).
REM
REM  Usage:  run-next.bat            (runs nexthack.nex)
REM          run-next.bat my.nex
REM ============================================================
SET ZES_DIR=%~dp0..\ZEsarUX

IF "%~1"=="" (SET NEX=%~dp0nexthack.nex) ELSE (SET NEX=%~1)

CD /D "%ZES_DIR%"
zesarux.exe "%NEX%" --enable-remoteprotocol --nowelcomemessage --quickexit
