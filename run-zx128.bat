@ECHO OFF
REM ============================================================
REM  Run the ZX Spectrum 128K build (nexthack128.sna) in ZEsarUX
REM  with the ZRCP remote protocol enabled (TCP :10000), so the
REM  build agent can verify rendering / drive the game via ZRCP.
REM  (ZEsarUX lives one dir up, in ..\ZEsarUX.)
REM
REM  Usage:  run-zx128.bat            (runs nexthack128.sna)
REM          run-zx128.bat my.sna
REM ============================================================
SET ZES_DIR=%~dp0..\ZEsarUX

IF "%~1"=="" (SET SNA=%~dp0nexthack128.sna) ELSE (SET SNA=%~1)

CD /D "%ZES_DIR%"
zesarux.exe --machine 128k "%SNA%" --enable-remoteprotocol --nowelcomemessage --quickexit
