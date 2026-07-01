@ECHO OFF
REM ============================================================
REM  Run the ZX Spectrum 128K build (nexthack128.tap) in ZEsarUX
REM  with the ZRCP remote protocol enabled (TCP :10000), so the
REM  build agent can verify rendering / drive the game via ZRCP.
REM  (ZEsarUX lives one dir up, in ..\ZEsarUX.)
REM
REM  Loads the .TAP, not the .sna: appmake's .sna boots the resident
REM  title but CRASHES on the first banked call (build_level) because
REM  it does not carry the code-banked layout into the RAM banks. Only
REM  the .tap's bank-paging loader sets the banks up, so only it runs.
REM
REM  At the 128K boot menu, press ENTER (Tape Loader) to LOAD "" the
REM  tape; the game then autostarts.
REM
REM  Usage:  run-zx128.bat            (runs nexthack128.tap)
REM          run-zx128.bat my.tap
REM ============================================================
SET ZES_DIR=%~dp0..\ZEsarUX

IF "%~1"=="" (SET TAP=%~dp0nexthack128.tap) ELSE (SET TAP=%~1)

CD /D "%ZES_DIR%"
zesarux.exe --machine 128k "%TAP%" --enable-remoteprotocol --nowelcomemessage --quickexit
