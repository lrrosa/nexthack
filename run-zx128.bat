@ECHO OFF
REM ============================================================
REM  Run the ZX Spectrum 128K build (nexthack128.tap) in ZEsarUX
REM  with the ZRCP remote protocol enabled (TCP :10000), so the
REM  build agent can verify rendering / drive the game via ZRCP.
REM  (ZEsarUX lives one dir up, in ..\ZEsarUX.)
REM
REM  --noconfigfile is load-bearing: the shared .zesaruxrc that
REM  run-next.bat leaves behind sets --machine TBBlue and
REM  --autoloadsnap, which force the Next machine and reload the
REM  last .nex ON TOP of our tape -- so without it, this boots the
REM  Next build, not the 128K. Ignoring the config also skips
REM  --saveconf-on-exit, so we do NOT clobber the user's config.
REM  (--noconfigfile must be the first parameter.)
REM
REM  Load the .TAP, not the .sna: appmake's .sna boots the resident
REM  title then CRASHES on the first banked call (it doesn't carry
REM  the code-banked layout into the RAM banks); only the .tap's
REM  bank-paging loader sets the banks up.
REM
REM  esxdos-root-dir + diviface-ram-size mirror the config so 128K
REM  save ('S') still works, writing nexthack.sav beside the .tap.
REM  (Do NOT add --enable-divmmc-paging: it maps the DivMMC ROM over
REM  the tape loader and the game never boots.)
REM
REM  ZEsarUX auto-loads the inserted tape (its default with no config),
REM  so there's no 128K-menu Enter to press -- it boots to the title.
REM
REM  Usage:  run-zx128.bat            (runs nexthack128.tap)
REM          run-zx128.bat my.tap
REM ============================================================
SET ZES_DIR=%~dp0..\ZEsarUX

IF "%~1"=="" (SET TAP=%~dp0nexthack128.tap) ELSE (SET TAP=%~1)

CD /D "%ZES_DIR%"
zesarux.exe --noconfigfile --machine 128k --esxdos-root-dir "%~dp0." --diviface-ram-size 128 --tape "%TAP%" --enable-remoteprotocol --nowelcomemessage --quickexit
