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
REM  Save ('S') works through ZEsarUX's esxDOS TRAPS handler:
REM  --enable-esxdos-handler serves the esxDOS API from the host dir
REM  (esxdos-root-dir), and --enable-divmmc-ports satisfies it without
REM  the automapper. The game detects this via its guarded API probe
REM  (esxdetect.asm): the 0xE3 hardware probe fails here (no paging
REM  emulation), then the RST 8 probe finds the handler.
REM  (Do NOT use --enable-divmmc-paging or --enable-divmmc: the
REM  automapper hijacks the boot and the tape loader never runs.)
REM  Emulator quirk: the handler doesn't implement F_UNLINK, so the
REM  save is NOT deleted after a restore here (it is on real esxDOS)
REM  -- convenient for testing, but not the anti-save-scum behaviour.
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
zesarux.exe --noconfigfile --machine 128k --enable-divmmc-ports --enable-esxdos-handler --esxdos-root-dir "%~dp0." --diviface-ram-size 128 --tape "%TAP%" --enable-remoteprotocol --nowelcomemessage --quickexit
